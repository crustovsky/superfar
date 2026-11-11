#include "core/jobs/undo.h"

#include "core/fileops.h"
#include "core/gobject.h"

#include <QHash>
#include <QSet>

#include <glib/gstdio.h>
#include <unistd.h>

namespace core {

namespace {

constexpr GFileQueryInfoFlags kNoFollow = G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;
const char* const kStat = "standard::type,standard::size,time::modified,unix::mode,unix::uid,unix::gid";

QString takeMessage(GError* error)
{
    GErrorPtr err(error);
    return err ? errorMessage(err.get()) : QStringLiteral("failed");
}

// The top of the filesystem path is on: the last folder up the way
// that is still on the same device.
QByteArray topDirOf(const QByteArray& path)
{
    GStatBuf st;
    QByteArray dir = path.left(path.lastIndexOf('/'));
    if (dir.isEmpty() || g_lstat(dir.constData(), &st) != 0)
        return {};
    const auto device = st.st_dev;
    while (dir.size() > 1) {
        const QByteArray up = dir.left(std::max<qsizetype>(1, dir.lastIndexOf('/')));
        if (g_lstat(up.constData(), &st) != 0 || st.st_dev != device)
            break;
        dir = up;
    }
    return dir;
}

} // namespace

struct TrashIndex::Data {
    struct Entry {
        qint64 when;
        QByteArray name;  // in files/, and with .trashinfo in info/
        QByteArray trash;
    };
    QHash<QByteArray, std::vector<Entry>> byPath;
    QSet<QByteArray> read;
    QByteArray lastFolder;  // items come folder by folder: one walk up for each

    // topDir is empty for the home trash, whose paths are absolute.
    void readTrash(const QByteArray& trash, const QByteArray& topDir)
    {
        if (read.contains(trash))
            return;
        read.insert(trash);
        GDir* dir = g_dir_open((trash + "/info").constData(), 0, nullptr);
        if (!dir)
            return;
        while (const char* name = g_dir_read_name(dir)) {
            const QByteArray file(name);
            if (!file.endsWith(".trashinfo"))
                continue;
            GKeyFile* keys = g_key_file_new();
            if (g_key_file_load_from_file(keys, (trash + "/info/" + file).constData(), G_KEY_FILE_NONE, nullptr)) {
                GStr raw(g_key_file_get_string(keys, "Trash Info", "Path", nullptr));
                GStr path(raw ? g_uri_unescape_string(raw.get(), nullptr) : nullptr);
                // When: the info file's own time. DeletionDate has
                // seconds, too coarse to tell an item from the one
                // that took its name and was trashed right after.
                GStatBuf st;
                if (path && g_lstat((trash + "/info/" + file).constData(), &st) == 0) {
                    QByteArray original(path.get());
                    if (!original.startsWith('/'))
                        original = topDir + '/' + original;
                    const qint64 when = static_cast<qint64>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
                    byPath[original].push_back({when, file.chopped(10), trash});
                }
            }
            g_key_file_free(keys);
        }
        g_dir_close(dir);
    }
};

TrashIndex::TrashIndex()
    : d_(std::make_unique<Data>())
{
}

TrashIndex::~TrashIndex() = default;

void TrashIndex::prime(const Location& original)
{
    const QByteArray path = original.path();
    const QByteArray folder = path.left(path.lastIndexOf('/'));
    if (path.isEmpty() || (folder == d_->lastFolder && !d_->read.isEmpty()))
        return;
    d_->lastFolder = folder;
    d_->readTrash(QByteArray(g_get_user_data_dir()) + "/Trash", {});
    if (const QByteArray top = topDirOf(path); !top.isEmpty()) {
        const QByteArray uid = QByteArray::number(static_cast<qulonglong>(getuid()));
        d_->readTrash(top + "/.Trash/" + uid, top);
        d_->readTrash(top + "/.Trash-" + uid, top);
    }
}

bool TrashIndex::find(const Location& original, qint64 when, Location* file, Location* info)
{
    const QByteArray path = original.path();
    if (path.isEmpty())
        return false;
    prime(original);
    auto it = d_->byPath.find(path);
    if (it == d_->byPath.end())
        return false;
    // The one trashed at that moment: the same path may have been
    // trashed before, and since. `when` was taken right after the
    // call, so the info file is a little older, never newer.
    auto best = it->end();
    for (auto e = it->begin(); e != it->end(); ++e)
        if (e->when >= when - 10000 && e->when <= when && (best == it->end() || e->when > best->when))
            best = e;
    if (best == it->end())
        return false;
    *file = Location::fromPath(best->trash + "/files/" + best->name);
    *info = Location::fromPath(best->trash + "/info/" + best->name + ".trashinfo");
    it->erase(best);
    return true;
}

struct UndoJob::Impl {
    UndoJob& job;
    JobProgress p;
    TrashIndex trash;
    struct Emptied {
        Location folder;   // to remove once what was in it has gone back
        Location remade;   // the folder made again in its stead, if any
    };
    std::vector<Emptied> emptied;

    // The item as it is now; null when it is gone.
    GPtr<GFileInfo> stat(const Location& loc)
    {
        return GPtr<GFileInfo>(g_file_query_info(loc.gfile(), kStat, kNoFollow, nullptr, nullptr));
    }

    // Whether the operation's result is still what the operation left.
    bool intact(const UndoItem& item, const Location& loc)
    {
        GPtr<GFileInfo> info = stat(loc);
        if (!info) {
            job.logFail(loc.displayPath(), tr("not there any more"));
            return false;
        }
        if (item.time < 0)
            return true;
        const bool file = g_file_info_get_file_type(info.get()) == G_FILE_TYPE_REGULAR;
        const auto time = static_cast<qint64>(g_file_info_get_attribute_uint64(info.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED));
        if (time != item.time || (file && static_cast<quint64>(g_file_info_get_size(info.get())) != item.size)) {
            job.logSkip(loc.displayPath(), tr("changed since"));
            return false;
        }
        return true;
    }

    // Makes room at target for what is coming back. False: leave it.
    bool clear(const Location& coming, Location& target, GCancellable* c)
    {
        GPtr<GFileInfo> there = stat(target);
        if (!there)
            return true;
        Conflict q;
        q.name = target.displayName();
        q.sourcePath = coming.displayPath();
        q.targetPath = target.displayPath();
        q.targetIsDir = g_file_info_get_file_type(there.get()) == G_FILE_TYPE_DIRECTORY;
        q.targetSize = static_cast<quint64>(g_file_info_get_size(there.get()));
        q.targetModified = static_cast<qint64>(g_file_info_get_attribute_uint64(there.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED));
        if (GPtr<GFileInfo> from = stat(coming)) {
            q.sourceIsDir = g_file_info_get_file_type(from.get()) == G_FILE_TYPE_DIRECTORY;
            q.sourceSize = static_cast<quint64>(g_file_info_get_size(from.get()));
            q.sourceModified = static_cast<qint64>(g_file_info_get_attribute_uint64(from.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED));
        }
        q.cannotUndo = !canTrash(target);
        ConflictAction action = job.askConflict(q);
        if (action == ConflictAction::OverwriteIfNewer)
            action = q.sourceModified > q.targetModified ? ConflictAction::Overwrite : ConflictAction::Skip;
        switch (action) {
        case ConflictAction::Rename:
            target = uniqueSibling(target);
            return true;
        case ConflictAction::Overwrite:
            // To the trash or not at all: undo deletes nothing.
            if (job.keepReplaced(target, c, true))
                return true;
            job.logFail(target.displayPath(), tr("in the way, and there is no trash to move it to"));
            return false;
        default:
            job.logSkip(target.displayPath(), tr("something else has this name now"));
            return false;
        }
    }

    void created(const UndoItem& item, GCancellable* c)
    {
        if (!intact(item, item.at))
            return;
        for (;;) {
            GError* error = nullptr;
            if (g_file_trash(item.at.gfile(), c, &error)) {
                job.record(UndoItem::trashed(item.at));
                return;
            }
            GErrorPtr err(error);
            // No trash there (a network share, another volume): the
            // only way to take the item back is to delete it, which
            // undo does not do on its own. It asks, as F8 does when
            // the trash refuses; unattended, the item stays.
            Failure f;
            f.name = item.at.displayName();
            f.path = item.at.displayPath();
            f.message = errorMessage(err.get());
            f.offerDelete = g_error_matches(err.get(), G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
            const ErrorAction answer = isCancelled(err.get()) ? ErrorAction::Skip : job.askError(f);
            if (answer == ErrorAction::Retry) {
                c = job.beginItem();
                continue;
            }
            if (answer == ErrorAction::Abort)
                job.abort();
            else if (answer == ErrorAction::DeletePermanently && f.offerDelete && deleteTree(item.at))
                return;  // gone for good: nothing a redo could bring back
            else
                job.logFail(f.path, f.message);
            return;
        }
    }

    // loc and everything under it, children first. False when something
    // could not be deleted; that is logged.
    bool deleteTree(const Location& loc)
    {
        GPtr<GFileInfo> info(g_file_query_info(loc.gfile(), "standard::type", kNoFollow, nullptr, nullptr));
        bool whole = true;
        if (info && g_file_info_get_file_type(info.get()) == G_FILE_TYPE_DIRECTORY) {
            GPtr<GFileEnumerator> en(g_file_enumerate_children(loc.gfile(), "standard::name", kNoFollow, nullptr, nullptr));
            while (en && !job.isAborted()) {
                GPtr<GFileInfo> child(g_file_enumerator_next_file(en.get(), nullptr, nullptr));
                if (!child)
                    break;
                whole = deleteTree(loc.child(g_file_info_get_name(child.get()))) && whole;
            }
        }
        if (!whole || job.isAborted())
            return false;
        GError* error = nullptr;
        if (g_file_delete(loc.gfile(), nullptr, &error))
            return true;
        job.logFail(loc.displayPath(), takeMessage(error));
        return false;
    }

    void trashed(const UndoItem& item, GCancellable* c)
    {
        Location file, info;
        if (!trash.find(item.at, item.time, &file, &info)) {
            job.logFail(item.at.displayPath(), tr("not found in the trash"));
            return;
        }
        Location target = item.at;
        if (!clear(file, target, c))
            return;
        GError* error = nullptr;
        if (!g_file_move(file.gfile(), target.gfile(), G_FILE_COPY_NOFOLLOW_SYMLINKS, c, nullptr, nullptr, &error)) {
            job.logFail(item.at.displayPath(), takeMessage(error));
            return;
        }
        g_file_delete(info.gfile(), nullptr, nullptr);
        job.record(UndoItem::created(target));
    }

    void moved(const UndoItem& item, GCancellable* c)
    {
        if (!intact(item, item.at))
            return;
        Location target = item.other;
        if (!clear(item.at, target, c))
            return;
        // Its folder may have gone with the move.
        if (const Location parent = target.parent(); parent.isValid())
            g_file_make_directory_with_parents(parent.gfile(), nullptr, nullptr);
        GError* error = nullptr;
        if (g_file_move(item.at.gfile(), target.gfile(), G_FILE_COPY_NOFOLLOW_SYMLINKS, c, nullptr, nullptr, &error))
            job.record(UndoItem::moved(item.at, target));
        else
            job.logFail(item.at.displayPath(), takeMessage(error));
    }

    void folder(const UndoItem& item)
    {
        // The folder the move removed comes back now, ahead of what was
        // in it; the one it made goes once that has left.
        bool remade = false;
        if (item.other.isValid())
            remade = g_file_make_directory_with_parents(item.other.gfile(), nullptr, nullptr);
        if (item.at.isValid())
            emptied.push_back({item.at, remade ? item.other : Location()});
        else if (remade)
            job.record(UndoItem::folder(item.other, Location()));
    }

    void removeEmptied()
    {
        // Innermost first: they were noted outermost first.
        for (auto e = emptied.rbegin(); e != emptied.rend(); ++e) {
            if (e->remade.isValid())
                g_file_copy_attributes(e->folder.gfile(), e->remade.gfile(), G_FILE_COPY_NOFOLLOW_SYMLINKS, nullptr, nullptr);
            // Refused while something is left inside, as it should be.
            const bool removed = g_file_delete(e->folder.gfile(), nullptr, nullptr);
            if (removed || e->remade.isValid())
                job.record(UndoItem::folder(e->remade, removed ? e->folder : Location()));
        }
    }

    void attributes(const UndoItem& item)
    {
        const Location loc = item.at.descendant(item.below);
        GPtr<GFileInfo> info = stat(loc);
        if (!info) {
            job.logFail(loc.displayPath(), tr("not there any more"));
            return;
        }
        const quint32 mode = g_file_info_get_attribute_uint32(info.get(), G_FILE_ATTRIBUTE_UNIX_MODE) & 07777;
        const quint32 uid = g_file_info_get_attribute_uint32(info.get(), G_FILE_ATTRIBUTE_UNIX_UID);
        const quint32 gid = g_file_info_get_attribute_uint32(info.get(), G_FILE_ATTRIBUTE_UNIX_GID);
        const bool link = g_file_info_get_file_type(info.get()) == G_FILE_TYPE_SYMBOLIC_LINK;
        if ((!link && mode != item.mode[1]) || uid != item.uid[1] || gid != item.gid[1]) {
            job.logSkip(loc.displayPath(), tr("changed since"));
            return;
        }
        UndoItem back = item;
        std::swap(back.mode[0], back.mode[1]);
        std::swap(back.uid[0], back.uid[1]);
        std::swap(back.gid[0], back.gid[1]);
        GError* error = nullptr;
        if (uid != item.uid[0] || gid != item.gid[0]) {
            GPtr<GFileInfo> owner(g_file_info_new());
            g_file_info_set_attribute_uint32(owner.get(), G_FILE_ATTRIBUTE_UNIX_UID, item.uid[0]);
            g_file_info_set_attribute_uint32(owner.get(), G_FILE_ATTRIBUTE_UNIX_GID, item.gid[0]);
            if (!g_file_set_attributes_from_info(loc.gfile(), owner.get(), kNoFollow, nullptr, &error)) {
                job.logFail(loc.displayPath(), takeMessage(error));
                return;
            }
        }
        // After the owner: a change of owner drops setuid and setgid.
        if (!link && !g_file_set_attribute_uint32(loc.gfile(), G_FILE_ATTRIBUTE_UNIX_MODE, item.mode[0], kNoFollow,
                                                  nullptr, &error)) {
            job.logFail(loc.displayPath(), takeMessage(error));
            back.mode[1] = mode;  // the owner did go back
        }
        job.record(std::move(back));
    }
};

UndoJob::UndoJob(UndoEntry entry, bool redo, QObject* parent)
    : Job(redo ? JobKind::Redo : JobKind::Undo, parent)
    , entry_(std::move(entry))
    , left_(entry_.items.size())
{
    setSubject(entry_.what);
}

UndoEntry UndoJob::remaining() const
{
    UndoEntry rest{entry_.what, {}};
    rest.items.assign(entry_.items.begin(), entry_.items.begin() + static_cast<qsizetype>(left_));
    return rest;
}

void UndoJob::run()
{
    Impl impl{*this, {}, {}, {}};
    JobProgress& p = impl.p;
    p.phase = JobProgress::Phase::Running;
    p.itemsTotal = entry_.items.size();
    report(p, true);
    for (const UndoItem& item : entry_.items)
        if (item.kind == UndoItem::Kind::Trashed)
            impl.trash.prime(item.at);

    while (left_ > 0 && waitIfPaused()) {
        const UndoItem& item = entry_.items[left_ - 1];
        p.currentName = item.at.isValid() ? item.at.displayName() : item.other.displayName();
        p.from = item.at.isValid() ? item.at.displayPath() : QString();
        p.to = item.other.isValid() ? item.other.displayPath() : QString();
        report(p);
        GCancellable* c = beginItem();
        switch (item.kind) {
        case UndoItem::Kind::Created: impl.created(item, c); break;
        case UndoItem::Kind::Trashed: impl.trashed(item, c); break;
        case UndoItem::Kind::Moved: impl.moved(item, c); break;
        case UndoItem::Kind::Folder: impl.folder(item); break;
        case UndoItem::Kind::Attributes: impl.attributes(item); break;
        }
        --left_;
        ++p.itemsDone;
    }
    impl.removeEmptied();
    p.phase = JobProgress::Phase::Done;
    p.currentName.clear();
    report(p, true);
}

} // namespace core
