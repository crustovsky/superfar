#include "core/archive.h"

#include "core/gobject.h"

#include <QDate>
#include <QDateTime>
#include <QPointer>
#include <QSet>
#include <algorithm>

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

// bsdtar prints names through safe_fprintf: bytes that are not printable
// in its locale come out as \ooo, backslash and the C escapes as \\ \a
// \b \f \n \r \t \v. It runs under C.UTF-8 here (a plain C locale refuses
// UTF-8 member names outright), so UTF-8 passes through and only control
// characters and bytes from another encoding are escaped. This turns
// them back into bytes.
QByteArray unescapeName(const QByteArray& text)
{
    QByteArray out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        const char next = text[++i];
        if (next >= '0' && next <= '7') {
            int value = 0, digits = 0;
            while (digits < 3 && i < text.size() && text[i] >= '0' && text[i] <= '7') {
                value = value * 8 + (text[i] - '0');
                ++i;
                ++digits;
            }
            --i;
            out += static_cast<char>(value);
            continue;
        }
        switch (next) {
        case 'a': out += '\a'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'v': out += '\v'; break;
        default: out += next; break;  // \\ and anything unknown
        }
    }
    return out;
}

QString bsdtarError(const GError* error)
{
    if (g_error_matches(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        return QStringLiteral("bsdtar is not installed (package libarchive); archives cannot be opened");
    return errorMessage(error);
}

ArchiveSummary summarizeArchive(const std::vector<ArchiveEntry>& entries)
{
    ArchiveSummary s;
    QSet<QByteArray> folders;
    QSet<QByteArray> top;
    for (const ArchiveEntry& e : entries) {
        const int slash = e.path.indexOf('/');
        top.insert(slash < 0 ? e.path : e.path.left(slash));
        for (int i = slash; i >= 0; i = e.path.indexOf('/', i + 1))
            folders.insert(e.path.left(i));  // the folders a path implies
        if (e.isDir) {
            folders.insert(e.path);
        } else {
            ++s.files;
            s.bytes += e.size;
            if (e.executable && slash < 0)
                ++s.executables;
        }
        if (e.symlink)
            ++s.symlinks;
        s.newest = std::max(s.newest, e.modified);
    }
    s.dirs = static_cast<quint64>(folders.size());
    s.topLevel = static_cast<int>(top.size());
    if (top.size() == 1 && folders.contains(*top.cbegin()))
        s.topFolder = *top.cbegin();
    return s;
}

std::vector<ArchiveEntry> parseArchiveListing(const QByteArray& output, int currentYear, ArchiveDropped* dropped)
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
        e.executable = mode[3] == 'x' || mode[3] == 's';
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
        path = unescapeName(path);
        while (path.endsWith('/'))
            path.chop(1);
        // What bsdtar does on extraction, so the listing shows what would
        // land: leading slashes go, "./" goes, and a member that climbs
        // out with ".." is refused, so it is not shown either.
        if (path.startsWith('/') && dropped)
            ++dropped->absolute;
        while (path.startsWith('/'))
            path = path.mid(1);
        while (path.startsWith("./"))
            path = path.mid(2);
        if (path.isEmpty() || path == ".")
            continue;
        if (path == ".." || path.startsWith("../") || path.contains("/../") || path.endsWith("/..")) {
            if (dropped)
                ++dropped->climbing;
            continue;
        }
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
        if (call->owner->process_ == proc.get()) {  // done: nothing to kill later
            g_object_unref(call->owner->process_);
            g_object_unref(call->owner->cancellable_);
            call->owner->process_ = nullptr;
            call->owner->cancellable_ = nullptr;
        }
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
        ArchiveDropped dropped;
        std::vector<ArchiveEntry> entries =
            parseArchiveListing(QByteArray(text, static_cast<qsizetype>(n)), QDate::currentDate().year(), &dropped);
        Q_EMIT call->owner->listed(call->generation, std::move(entries), dropped);
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
    g_subprocess_launcher_setenv(launcher.get(), "LC_ALL", "C.UTF-8", TRUE);
    const char* argv[] = {"bsdtar", "-tvf", path.constData(), nullptr};
    GError* error = nullptr;
    GPtr<GSubprocess> proc(g_subprocess_launcher_spawnv(launcher.get(), argv, &error));
    if (!proc) {
        GErrorPtr err(error);
        Q_EMIT failed(generation, bsdtarError(err.get()));
        return;
    }
    cancellable_ = g_cancellable_new();
    process_ = proc.release();
    auto* call = new Call{this, generation};
    g_subprocess_communicate_async(process_, nullptr, cancellable_, &Call::onDone, call);
}

void ArchiveLister::cancel()
{
    if (!cancellable_)
        return;
    // Cancelling the read does not stop the child (rule 16).
    g_cancellable_cancel(cancellable_);
    g_subprocess_force_exit(process_);
    g_object_unref(process_);
    g_object_unref(cancellable_);
    process_ = nullptr;
    cancellable_ = nullptr;
}

} // namespace core
