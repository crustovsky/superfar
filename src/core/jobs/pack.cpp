#include "core/jobs/pack.h"

#include "core/archive.h"
#include "core/fileops.h"
#include "core/gobject.h"

#include <QHash>
#include <QStringList>
#include <QTemporaryFile>

namespace core {

namespace {

const char* const kScanAttrs = "standard::name,standard::type,standard::size";
const char* const kConflictAttrs = "standard::type,standard::size,time::modified";

} // namespace

// Items and bytes below loc, and the size of every file under the path
// bsdtar will print for it.
struct PackJob::Scan {
    PackJob& job;
    JobProgress& p;
    QHash<QByteArray, quint64> sizes;

    void entry(const Location& loc, const QByteArray& rel)
    {
        if (job.isAborted())
            return;
        GPtr<GFileInfo> info(g_file_query_info(loc.gfile(), kScanAttrs, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                               nullptr, nullptr));
        if (!info)
            return;
        ++p.itemsTotal;
        if (g_file_info_get_file_type(info.get()) == G_FILE_TYPE_DIRECTORY) {
            GPtr<GFileEnumerator> en(g_file_enumerate_children(loc.gfile(), kScanAttrs,
                                                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr));
            while (en && !job.aborted()) {
                GPtr<GFileInfo> child(g_file_enumerator_next_file(en.get(), nullptr, nullptr));
                if (!child)
                    break;
                const QByteArray name = g_file_info_get_name(child.get());
                entry(loc.child(name), rel + '/' + name);
            }
        } else {
            const auto size = static_cast<quint64>(g_file_info_get_size(info.get()));
            p.bytesTotal += size;
            sizes.insert(rel, size);
        }
        job.report(p);
    }
};

PackJob::PackJob(Location parent, std::vector<QByteArray> names, Location archive, QByteArray options,
                 QObject* parentObject)
    : Job(JobKind::Pack, parentObject)
    , parent_(std::move(parent))
    , names_(std::move(names))
    , archive_(std::move(archive))
    , options_(std::move(options))
{
}

void PackJob::run()
{
    JobProgress p;
    p.from = parent_.displayPath();
    p.to = archive_.displayPath();
    p.currentName = archive_.displayName();
    report(p, true);
    auto finish = [this, &p] {
        p.phase = JobProgress::Phase::Done;
        p.currentName.clear();
        report(p, true);
    };

    const QByteArray parentPath = parent_.path();
    if (parentPath.isEmpty() || archive_.path().isEmpty()) {
        logFail(archive_.displayPath(), tr("archives are made from local folders only"));
        finish();
        return;
    }

    Scan scan{*this, p, {}};
    for (const QByteArray& name : names_)
        scan.entry(parent_.child(name), name);
    if (isAborted()) {
        finish();
        return;
    }

    // bsdtar would silently replace an archive that is there.
    Location target = archive_;
    if (GPtr<GFileInfo> existing(g_file_query_info(target.gfile(), kConflictAttrs, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                   nullptr, nullptr));
        existing) {
        Conflict c;
        c.name = target.displayName();
        c.sourcePath = parent_.displayPath();
        c.targetPath = target.displayPath();
        c.sourceSize = p.bytesTotal;
        c.targetSize = static_cast<quint64>(g_file_info_get_size(existing.get()));
        c.targetModified = static_cast<qint64>(g_file_info_get_attribute_uint64(existing.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED));
        c.targetIsDir = g_file_info_get_file_type(existing.get()) == G_FILE_TYPE_DIRECTORY;
        switch (askConflict(c)) {
        case ConflictAction::Overwrite:
        case ConflictAction::OverwriteIfNewer:  // the archive is being made now
            break;
        case ConflictAction::Rename:
            target = uniqueSibling(target);
            break;
        case ConflictAction::Skip:
        case ConflictAction::Ask:
            logSkip(target.displayPath());
            finish();
            return;
        }
    }

    // The names go through a list file: a pipe would fill while bsdtar
    // waits for its stderr to be read.
    QTemporaryFile list;
    if (!list.open()) {
        logFail(target.displayPath(), tr("cannot write the list of names: %1").arg(list.errorString()));
        finish();
        return;
    }
    for (const QByteArray& name : names_) {
        list.write(name);
        list.write("", 1);
    }
    list.flush();
    const QByteArray listPath = QFile::encodeName(list.fileName());

    p.phase = JobProgress::Phase::Running;
    p.to = target.displayPath();
    report(p, true);
    std::vector<QByteArray> args = {"bsdtar", "-a", "-cvf", target.path()};
    if (!options_.isEmpty()) {
        args.push_back("--options");
        args.push_back(options_);
    }
    for (const QByteArray& a : {QByteArray("-C"), parentPath, QByteArray("--null"), QByteArray("-T"), listPath})
        args.push_back(a);
    std::vector<const char*> argv;
    for (const QByteArray& a : args)
        argv.push_back(a.constData());
    argv.push_back(nullptr);

    GPtr<GSubprocessLauncher> launcher(g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDERR_PIPE));
    g_subprocess_launcher_setenv(launcher.get(), "LC_ALL", "C.UTF-8", TRUE);
    GError* error = nullptr;
    GPtr<GSubprocess> proc(g_subprocess_launcher_spawnv(launcher.get(), argv.data(), &error));
    if (!proc) {
        GErrorPtr err(error);
        logFail(target.displayPath(), bsdtarError(err.get()));
        finish();
        return;
    }

    // One line per member on stderr ("a path"); anything else is a
    // warning or an error, kept for the log.
    GCancellable* cancel = beginItem();
    GPtr<GDataInputStream> lines(g_data_input_stream_new(g_subprocess_get_stderr_pipe(proc.get())));
    QStringList problems;
    while (waitIfPaused()) {
        gsize len = 0;
        GStr line(g_data_input_stream_read_line(lines.get(), &len, cancel, nullptr));
        if (!line)
            break;  // the end, or aborted
        const QByteArray text(line.get(), static_cast<qsizetype>(len));
        if (text.startsWith("a ")) {
            const QByteArray member = text.mid(2);
            ++p.itemsDone;
            p.bytesDone += scan.sizes.value(member);
            p.currentName = QString::fromUtf8(member);
            report(p);
        } else if (!text.trimmed().isEmpty()) {
            problems << QString::fromUtf8(text).trimmed();
        }
    }
    if (isAborted()) {
        g_subprocess_force_exit(proc.get());
        g_subprocess_wait(proc.get(), nullptr, nullptr);
        g_file_delete(target.gfile(), nullptr, nullptr);  // a partial archive is worth nothing
        logSkip(target.displayPath());
    } else {
        g_subprocess_wait(proc.get(), nullptr, nullptr);
        if (!g_subprocess_get_successful(proc.get())) {
            logFail(target.displayPath(), problems.isEmpty() ? tr("bsdtar failed") : problems.join(QLatin1Char('\n')));
        } else {
            p.itemsDone = p.itemsTotal;
            p.bytesDone = p.bytesTotal;
        }
    }
    finish();
}

} // namespace core
