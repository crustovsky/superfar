#include "core/jobs/extract.h"

#include "core/gobject.h"

namespace core {

ExtractJob::ExtractJob(Location archive, std::vector<QByteArray> members, Location destination, int stripComponents,
                       QObject* parent)
    : Job(JobKind::Extract, parent)
    , archive_(std::move(archive))
    , members_(std::move(members))
    , dest_(std::move(destination))
    , strip_(stripComponents)
{
}

void ExtractJob::run()
{
    JobProgress p;
    p.phase = JobProgress::Phase::Running;
    p.itemsTotal = members_.size();
    p.from = archive_.displayPath();
    p.to = dest_.displayPath();
    p.currentName = archive_.displayName();
    report(p, true);

    const QByteArray archivePath = archive_.path();
    const QByteArray destPath = dest_.path();
    if (archivePath.isEmpty() || destPath.isEmpty()) {
        logFail(archive_.displayPath(), QStringLiteral("archives extract between local folders only"));
        p.phase = JobProgress::Phase::Done;
        report(p, true);
        return;
    }
    std::vector<QByteArray> args = {"bsdtar", "-xf", archivePath, "-C", destPath};
    if (strip_ > 0)
        args.push_back("--strip-components=" + QByteArray::number(strip_));
    args.push_back("--");
    for (const QByteArray& m : members_)
        args.push_back(m);
    std::vector<const char*> argv;
    for (const QByteArray& a : args)
        argv.push_back(a.constData());
    argv.push_back(nullptr);

    GPtr<GSubprocessLauncher> launcher(g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDERR_PIPE));
    g_subprocess_launcher_setenv(launcher.get(), "LC_ALL", "C", TRUE);
    GError* error = nullptr;
    GPtr<GSubprocess> proc(g_subprocess_launcher_spawnv(launcher.get(), argv.data(), &error));
    if (!proc) {
        GErrorPtr err(error);
        logFail(archive_.displayPath(), errorMessage(err.get()));
    } else {
        GCancellable* cancel = beginItem();
        GBytes* errOut = nullptr;
        const gboolean ok = g_subprocess_communicate(proc.get(), nullptr, cancel, nullptr, &errOut, &error);
        GErrorPtr err(error);
        if (isAborted()) {
            g_subprocess_force_exit(proc.get());
            logSkip(archive_.displayPath());
        } else if (!ok || !g_subprocess_get_successful(proc.get())) {
            gsize n = 0;
            const char* text = errOut ? static_cast<const char*>(g_bytes_get_data(errOut, &n)) : nullptr;
            logFail(archive_.displayPath(), text && n ? QString::fromUtf8(text, static_cast<int>(n)).trimmed()
                                                     : errorMessage(err.get()));
        } else {
            p.itemsDone = members_.size();
        }
        if (errOut)
            g_bytes_unref(errOut);
    }
    p.phase = JobProgress::Phase::Done;
    report(p, true);
}

} // namespace core
