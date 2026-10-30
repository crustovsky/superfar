// org.freedesktop.FileManager1: launchers and browsers call ShowItems
// to reveal a download. The first instance owns the name.
#pragma once

#include <QDBusAbstractAdaptor>
#include <QStringList>

class MainWindow;

class FileManager1 : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    explicit FileManager1(MainWindow* window);

    // False when another instance already owns the name.
    static bool publish(MainWindow* window);

public Q_SLOTS:
    void ShowFolders(const QStringList& uris, const QString& startupId);
    void ShowItems(const QStringList& uris, const QString& startupId);
    void ShowItemProperties(const QStringList& uris, const QString& startupId);

private:
    MainWindow* window_;
};
