// Thumbnails through the freedesktop cache and tumbler over D-Bus
// (org.freedesktop.thumbnails.Thumbnailer1), rule 8: nothing is decoded
// here. A request first looks in the cache; a miss is queued to the
// daemon in small batches and ready(uri, path) follows when the PNG is
// there. supports(mime) says whether the daemon has a thumbnailer for a
// type, so unsupported files never generate requests.
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QTimer;
typedef struct _GDBusProxy GDBusProxy;

namespace core {

class Thumbnailer : public QObject {
    Q_OBJECT
public:
    enum class Flavor { Normal, Large, XLarge, XXLarge };

    explicit Thumbnailer(QObject* parent = nullptr);
    ~Thumbnailer() override;

    // Known only after the daemon answered; false until then.
    bool available() const { return proxy_ != nullptr && !supported_.isEmpty(); }
    bool supports(const QString& mime) const;

    // The cached PNG for the uri, or empty when none exists yet.
    static QString cachedPath(const QString& uri, Flavor flavor);
    static QString flavorName(Flavor flavor);

    // Cache first, then the daemon. ready() or failed() follows for a
    // miss; a hit returns the path at once and emits nothing.
    QString request(const QString& uri, const QString& mime, Flavor flavor);

Q_SIGNALS:
    void ready(const QString& uri, const QString& path);
    void failed(const QString& uri);
    void becameAvailable();

private:
    struct Hook;
    void flush();
    void onSignal(const char* name, const QStringList& uris, quint32 handle);

    GDBusProxy* proxy_ = nullptr;
    QSet<QString> supported_;
    QTimer* timer_;
    struct Pending {
        QString uri;
        QString mime;
        Flavor flavor;
    };
    QList<Pending> queue_;
    QHash<quint32, Flavor> handles_;  // in flight at the daemon
};

} // namespace core
