// The GTK bookmarks file (~/.config/gtk-3.0/bookmarks): one "uri [label]"
// per line, shared with Thunar, Nautilus and the file chooser. Read on
// start and whenever the file changes.
#pragma once

#include "core/location.h"

#include <QObject>
#include <QString>
#include <vector>

typedef struct _GFileMonitor GFileMonitor;

namespace core {

struct Bookmark {
    QString name;
    Location location;
};

class Bookmarks : public QObject {
    Q_OBJECT
public:
    // Empty path means the standard file.
    explicit Bookmarks(const QByteArray& path = {}, QObject* parent = nullptr);
    ~Bookmarks() override;

    const std::vector<Bookmark>& list() const { return list_; }

    static std::vector<Bookmark> parse(const QByteArray& content);

Q_SIGNALS:
    void changed();

private:
    Q_INVOKABLE void reloadLater()
    {
        reload();
        Q_EMIT changed();
    }
    void reload();

    Location file_;
    GFileMonitor* monitor_ = nullptr;
    std::vector<Bookmark> list_;
};

} // namespace core
