#include "core/jobs/remove.h"

#include "core/gobject.h"

namespace core {

namespace {
const char* const kAttrs = "standard::name,standard::type";

bool isDirInfo(GFileInfo* info)
{
    return g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
}
} // namespace

struct RemoveJob::Impl {
    RemoveJob& job;
    JobProgress p;

    void scan(const Location& loc, bool isDir)
    {
        if (job.isAborted())
            return;
        ++p.itemsTotal;
        job.report(p);
        if (!isDir)
            return;
        GPtr<GFileEnumerator> en(g_file_enumerate_children(loc.gfile(), kAttrs,
                                                           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                           nullptr, nullptr));
        if (!en)
            return;
        while (!job.isAborted()) {
            GPtr<GFileInfo> info(g_file_enumerator_next_file(en.get(), nullptr, nullptr));
            if (!info)
                break;
            scan(loc.child(g_file_info_get_name(info.get())), isDirInfo(info.get()));
        }
    }

    bool handleError(const Location& where, const QString& message, bool offerDelete,
                     ErrorAction* chosen)
    {
        Failure f;
        f.name = where.displayName();
        f.path = where.displayPath();
        f.message = message;
        f.offerDelete = offerDelete;
        const ErrorAction a = job.askError(f);
        if (chosen)
            *chosen = a;
        switch (a) {
        case ErrorAction::Abort:
            job.abort();
            return false;
        case ErrorAction::Retry:
        case ErrorAction::DeletePermanently:
            return true;
        default:
            job.logFail(f.path, f.message);
            return true;
        }
    }

    void itemDone()
    {
        ++p.itemsDone;
        job.report(p);
    }

    // Deletes loc and everything under it. False means abort.
    bool deleteTree(const Location& loc, bool isDir)
    {
        if (!job.waitIfPaused())
            return false;
        p.currentName = loc.displayName();
        p.from = loc.displayPath();
        job.report(p);
        if (isDir) {
            // No cancellable here: children replace it, and abort is seen
            // through waitIfPaused at every entry.
            GPtr<GFileEnumerator> en(g_file_enumerate_children(loc.gfile(), kAttrs,
                                                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                               nullptr, nullptr));
            if (en) {
                for (;;) {
                    if (job.isAborted())
                        return false;
                    GPtr<GFileInfo> info(g_file_enumerator_next_file(en.get(), nullptr, nullptr));
                    if (!info)
                        break;
                    if (!deleteTree(loc.child(g_file_info_get_name(info.get())), isDirInfo(info.get())))
                        return false;
                }
            }
        }
        for (;;) {
            GCancellable* c = job.beginItem();
            GError* error = nullptr;
            if (g_file_delete(loc.gfile(), c, &error)) {
                itemDone();
                return true;
            }
            GErrorPtr err(error);
            if (isCancelled(err.get())) {
                if (job.isAborted())
                    return false;
                job.logSkip(loc.displayPath());
                itemDone();
                return true;
            }
            ErrorAction a = ErrorAction::Skip;
            if (!handleError(loc, errorMessage(err.get()), false, &a))
                return false;
            if (a == ErrorAction::Retry)
                continue;
            itemDone();
            return true;
        }
    }

    bool trash(const Location& loc, bool isDir)
    {
        if (!job.waitIfPaused())
            return false;
        p.currentName = loc.displayName();
        p.from = loc.displayPath();
        job.report(p);
        for (;;) {
            GCancellable* c = job.beginItem();
            GError* error = nullptr;
            if (g_file_trash(loc.gfile(), c, &error)) {
                itemDone();
                return true;
            }
            GErrorPtr err(error);
            if (isCancelled(err.get())) {
                if (job.isAborted())
                    return false;
                job.logSkip(loc.displayPath());
                itemDone();
                return true;
            }
            const bool unsupported = g_error_matches(err.get(), G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
            ErrorAction a = ErrorAction::Skip;
            if (!handleError(loc, errorMessage(err.get()), unsupported, &a))
                return false;
            if (a == ErrorAction::Retry)
                continue;
            if (a == ErrorAction::DeletePermanently)
                return deleteTree(loc, isDir);
            itemDone();
            return true;
        }
    }
};

RemoveJob::RemoveJob(std::vector<Location> sources, bool permanent, QObject* parent)
    : Job(permanent ? JobKind::Delete : JobKind::Trash, parent)
    , sources_(std::move(sources))
    , permanent_(permanent)
{
}

void RemoveJob::run()
{
    Impl impl{*this, {}};
    JobProgress& p = impl.p;

    std::vector<bool> isDir;
    for (const Location& src : sources_) {
        GPtr<GFileInfo> info(g_file_query_info(src.gfile(), kAttrs, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                               nullptr, nullptr));
        isDir.push_back(info && isDirInfo(info.get()));
        if (permanent_)
            impl.scan(src, isDir.back());
        else
            ++p.itemsTotal;
    }
    p.phase = JobProgress::Phase::Running;
    report(p, true);

    for (size_t i = 0; i < sources_.size() && !aborted(); ++i) {
        const bool ok = permanent_ ? impl.deleteTree(sources_[i], isDir[i])
                                   : impl.trash(sources_[i], isDir[i]);
        if (!ok)
            break;
    }
    p.phase = JobProgress::Phase::Done;
    if (!aborted())
        p.itemsDone = p.itemsTotal;
    p.currentName.clear();
    report(p, true);
}

} // namespace core
