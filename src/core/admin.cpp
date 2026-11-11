#include "core/admin.h"

#include "core/gobject.h"

#include <QCoreApplication>
#include <QFile>

namespace core::admin {

bool availableIn(const QStringList& dataDirs)
{
    // GVfs installs one file per backend; this is the admin one.
    for (const QString& dir : dataDirs)
        if (QFile::exists(dir + QStringLiteral("/gvfs/mounts/admin.mount")))
            return true;
    return false;
}

bool available()
{
    static const bool there = [] {
        QStringList dirs;
        for (const char* const* dir = g_get_system_data_dirs(); *dir; ++dir)
            dirs << QFile::decodeName(*dir);
        return availableIn(dirs);
    }();
    return there;
}

bool isAdmin(const Location& location)
{
    return location.isValid() && g_file_has_uri_scheme(location.gfile(), "admin");
}

Location of(const Location& location)
{
    if (!location.isNative())
        return location;
    // file:///some/path, with the path escaped as a URI wants it.
    const QString uri = location.uri();
    return Location::fromUri(QStringLiteral("admin") + uri.mid(uri.indexOf(QLatin1Char(':'))));
}

Location plain(const Location& location)
{
    if (!isAdmin(location))
        return location;
    const QString uri = location.uri();
    return Location::fromUri(QStringLiteral("file") + uri.mid(uri.indexOf(QLatin1Char(':'))));
}

bool offerFor(const Location& folder)
{
    if (!folder.isNative() || !available())
        return false;
    GPtr<GFileInfo> info(g_file_query_info(folder.gfile(), G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, G_FILE_QUERY_INFO_NONE,
                                           nullptr, nullptr));
    return info && !g_file_info_get_attribute_boolean(info.get(), G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE);
}

QString refusal(const QString& message)
{
    return QCoreApplication::translate("core::admin", "%1 (polkit refused, or no polkit agent is running)").arg(message);
}

} // namespace core::admin
