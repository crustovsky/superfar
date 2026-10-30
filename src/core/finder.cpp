#include "core/finder.h"

#include "core/gobject.h"
#include "core/mainthread.h"
#include "core/sort.h"

#include <QElapsedTimer>
#include <atomic>
#include <thread>

namespace core {

namespace {
const char* const kAttributes =
    "standard::name,standard::display-name,standard::type,standard::is-hidden,standard::is-symlink,unix::device";
constexpr size_t kBatch = 200;
constexpr size_t kLimit = 20000;
constexpr qint64 kBatchMs = 150;
} // namespace

struct Finder::Run {
    Finder* owner;  // main thread only; cleared by cancel()
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    Location root;
    QString pattern;
    quint64 generation;
    GPtr<GCancellable> cancellable{g_cancellable_new()};
    std::thread thread;

    bool cancelled() const { return g_cancellable_is_cancelled(cancellable.get()); }

    void post(std::function<void(Finder*)> fn)
    {
        auto alive = this->alive;
        Finder** ownerRef = &owner;
        postToMain([alive, ownerRef, fn = std::move(fn)] {
            if (*alive && *ownerRef)
                fn(*ownerRef);
        });
    }

    void walk()
    {
        const QString needle = pattern.toCaseFolded();
        struct Dir {
            GPtr<GFile> file;
            QByteArray rel;
            QString relDisplay;
        };
        std::vector<Dir> pending;
        pending.push_back({GPtr<GFile>(G_FILE(g_object_ref(root.gfile()))), QByteArray(), QString()});
        std::vector<Entry> batch;
        quint64 scanned = 0;
        size_t found = 0;
        quint32 device = 0;
        bool haveDevice = false;
        QElapsedTimer since;
        since.start();

        auto flush = [&] {
            if (batch.empty())
                return;
            auto entries = std::move(batch);
            batch.clear();
            const quint64 gen = generation;
            post([entries = std::move(entries), gen](Finder* f) mutable { Q_EMIT f->matches(gen, std::move(entries)); });
            since.restart();
        };

        while (!pending.empty() && !cancelled() && found < kLimit) {
            Dir dir = std::move(pending.back());
            pending.pop_back();
            GError* error = nullptr;
            GPtr<GFileEnumerator> en(g_file_enumerate_children(dir.file.get(), kAttributes,
                                                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                               cancellable.get(), &error));
            if (error)
                g_error_free(error);
            if (!en)
                continue;
            for (;;) {
                GFileInfo* info = nullptr;
                GFile* child = nullptr;
                if (!g_file_enumerator_iterate(en.get(), &info, &child, cancellable.get(), nullptr) || !info)
                    break;
                ++scanned;
                const QByteArray name(g_file_info_get_name(info));
                const char* dn = g_file_info_get_display_name(info);
                const QString display = dn ? QString::fromUtf8(dn) : QString::fromUtf8(name);
                const QByteArray rel = dir.rel.isEmpty() ? name : dir.rel + '/' + name;
                const QString relDisplay = dir.relDisplay.isEmpty() ? display : dir.relDisplay + QLatin1Char('/') + display;
                const GFileType type = g_file_info_get_file_type(info);
                if (type == G_FILE_TYPE_DIRECTORY) {
                    const bool has = g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_DEVICE);
                    const quint32 dev = has ? g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_DEVICE) : 0;
                    if (!haveDevice && has) {
                        haveDevice = true;
                        device = dev;
                    }
                    if (!has || !haveDevice || dev == device)
                        pending.push_back({GPtr<GFile>(G_FILE(g_object_ref(child))), rel, relDisplay});
                }
                if (needle.isEmpty() || Finder::nameMatches(display.toCaseFolded(), needle)) {
                    Entry e;
                    e.name = rel;
                    e.display = relDisplay;
                    e.type = type == G_FILE_TYPE_DIRECTORY ? EntryType::Directory
                        : type == G_FILE_TYPE_REGULAR    ? EntryType::File
                        : type == G_FILE_TYPE_SYMBOLIC_LINK ? EntryType::Symlink
                                                            : EntryType::Special;
                    e.hidden = g_file_info_get_is_hidden(info);
                    e.symlink = g_file_info_get_is_symlink(info);
                    computeSortKey(e);
                    batch.push_back(std::move(e));
                    ++found;
                }
                if (batch.size() >= kBatch || (!batch.empty() && since.elapsed() >= kBatchMs))
                    flush();
                if (cancelled() || found >= kLimit)
                    break;
            }
        }
        if (cancelled())
            return;
        flush();
        const quint64 gen = generation;
        post([gen, scanned](Finder* f) { Q_EMIT f->finished(gen, scanned); });
    }
};

Finder::Finder(QObject* parent)
    : QObject(parent)
{
}

Finder::~Finder()
{
    cancel();
}

bool Finder::nameMatches(const QString& hay, const QString& needle)
{
    qsizetype at = 0;
    for (const QChar c : needle) {
        at = hay.indexOf(c, at);
        if (at < 0)
            return false;
        ++at;
    }
    return true;
}

void Finder::start(const Location& root, const QString& pattern, quint64 generation)
{
    cancel();
    if (!root.isValid())
        return;
    run_ = std::make_shared<Run>();
    run_->owner = this;
    run_->root = root;
    run_->pattern = pattern;
    run_->generation = generation;
    std::shared_ptr<Run> run = run_;
    run_->thread = std::thread([run] { run->walk(); });
}

void Finder::cancel()
{
    if (!run_)
        return;
    g_cancellable_cancel(run_->cancellable.get());
    *run_->alive = false;
    run_->owner = nullptr;
    if (run_->thread.joinable())
        run_->thread.join();
    run_.reset();
}

} // namespace core
