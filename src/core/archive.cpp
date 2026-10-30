#include "core/archive.h"

#include "core/gobject.h"

#include <QDate>
#include <QDateTime>
#include <QPointer>

namespace core {

bool isArchiveName(const QByteArray& name)
{
    const QByteArray lower = name.toLower();
    for (const char* ext : {".zip", ".tar", ".tgz", ".tbz2", ".txz", ".tzst", ".tar.gz", ".tar.bz2", ".tar.xz",
                            ".tar.zst", ".tar.lz4", ".7z", ".rar", ".iso", ".cpio", ".ar", ".deb", ".jar", ".war",
                            ".xpi", ".cbz", ".cbr", ".apk", ".whl", ".crate", ".pkg.tar.zst", ".rpm"})
        if (lower.endsWith(ext))
            return true;
    return false;
}

namespace {

// Skips n whitespace-separated fields; returns the offset of the next.
int skipFields(const QByteArray& line, int n)
{
    int i = 0;
    for (int f = 0; f < n; ++f) {
        while (i < line.size() && line[i] == ' ')
            ++i;
        while (i < line.size() && line[i] != ' ')
            ++i;
    }
    while (i < line.size() && line[i] == ' ')
        ++i;
    return i;
}

QByteArray field(const QByteArray& line, int n)
{
    const int start = skipFields(line, n);
    int end = start;
    while (end < line.size() && line[end] != ' ')
        ++end;
    return line.mid(start, end - start);
}

int monthIndex(const QByteArray& m)
{
    static const char* const names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; ++i)
        if (m == names[i])
            return i + 1;
    return 0;
}

} // namespace

std::vector<ArchiveEntry> parseArchiveListing(const QByteArray& output, int currentYear)
{
    std::vector<ArchiveEntry> out;
    for (const QByteArray& raw : output.split('\n')) {
        QByteArray line = raw;
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.size() < 10)
            continue;
        const QByteArray mode = field(line, 0);
        if (mode.size() != 10)
            continue;
        const int month = monthIndex(field(line, 5));
        if (month == 0)
            continue;
        ArchiveEntry e;
        e.isDir = mode[0] == 'd';
        e.symlink = mode[0] == 'l';
        e.size = field(line, 4).toULongLong();
        const int day = field(line, 6).toInt();
        const QByteArray clock = field(line, 7);
        QDateTime when;
        if (clock.contains(':')) {
            when = QDateTime(QDate(currentYear, month, day), QTime::fromString(QString::fromLatin1(clock), QStringLiteral("HH:mm")));
            if (when.isValid() && when > QDateTime::currentDateTime().addDays(1))
                when = when.addYears(-1);  // last year's date shown with a clock
        } else {
            when = QDateTime(QDate(clock.toInt(), month, day), QTime(0, 0));
        }
        e.modified = when.isValid() ? when.toSecsSinceEpoch() : 0;
        QByteArray path = line.mid(skipFields(line, 8));
        if (e.symlink) {
            const int arrow = path.indexOf(" -> ");
            if (arrow >= 0)
                path = path.left(arrow);
        }
        while (path.endsWith('/'))
            path.chop(1);
        if (path.isEmpty() || path == ".")
            continue;
        if (path.startsWith("./"))
            path = path.mid(2);
        e.path = path;
        out.push_back(std::move(e));
    }
    return out;
}

struct ArchiveLister::Call {
    QPointer<ArchiveLister> owner;
    quint64 generation = 0;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(data));
        GError* error = nullptr;
        GBytes* out = nullptr;
        GBytes* err = nullptr;
        g_subprocess_communicate_finish(G_SUBPROCESS(source), result, &out, &err, &error);
        GErrorPtr gerr(error);
        GPtr<GSubprocess> proc(G_SUBPROCESS(g_object_ref(source)));
        std::unique_ptr<GBytes, void (*)(GBytes*)> outGuard(out, [](GBytes* b) { if (b) g_bytes_unref(b); });
        std::unique_ptr<GBytes, void (*)(GBytes*)> errGuard(err, [](GBytes* b) { if (b) g_bytes_unref(b); });
        if (!call->owner || isCancelled(gerr.get()))
            return;
        if (gerr || !g_subprocess_get_successful(proc.get())) {
            gsize n = 0;
            const char* text = err ? static_cast<const char*>(g_bytes_get_data(err, &n)) : nullptr;
            QString message = text && n ? QString::fromUtf8(text, static_cast<int>(n)).trimmed() : errorMessage(gerr.get());
            if (message.isEmpty())
                message = QStringLiteral("bsdtar failed");
            Q_EMIT call->owner->failed(call->generation, message);
            return;
        }
        gsize n = 0;
        const char* text = out ? static_cast<const char*>(g_bytes_get_data(out, &n)) : nullptr;
        Q_EMIT call->owner->listed(call->generation,
                                   parseArchiveListing(QByteArray(text, static_cast<qsizetype>(n)), QDate::currentDate().year()));
    }
};

ArchiveLister::ArchiveLister(QObject* parent)
    : QObject(parent)
{
}

ArchiveLister::~ArchiveLister()
{
    cancel();
}

void ArchiveLister::start(const Location& archive, quint64 generation)
{
    cancel();
    const QByteArray path = archive.path();
    if (path.isEmpty()) {
        Q_EMIT failed(generation, QStringLiteral("archives open from local files only"));
        return;
    }
    GPtr<GSubprocessLauncher> launcher(g_subprocess_launcher_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE)));
    g_subprocess_launcher_setenv(launcher.get(), "LC_ALL", "C", TRUE);
    const char* argv[] = {"bsdtar", "-tvf", path.constData(), nullptr};
    GError* error = nullptr;
    GPtr<GSubprocess> proc(g_subprocess_launcher_spawnv(launcher.get(), argv, &error));
    if (!proc) {
        GErrorPtr err(error);
        Q_EMIT failed(generation, errorMessage(err.get()));
        return;
    }
    cancellable_ = g_cancellable_new();
    auto* call = new Call{this, generation};
    g_subprocess_communicate_async(proc.get(), nullptr, cancellable_, &Call::onDone, call);
}

void ArchiveLister::cancel()
{
    if (!cancellable_)
        return;
    g_cancellable_cancel(cancellable_);
    g_object_unref(cancellable_);
    cancellable_ = nullptr;
}

} // namespace core
