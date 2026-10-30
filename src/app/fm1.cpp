#include "fm1.h"

#include "mainwindow.h"

#include <QDBusConnection>

FileManager1::FileManager1(MainWindow* window)
    : QDBusAbstractAdaptor(window)
    , window_(window)
{
}

bool FileManager1::publish(MainWindow* window)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    new FileManager1(window);
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), window))
        return false;
    return bus.registerService(QStringLiteral("org.freedesktop.FileManager1"));
}

void FileManager1::ShowFolders(const QStringList& uris, const QString&)
{
    for (const QString& uri : uris)
        window_->showFolder(core::Location::fromUri(uri));
}

void FileManager1::ShowItems(const QStringList& uris, const QString&)
{
    for (const QString& uri : uris)
        window_->showItem(core::Location::fromUri(uri));
}

void FileManager1::ShowItemProperties(const QStringList& uris, const QString&)
{
    // No properties panel yet: reveal the item instead.
    ShowItems(uris, QString());
}
