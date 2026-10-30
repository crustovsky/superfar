#include "core/thumbnails.h"

#include "core/gobject.h"

#include <QCryptographicHash>
#include <QPointer>
#include <QTimer>

namespace core {

namespace {
constexpr int kBatchMs = 40;
constexpr int kBatchMax = 32;
const char* const kService = "org.freedesktop.thumbnails.Thumbnailer1";
const char* const kPath = "/org/freedesktop/thumbnails/Thumbnailer1";

QStringList fromStrv(GVariant* v)
{
    QStringList out;
    gsize n = 0;
    const gchar** items = g_variant_get_strv(v, &n);
    for (gsize i = 0; i < n; ++i)
        out << QString::fromUtf8(items[i]);
    g_free(items);
    return out;
}
} // namespace

struct Thumbnailer::Hook {
    static void onProxy(GObject*, GAsyncResult* result, gpointer data)
    {
        auto* self = static_cast<Thumbnailer*>(static_cast<QPointer<Thumbnailer>*>(data)->data());
        delete static_cast<QPointer<Thumbnailer>*>(data);
        GError* error = nullptr;
        GDBusProxy* proxy = g_dbus_proxy_new_for_bus_finish(result, &error);
        GErrorPtr err(error);
        if (!self) {
            if (proxy)
                g_object_unref(proxy);
            return;
        }
        if (!proxy) {
            self->unavailable_ = true;
            return;
        }
        self->proxy_ = proxy;
        g_signal_connect(proxy, "g-signal", G_CALLBACK(&Hook::onGSignal), self);
        g_dbus_proxy_call(proxy, "GetSupported", nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
                          &Hook::onSupported, new QPointer<Thumbnailer>(self));
    }

    static void onSupported(GObject* source, GAsyncResult* result, gpointer data)
    {
        auto* self = static_cast<Thumbnailer*>(static_cast<QPointer<Thumbnailer>*>(data)->data());
        delete static_cast<QPointer<Thumbnailer>*>(data);
        GError* error = nullptr;
        GVariant* reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, &error);
        GErrorPtr err(error);
        if (self && !reply)
            self->unavailable_ = true;  // no thumbnailer on this bus
        if (!self || !reply) {
            if (reply)
                g_variant_unref(reply);
            return;
        }
        GVariant* schemes = nullptr;
        GVariant* mimes = nullptr;
        g_variant_get(reply, "(@as@as)", &schemes, &mimes);
        for (const QString& m : fromStrv(mimes))
            self->supported_.insert(m);
        g_variant_unref(schemes);
        g_variant_unref(mimes);
        g_variant_unref(reply);
        Q_EMIT self->becameAvailable();
        self->flush();
    }

    static void onGSignal(GDBusProxy*, gchar*, gchar* name, GVariant* params, gpointer data)
    {
        auto* self = static_cast<Thumbnailer*>(data);
        const QByteArray n(name);
        quint32 handle = 0;
        QStringList uris;
        if (n == "Ready") {
            GVariant* v = nullptr;
            g_variant_get(params, "(u@as)", &handle, &v);
            uris = fromStrv(v);
            g_variant_unref(v);
        } else if (n == "Error") {
            GVariant* v = nullptr;
            gint code = 0;
            gchar* msg = nullptr;
            g_variant_get(params, "(u@asis)", &handle, &v, &code, &msg);
            uris = fromStrv(v);
            g_variant_unref(v);
            g_free(msg);
        } else if (n == "Finished") {
            g_variant_get(params, "(u)", &handle);
        } else {
            return;
        }
        self->onSignal(name, uris, handle);
    }

    struct Queued {
        QPointer<Thumbnailer> self;
        Flavor flavor;
        QStringList uris;
    };

    static void onQueued(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Queued> box(static_cast<Queued*>(data));
        Thumbnailer* self = box->self.data();
        GError* error = nullptr;
        GVariant* reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, &error);
        GErrorPtr err(error);
        if (!reply) {
            // The daemon refused the batch: say so once and let the rows fall
            // back to their icons.
            qWarning("thumbnailer: %s", err ? err->message : "no reply");
            if (self)
                for (const QString& uri : box->uris)
                    Q_EMIT self->failed(uri);
            return;
        }
        quint32 handle = 0;
        g_variant_get(reply, "(u)", &handle);
        g_variant_unref(reply);
        if (self)
            self->handles_.insert(handle, box->flavor);
    }
};

Thumbnailer::Thumbnailer(QObject* parent)
    : QObject(parent)
    , timer_(new QTimer(this))
{
    timer_->setSingleShot(true);
    timer_->setInterval(kBatchMs);
    connect(timer_, &QTimer::timeout, this, &Thumbnailer::flush);
    g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr, kService, kPath, kService,
                             nullptr, &Hook::onProxy, new QPointer<Thumbnailer>(this));
}

Thumbnailer::~Thumbnailer()
{
    if (proxy_) {
        g_signal_handlers_disconnect_by_data(proxy_, this);
        g_object_unref(proxy_);
    }
}

bool Thumbnailer::supports(const QString& mime) const
{
    return supported_.contains(mime);
}

QString Thumbnailer::flavorName(Flavor flavor)
{
    switch (flavor) {
    case Flavor::Normal: return QStringLiteral("normal");
    case Flavor::Large: return QStringLiteral("large");
    case Flavor::XLarge: return QStringLiteral("x-large");
    case Flavor::XXLarge: return QStringLiteral("xx-large");
    }
    return {};
}

QString Thumbnailer::cachedPath(const QString& uri, Flavor flavor)
{
    const QByteArray md5 = QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex();
    GStr path(g_build_filename(g_get_user_cache_dir(), "thumbnails", flavorName(flavor).toUtf8().constData(),
                               (md5 + ".png").constData(), nullptr));
    return QString::fromUtf8(path.get());
}

QString Thumbnailer::request(const QString& uri, const QString& mime, Flavor flavor)
{
    const QString path = cachedPath(uri, flavor);
    if (g_file_test(path.toUtf8().constData(), G_FILE_TEST_IS_REGULAR))
        return path;
    queue_.append({uri, mime, flavor});
    if (!timer_->isActive())
        timer_->start();
    return {};
}

void Thumbnailer::flush()
{
    if (!proxy_ || supported_.isEmpty() || queue_.isEmpty())
        return;  // flushed again once the daemon has answered
    // One call per flavor, a few dozen uris each.
    while (!queue_.isEmpty()) {
        const Flavor flavor = queue_.first().flavor;
        GVariantBuilder uris, mimes;
        g_variant_builder_init(&uris, G_VARIANT_TYPE_STRING_ARRAY);
        g_variant_builder_init(&mimes, G_VARIANT_TYPE_STRING_ARRAY);
        auto* box = new Hook::Queued{this, flavor, {}};
        for (int i = 0; i < queue_.size() && box->uris.size() < kBatchMax;) {
            if (queue_[i].flavor != flavor) {
                ++i;
                continue;
            }
            g_variant_builder_add(&uris, "s", queue_[i].uri.toUtf8().constData());
            g_variant_builder_add(&mimes, "s", queue_[i].mime.toUtf8().constData());
            box->uris << queue_[i].uri;
            queue_.removeAt(i);
        }
        GVariant* args = g_variant_new("(asasssu)", &uris, &mimes, flavorName(flavor).toUtf8().constData(),
                                       "foreground", 0u);
        g_dbus_proxy_call(proxy_, "Queue", args, G_DBUS_CALL_FLAGS_NONE, 30000, nullptr, &Hook::onQueued, box);
    }
}

void Thumbnailer::onSignal(const char* name, const QStringList& uris, quint32 handle)
{
    const QByteArray n(name);
    if (n == "Finished") {
        handles_.remove(handle);
        return;
    }
    const Flavor flavor = handles_.value(handle, Flavor::Normal);
    for (const QString& uri : uris) {
        if (n == "Ready")
            Q_EMIT ready(uri, cachedPath(uri, flavor));
        else
            Q_EMIT failed(uri);
    }
}

} // namespace core
