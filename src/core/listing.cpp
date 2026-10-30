#include "core/listing.h"

#include "core/gobject.h"
#include "core/sort.h"

namespace core {

namespace {

const char* const kAttributes =
    "standard::name,standard::display-name,standard::type,"
    "standard::is-hidden,standard::is-symlink";

// The first batch is small so a screenful paints as soon as possible.
constexpr int kFirstBatch = 512;
constexpr int kBatch = 4096;

EntryType typeOf(GFileType t)
{
    switch (t) {
    case G_FILE_TYPE_REGULAR:
        return EntryType::File;
    case G_FILE_TYPE_DIRECTORY:
        return EntryType::Directory;
    case G_FILE_TYPE_SYMBOLIC_LINK:
        return EntryType::Symlink;
    case G_FILE_TYPE_SPECIAL:
    case G_FILE_TYPE_SHORTCUT:
    case G_FILE_TYPE_MOUNTABLE:
        return EntryType::Special;
    default:
        return EntryType::Unknown;
    }
}

Entry toEntry(GFileInfo* info)
{
    Entry e;
    e.name = QByteArray(g_file_info_get_name(info));
    const char* display = g_file_info_get_display_name(info);
    e.display = display ? QString::fromUtf8(display) : QString::fromUtf8(e.name);
    e.type = typeOf(g_file_info_get_file_type(info));
    e.hidden = g_file_info_get_is_hidden(info);
    e.symlink = g_file_info_get_is_symlink(info);
    computeSortKey(e);
    return e;
}

} // namespace

// One load. Owned by its own callback chain: it deletes itself when the
// chain ends, whether by completion, error or cancellation. owner is
// cleared when the Listing moves on, so a late reply goes nowhere.
struct Listing::Job {
    Listing* owner = nullptr;
    quint64 generation = 0;
    GPtr<GCancellable> cancellable;
    GPtr<GFileEnumerator> enumerator;
    bool first = true;

    void detach()
    {
        if (owner && owner->job_ == this)
            owner->job_ = nullptr;
        owner = nullptr;
    }

    void fail(GErrorPtr error)
    {
        if (owner && !isCancelled(error.get())) {
            ListFailure why = ListFailure::Other;
            if (g_error_matches(error.get(), G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED))
                why = ListFailure::NotMounted;
            else if (g_error_matches(error.get(), G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY))
                why = ListFailure::NotDirectory;
            else if (g_error_matches(error.get(), G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
                why = ListFailure::NotSupported;
            Q_EMIT owner->failed(generation, errorMessage(error.get()), why);
        }
        detach();
        delete this;
    }

    void complete()
    {
        g_file_enumerator_close_async(enumerator.get(), G_PRIORITY_DEFAULT, nullptr, nullptr, nullptr);
        if (owner)
            Q_EMIT owner->finished(generation);
        detach();
        delete this;
    }

    void requestNext()
    {
        g_file_enumerator_next_files_async(enumerator.get(), first ? kFirstBatch : kBatch,
                                           G_PRIORITY_DEFAULT, cancellable.get(), &Job::onNext, this);
        first = false;
    }

    static void onEnumerated(GObject* source, GAsyncResult* result, gpointer data)
    {
        auto* job = static_cast<Job*>(data);
        GError* error = nullptr;
        GFileEnumerator* en = g_file_enumerate_children_finish(G_FILE(source), result, &error);
        if (!en) {
            job->fail(GErrorPtr(error));
            return;
        }
        job->enumerator.reset(en);
        if (!job->owner) {
            delete job;
            return;
        }
        job->requestNext();
    }

    static void onNext(GObject* source, GAsyncResult* result, gpointer data)
    {
        auto* job = static_cast<Job*>(data);
        GError* error = nullptr;
        GList* infos = g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(source), result, &error);
        if (error) {
            job->fail(GErrorPtr(error));
            return;
        }
        if (!infos) {
            job->complete();
            return;
        }
        std::vector<Entry> entries;
        entries.reserve(g_list_length(infos));
        for (GList* l = infos; l; l = l->next)
            entries.push_back(toEntry(G_FILE_INFO(l->data)));
        g_list_free_full(infos, g_object_unref);

        if (job->owner)
            Q_EMIT job->owner->batch(job->generation, std::move(entries));
        // The receiver may have moved on during the emit.
        if (!job->owner) {
            delete job;
            return;
        }
        job->requestNext();
    }
};

Listing::Listing(QObject* parent)
    : QObject(parent)
{
}

Listing::~Listing()
{
    cancel();
}

void Listing::start(const Location& dir, quint64 generation)
{
    cancel();
    if (!dir.isValid()) {
        Q_EMIT failed(generation, QStringLiteral("invalid location"), ListFailure::Other);
        return;
    }
    auto* job = new Job;
    job->owner = this;
    job->generation = generation;
    job->cancellable.reset(g_cancellable_new());
    job_ = job;
    g_file_enumerate_children_async(dir.gfile(), kAttributes, G_FILE_QUERY_INFO_NONE,
                                    G_PRIORITY_DEFAULT, job->cancellable.get(),
                                    &Job::onEnumerated, job);
}

void Listing::cancel()
{
    if (!job_)
        return;
    g_cancellable_cancel(job_->cancellable.get());
    job_->owner = nullptr;
    job_ = nullptr;
}

} // namespace core
