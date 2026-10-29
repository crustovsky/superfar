#include "core/jobs/transfer.h"

#include "core/gobject.h"

namespace core {

namespace {

const char* const kScanAttrs = "standard::name,standard::type,standard::size";
const char* const kConflictAttrs = "standard::type,standard::size,time::modified";

bool isDirInfo(GFileInfo* info)
{
    return g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
}

bool sameFilesystem(const Location& a, const Location& b)
{
    GPtr<GFileInfo> ia(g_file_query_info(a.gfile(), G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                         G_FILE_QUERY_INFO_NONE, nullptr, nullptr));
    GPtr<GFileInfo> ib(g_file_query_info(b.gfile(), G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                         G_FILE_QUERY_INFO_NONE, nullptr, nullptr));
    if (!ia || !ib)
        return false;
    const char* fa = g_file_info_get_attribute_string(ia.get(), G_FILE_ATTRIBUTE_ID_FILESYSTEM);
    const char* fb = g_file_info_get_attribute_string(ib.get(), G_FILE_ATTRIBUTE_ID_FILESYSTEM);
    return fa && fb && g_strcmp0(fa, fb) == 0;
}

} // namespace

struct TransferJob::Impl {
    TransferJob& job;
    JobProgress p;
    quint64 currentBase = 0;

    void scanEntry(const Location& loc, bool isDir, quint64 size)
    {
        if (job.isAborted())
            return;
        ++p.itemsTotal;
        if (isDir)
            scanDir(loc);
        else
            p.bytesTotal += size;
        job.report(p);
    }

    void scanDir(const Location& dir)
    {
        GPtr<GFileEnumerator> en(g_file_enumerate_children(dir.gfile(), kScanAttrs,
                                                           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                           nullptr, nullptr));
        if (!en)
            return;
        while (!job.isAborted()) {
            GPtr<GFileInfo> info(g_file_enumerator_next_file(en.get(), nullptr, nullptr));
            if (!info)
                break;
            scanEntry(dir.child(g_file_info_get_name(info.get())), isDirInfo(info.get()),
                      static_cast<quint64>(g_file_info_get_size(info.get())));
        }
    }

    static void onProgress(goffset current, goffset total, gpointer data)
    {
        auto* self = static_cast<Impl*>(data);
        self->p.currentDone = static_cast<quint64>(current);
        self->p.currentTotal = static_cast<quint64>(total);
        self->p.bytesDone = self->currentBase + static_cast<quint64>(current);
        self->job.report(self->p);
        self->job.waitIfPaused();
    }

    Location uniqueName(const Location& dst) const
    {
        const QByteArray name = dst.basename();
        const Location parent = dst.parent();
        int dot = name.lastIndexOf('.');
        if (dot <= 0)
            dot = name.size();
        const QByteArray stem = name.left(dot);
        const QByteArray ext = name.mid(dot);
        for (int n = 2; n < 10000; ++n) {
            const Location candidate = parent.child(stem + " (" + QByteArray::number(n) + ")" + ext);
            if (!g_file_query_exists(candidate.gfile(), nullptr))
                return candidate;
        }
        return dst;
    }

    // Decides what to do about an existing target. Returns false to skip.
    bool resolveConflict(const Location& src, bool srcIsDir, Location& dst, GFileInfo* dstInfo,
                         bool& overwrite)
    {
        Conflict c;
        c.name = dst.displayName();
        c.sourcePath = src.displayPath();
        c.targetPath = dst.displayPath();
        c.sourceIsDir = srcIsDir;
        c.targetIsDir = isDirInfo(dstInfo);
        c.targetSize = static_cast<quint64>(g_file_info_get_size(dstInfo));
        c.targetModified = static_cast<qint64>(
            g_file_info_get_attribute_uint64(dstInfo, G_FILE_ATTRIBUTE_TIME_MODIFIED));
        GPtr<GFileInfo> si(g_file_query_info(src.gfile(), kConflictAttrs,
                                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr));
        if (si) {
            c.sourceSize = static_cast<quint64>(g_file_info_get_size(si.get()));
            c.sourceModified = static_cast<qint64>(
                g_file_info_get_attribute_uint64(si.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED));
        }
        switch (job.askConflict(c)) {
        case ConflictAction::Overwrite:
            overwrite = true;
            return true;
        case ConflictAction::OverwriteIfNewer:
            if (c.sourceModified > c.targetModified) {
                overwrite = true;
                return true;
            }
            job.logSkip(c.targetPath);
            return false;
        case ConflictAction::Rename:
            dst = uniqueName(dst);
            return true;
        case ConflictAction::Skip:
        case ConflictAction::Ask:
            job.logSkip(c.targetPath);
            return false;
        }
        return false;
    }

    bool transferFile(const Location& src, Location dst, quint64 size)
    {
        for (;;) {
            GCancellable* c = job.beginItem();
            auto flags = static_cast<GFileCopyFlags>(G_FILE_COPY_NOFOLLOW_SYMLINKS);
            GPtr<GFileInfo> existing(g_file_query_info(dst.gfile(), kConflictAttrs,
                                                       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, nullptr));
            if (existing) {
                bool overwrite = false;
                if (!resolveConflict(src, false, dst, existing.get(), overwrite))
                    return finishItem(size, true);
                if (overwrite)
                    flags = static_cast<GFileCopyFlags>(flags | G_FILE_COPY_OVERWRITE);
            }
            p.currentName = dst.displayName();
            p.from = src.displayPath();
            p.to = dst.displayPath();
            p.currentDone = 0;
            p.currentTotal = size;
            currentBase = p.bytesDone;
            job.report(p, true);

            GError* error = nullptr;
            const gboolean ok = job.move_
                ? g_file_move(src.gfile(), dst.gfile(), flags, c, &onProgress, this, &error)
                : g_file_copy(src.gfile(), dst.gfile(), flags, c, &onProgress, this, &error);
            GErrorPtr err(error);
            if (ok)
                return finishItem(size, true);
            if (isCancelled(err.get())) {
                if (job.isAborted())
                    return false;
                job.logSkip(src.displayPath());
                return finishItem(size, true);
            }
            Failure f;
            f.name = src.displayName();
            f.path = src.displayPath();
            f.message = errorMessage(err.get());
            switch (job.askError(f)) {
            case ErrorAction::Retry:
                continue;
            case ErrorAction::Abort:
                job.abort();
                return false;
            default:
                job.logFail(f.path, f.message);
                return finishItem(size, true);
            }
        }
    }

    bool finishItem(quint64 size, bool result)
    {
        ++p.itemsDone;
        p.bytesDone = currentBase + size;
        currentBase = p.bytesDone;
        p.currentDone = p.currentTotal = 0;
        job.report(p);
        return result;
    }

    bool transferDir(const Location& src, Location dst)
    {
        GCancellable* c = job.beginItem();
        GPtr<GFileInfo> existing(g_file_query_info(dst.gfile(), kConflictAttrs,
                                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, nullptr));
        if (existing && !isDirInfo(existing.get())) {
            bool overwrite = false;
            if (!resolveConflict(src, true, dst, existing.get(), overwrite))
                return finishItem(0, true);
            if (overwrite) {
                g_file_delete(dst.gfile(), c, nullptr);
                existing.reset();
            } else {
                existing.reset();  // renamed: the new name does not exist
            }
        }
        p.currentName = dst.displayName();
        p.from = src.displayPath();
        p.to = dst.displayPath();
        job.report(p, true);

        if (!existing) {
            GError* error = nullptr;
            if (job.move_) {
                // A rename moves the whole tree at once when GIO can do it.
                if (g_file_move(src.gfile(), dst.gfile(), G_FILE_COPY_NOFOLLOW_SYMLINKS, c,
                                nullptr, nullptr, &error))
                    return finishItem(0, true);
                GErrorPtr err(error);
                error = nullptr;
                if (isCancelled(err.get()))
                    return !job.isAborted();
                if (!g_error_matches(err.get(), G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE)
                    && !g_error_matches(err.get(), G_IO_ERROR, G_IO_ERROR_WOULD_MERGE)) {
                    if (!handleError(src, errorMessage(err.get())))
                        return false;
                    return finishItem(0, true);
                }
            }
            if (!g_file_make_directory(dst.gfile(), c, &error)) {
                GErrorPtr err(error);
                if (!g_error_matches(err.get(), G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                    if (isCancelled(err.get()))
                        return !job.isAborted();
                    if (!handleError(src, errorMessage(err.get())))
                        return false;
                    return finishItem(0, true);
                }
            }
        }
        finishItem(0, true);

        GError* error = nullptr;
        GPtr<GFileEnumerator> en(g_file_enumerate_children(src.gfile(), kScanAttrs,
                                                           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, &error));
        if (!en) {
            GErrorPtr err(error);
            if (isCancelled(err.get()))
                return !job.isAborted();
            return handleError(src, errorMessage(err.get()));
        }
        for (;;) {
            if (!job.waitIfPaused())
                return false;
            GPtr<GFileInfo> info(g_file_enumerator_next_file(en.get(), nullptr, &error));
            if (!info) {
                if (error) {
                    GErrorPtr err(error);
                    return handleError(src, errorMessage(err.get()));
                }
                break;
            }
            const QByteArray name(g_file_info_get_name(info.get()));
            const bool ok = isDirInfo(info.get())
                ? transferDir(src.child(name), dst.child(name))
                : transferFile(src.child(name), dst.child(name),
                               static_cast<quint64>(g_file_info_get_size(info.get())));
            if (!ok)
                return false;
        }
        en.reset();
        g_file_copy_attributes(src.gfile(), dst.gfile(), G_FILE_COPY_NOFOLLOW_SYMLINKS, nullptr, nullptr);
        if (job.move_)
            g_file_delete(src.gfile(), nullptr, nullptr);  // fails when something was skipped
        return true;
    }

    // Asks about a non-file error; false means abort.
    bool handleError(const Location& where, const QString& message)
    {
        Failure f;
        f.name = where.displayName();
        f.path = where.displayPath();
        f.message = message;
        switch (job.askError(f)) {
        case ErrorAction::Abort:
            job.abort();
            return false;
        default:
            job.logFail(f.path, f.message);
            return true;
        }
    }
};

TransferJob::TransferJob(std::vector<Location> sources, Location destination, bool move, QObject* parent)
    : Job(move ? JobKind::Move : JobKind::Copy, parent)
    , sources_(std::move(sources))
    , dest_(std::move(destination))
    , move_(move)
{
}

void TransferJob::run()
{
    Impl impl{*this, {}, 0};
    JobProgress& p = impl.p;

    // Scan for totals, unless this is a move within one filesystem, where
    // a rename makes the walk pointless.
    const bool rename = move_ && !sources_.empty() && sameFilesystem(sources_.front().parent(), dest_);
    if (!rename) {
        for (const Location& src : sources_) {
            GPtr<GFileInfo> info(g_file_query_info(src.gfile(), kScanAttrs,
                                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr));
            if (info)
                impl.scanEntry(src, isDirInfo(info.get()),
                               static_cast<quint64>(g_file_info_get_size(info.get())));
        }
    } else {
        p.itemsTotal = sources_.size();
    }
    p.phase = JobProgress::Phase::Running;
    report(p, true);

    for (const Location& src : sources_) {
        if (!waitIfPaused())
            break;
        GPtr<GFileInfo> info(g_file_query_info(src.gfile(), kScanAttrs,
                                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr));
        if (!info) {
            if (!impl.handleError(src, tr("not found")))
                break;
            continue;
        }
        const Location dst = dest_.child(src.basename());
        const bool ok = isDirInfo(info.get())
            ? impl.transferDir(src, dst)
            : impl.transferFile(src, dst, static_cast<quint64>(g_file_info_get_size(info.get())));
        if (!ok)
            break;
    }
    p.phase = JobProgress::Phase::Done;
    if (!aborted()) {
        p.itemsDone = p.itemsTotal;
        p.bytesDone = p.bytesTotal;
    }
    p.currentName.clear();
    report(p, true);
}

} // namespace core
