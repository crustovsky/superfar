#include "core/bookmarks.h"

#include "core/gobject.h"

namespace core {

namespace {

void onFileChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer data)
{
    QMetaObject::invokeMethod(static_cast<Bookmarks*>(data), "reloadLater", Qt::QueuedConnection);
}

} // namespace

Bookmarks::Bookmarks(const QByteArray& path, QObject* parent)
    : QObject(parent)
{
    if (path.isEmpty()) {
        GStr p(g_build_filename(g_get_user_config_dir(), "gtk-3.0", "bookmarks", nullptr));
        file_ = Location::fromPath(QByteArray(p.get()));
    } else {
        file_ = Location::fromPath(path);
    }
    reload();
    monitor_ = g_file_monitor_file(file_.gfile(), G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (monitor_)
        g_signal_connect(monitor_, "changed", G_CALLBACK(onFileChanged), this);
}

Bookmarks::~Bookmarks()
{
    if (monitor_) {
        g_file_monitor_cancel(monitor_);
        g_object_unref(monitor_);
    }
}

std::vector<Bookmark> Bookmarks::parse(const QByteArray& content)
{
    std::vector<Bookmark> out;
    for (const QByteArray& raw : content.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const int space = line.indexOf(' ');
        const QByteArray uri = space < 0 ? line : line.left(space);
        if (!uri.contains("://"))
            continue;
        Bookmark b;
        b.location = Location::fromUri(QString::fromUtf8(uri));
        if (!b.location.isValid())
            continue;
        b.name = space < 0 ? b.location.displayName() : QString::fromUtf8(line.mid(space + 1).trimmed());
        if (b.name.isEmpty())
            b.name = b.location.displayPath();
        out.push_back(std::move(b));
    }
    return out;
}

bool Bookmarks::contains(const Location& dir) const
{
    for (const Bookmark& b : list_)
        if (b.location == dir)
            return true;
    return false;
}

bool Bookmarks::toggle(const Location& dir)
{
    if (!dir.isValid())
        return false;
    const bool had = contains(dir);
    QByteArray content;
    for (const Bookmark& b : list_) {
        if (had && b.location == dir)
            continue;
        content += b.location.uri().toUtf8();
        if (b.name != b.location.displayName())
            content += ' ' + b.name.toUtf8();
        content += '\n';
    }
    if (!had)
        content += dir.uri().toUtf8() + '\n';
    GStr path(g_file_get_path(file_.gfile()));
    if (path) {
        GStr parent(g_path_get_dirname(path.get()));
        g_mkdir_with_parents(parent.get(), 0700);
        g_file_set_contents(path.get(), content.constData(), content.size(), nullptr);
    }
    reload();
    Q_EMIT changed();
    return !had;
}

void Bookmarks::reload()
{
    gchar* data = nullptr;
    gsize len = 0;
    GStr path(g_file_get_path(file_.gfile()));
    if (path && g_file_get_contents(path.get(), &data, &len, nullptr)) {
        list_ = parse(QByteArray(data, static_cast<qsizetype>(len)));
        g_free(data);
    } else {
        list_.clear();
    }
}

} // namespace core
