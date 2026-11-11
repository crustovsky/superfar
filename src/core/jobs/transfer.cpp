#include "core/jobs/transfer.h"

#include "core/fileops.h"
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
    // Inside a folder this copy made nothing is recorded: taking the
    // folder back takes its contents with it.
    int insideCreated = 0;

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


    // Decides what to do about an existing target. Returns false to skip.
    bool resolveConflict(const Location& src, bool srcIsDir, Location& dst, GFileInfo* dstInfo,
                         bool& overwrite, bool& warned)
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
        c.cannotUndo = !canTrash(dst);
        bool asked = false;
        const ConflictAction action = job.askConflict(c, &asked);
        warned = asked && c.cannotUndo;
        switch (action) {
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
            dst = uniqueSibling(dst);
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
        bool forGood = false;
        for (;;) {
            GCancellable* c = job.beginItem();
            auto flags = static_cast<GFileCopyFlags>(G_FILE_COPY_NOFOLLOW_SYMLINKS);
            GPtr<GFileInfo> existing(g_file_query_info(dst.gfile(), kConflictAttrs,
                                                       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, nullptr));
            if (existing) {
                bool overwrite = false, warned = false;
                if (!resolveConflict(src, false, dst, existing.get(), overwrite, warned))
                    return finishItem(size, true);
                // What is replaced goes to the trash, so undo can bring
                // it back; without a trash it is overwritten in place.
                if (overwrite && !job.keepReplaced(dst, c, warned)) {
                    flags = static_cast<GFileCopyFlags>(flags | G_FILE_COPY_OVERWRITE);
                    forGood = true;  // nothing undo could give back
                }
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
            if (ok) {
                if (forGood)
                    ;
                else if (job.move_)
                    job.record(UndoItem::moved(src, dst));
                else if (insideCreated == 0)
                    job.record(UndoItem::created(dst));
                return finishItem(size, true);
            }
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
            bool overwrite = false, warned = false;
            if (!resolveConflict(src, true, dst, existing.get(), overwrite, warned))
                return finishItem(0, true);
            if (overwrite && !job.keepReplaced(dst, c, warned))
                g_file_delete(dst.gfile(), c, nullptr);
            existing.reset();  // out of the way, or renamed: the new name does not exist
        }
        p.currentName = dst.displayName();
        p.from = src.displayPath();
        p.to = dst.displayPath();
        job.report(p, true);

        bool made = false;
        if (!existing) {
            GError* error = nullptr;
            if (job.move_) {
                // A rename moves the whole tree at once when GIO can do it.
                if (g_file_move(src.gfile(), dst.gfile(), G_FILE_COPY_NOFOLLOW_SYMLINKS, c,
                                nullptr, nullptr, &error)) {
                    job.record(UndoItem::moved(src, dst));
                    return finishItem(0, true);
                }
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
            } else {
                made = true;
            }
        }
        finishItem(0, true);

        // What this folder's part comes to, on every way out, an abort
        // included. A copy that made the folder records the folder and
        // nothing inside it. A move records each file where it went,
        // and here the folder it made and the one it emptied and
        // removed, so undo can put the tree back as it was.
        struct Note {
            Impl& self;
            const Location& src;
            const Location& dst;
            bool made;
            bool removed = false;
            ~Note()
            {
                if (self.job.move_) {
                    if (made || removed)
                        self.job.record(UndoItem::folder(made ? dst : Location(), removed ? src : Location()));
                } else if (made && --self.insideCreated == 0) {
                    self.job.record(UndoItem::created(dst));
                }
            }
        } note{*this, src, dst, made};
        if (made && !job.move_)
            ++insideCreated;

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
            note.removed = g_file_delete(src.gfile(), nullptr, nullptr);  // fails when something was skipped
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
    Impl impl{*this, {}, 0, 0};
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
        // Into itself (or below itself) would copy the copies forever;
        // onto itself would overwrite the file with its own truncation.
        if (!dest_.relativePathFrom(src).isNull()) {
            if (!impl.handleError(src, tr("cannot copy a folder into itself")))
                break;
            continue;
        }
        if (dst == src) {
            if (!impl.handleError(src, tr("source and target are the same")))
                break;
            continue;
        }
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
