#include "core/mounts.h"

#include "core/gobject.h"

#include <QPointer>

namespace core {

namespace {

QStringList iconNamesOf(GIcon* icon)
{
    QStringList names;
    if (icon && G_IS_THEMED_ICON(icon)) {
        const gchar* const* n = g_themed_icon_get_names(G_THEMED_ICON(icon));
        for (; n && *n; ++n)
            names.append(QString::fromUtf8(*n));
    }
    return names;
}

QString mountId(GMount* mount)
{
    GPtr<GFile> root(g_mount_get_root(mount));
    GStr uri(g_file_get_uri(root.get()));
    return QStringLiteral("mount:") + QString::fromUtf8(uri.get());
}

QString volumeId(GVolume* volume)
{
    for (const char* kind : {G_VOLUME_IDENTIFIER_KIND_UUID, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE,
                             G_VOLUME_IDENTIFIER_KIND_LABEL}) {
        GStr id(g_volume_get_identifier(volume, kind));
        if (id)
            return QStringLiteral("volume:") + QString::fromUtf8(id.get());
    }
    GStr name(g_volume_get_name(volume));
    return QStringLiteral("volume:") + QString::fromUtf8(name.get());
}

GMount* findMount(GVolumeMonitor* monitor, const QString& id)
{
    GList* mounts = g_volume_monitor_get_mounts(monitor);
    GMount* found = nullptr;
    for (GList* l = mounts; l; l = l->next)
        if (!found && mountId(G_MOUNT(l->data)) == id)
            found = G_MOUNT(g_object_ref(l->data));
    g_list_free_full(mounts, g_object_unref);
    return found;
}

GVolume* findVolume(GVolumeMonitor* monitor, const QString& id)
{
    GList* volumes = g_volume_monitor_get_volumes(monitor);
    GVolume* found = nullptr;
    for (GList* l = volumes; l; l = l->next)
        if (!found && volumeId(G_VOLUME(l->data)) == id)
            found = G_VOLUME(g_object_ref(l->data));
    g_list_free_full(volumes, g_object_unref);
    return found;
}

} // namespace

// One mount, unmount or mount-enclosing call in flight.
struct Mounts::Op {
    QPointer<Mounts> owner;
    Done done;
    GMountOperation* operation = nullptr;

    ~Op()
    {
        if (operation)
            g_object_unref(operation);
    }

    void finish(GError* error)
    {
        GErrorPtr err(error);
        if (done)
            done(err && !isCancelled(err.get()) ? errorMessage(err.get()) : QString());
    }

    static void onVolumeMounted(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Op> op(static_cast<Op*>(data));
        GError* error = nullptr;
        g_volume_mount_finish(G_VOLUME(source), result, &error);
        op->finish(error);
    }

    static void onUnmounted(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Op> op(static_cast<Op*>(data));
        GError* error = nullptr;
        g_mount_unmount_with_operation_finish(G_MOUNT(source), result, &error);
        op->finish(error);
    }

    static void onEjected(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Op> op(static_cast<Op*>(data));
        GError* error = nullptr;
        g_mount_eject_with_operation_finish(G_MOUNT(source), result, &error);
        op->finish(error);
    }

    static void onEnclosingMounted(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Op> op(static_cast<Op*>(data));
        GError* error = nullptr;
        g_file_mount_enclosing_volume_finish(G_FILE(source), result, &error);
        // Already mounted counts as success.
        if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED)) {
            g_error_free(error);
            error = nullptr;
        }
        op->finish(error);
    }
};

namespace {

void onAskPassword(GMountOperation* op, gchar* message, gchar* defaultUser, gchar* defaultDomain,
                   GAskPasswordFlags flags, gpointer data)
{
    auto* mounts = static_cast<Mounts*>(data);
    const quint64 token = reinterpret_cast<quintptr>(g_object_get_data(G_OBJECT(op), "superfar-token"));
    Q_EMIT mounts->askPassword(token, QString::fromUtf8(message), QString::fromUtf8(defaultUser),
                               QString::fromUtf8(defaultDomain), (flags & G_ASK_PASSWORD_NEED_USERNAME) != 0,
                               (flags & G_ASK_PASSWORD_NEED_DOMAIN) != 0,
                               (flags & G_ASK_PASSWORD_NEED_PASSWORD) != 0,
                               (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED) != 0);
}

void onAskQuestion(GMountOperation* op, gchar* message, GStrv choices, gpointer data)
{
    auto* mounts = static_cast<Mounts*>(data);
    const quint64 token = reinterpret_cast<quintptr>(g_object_get_data(G_OBJECT(op), "superfar-token"));
    QStringList list;
    for (gchar** c = choices; c && *c; ++c)
        list << QString::fromUtf8(*c);
    Q_EMIT mounts->askQuestion(token, QString::fromUtf8(message), list);
}

void onMonitorChanged(GVolumeMonitor*, gpointer, gpointer data)
{
    Q_EMIT static_cast<Mounts*>(data)->changed();
}

} // namespace

Mounts::Mounts(QObject* parent)
    : QObject(parent)
    , monitor_(g_volume_monitor_get())
{
    for (const char* sig : {"mount-added", "mount-removed", "mount-changed", "volume-added", "volume-removed",
                            "volume-changed"})
        handlers_.push_back(g_signal_connect(monitor_, sig, G_CALLBACK(onMonitorChanged), this));
}

Mounts::~Mounts()
{
    for (unsigned long h : handlers_)
        g_signal_handler_disconnect(monitor_, h);
    for (auto& [token, op] : prompts_) {
        g_mount_operation_reply(op, G_MOUNT_OPERATION_ABORTED);
        g_object_unref(op);
    }
    g_object_unref(monitor_);
}

std::vector<Place> Mounts::places() const
{
    std::vector<Place> out;
    GList* mounts = g_volume_monitor_get_mounts(monitor_);
    for (GList* l = mounts; l; l = l->next) {
        auto* mount = G_MOUNT(l->data);
        if (g_mount_is_shadowed(mount))
            continue;
        Place p;
        p.id = mountId(mount);
        GStr name(g_mount_get_name(mount));
        p.name = QString::fromUtf8(name.get());
        GPtr<GIcon> icon(g_mount_get_symbolic_icon(mount));
        p.iconNames = iconNamesOf(icon.get());
        p.location = Location::adopt(g_mount_get_root(mount));
        p.mounted = true;
        p.canUnmount = g_mount_can_unmount(mount);
        p.canEject = g_mount_can_eject(mount);
        out.push_back(std::move(p));
    }
    g_list_free_full(mounts, g_object_unref);

    GList* volumes = g_volume_monitor_get_volumes(monitor_);
    for (GList* l = volumes; l; l = l->next) {
        auto* volume = G_VOLUME(l->data);
        GPtr<GMount> mount(g_volume_get_mount(volume));
        if (mount || !g_volume_can_mount(volume))
            continue;
        Place p;
        p.id = volumeId(volume);
        GStr name(g_volume_get_name(volume));
        p.name = QString::fromUtf8(name.get());
        GPtr<GIcon> icon(g_volume_get_symbolic_icon(volume));
        p.iconNames = iconNamesOf(icon.get());
        out.push_back(std::move(p));
    }
    g_list_free_full(volumes, g_object_unref);
    return out;
}

GMountOperation* Mounts::newOperation()
{
    GMountOperation* op = g_mount_operation_new();
    const quint64 token = nextToken_++;
    g_object_set_data(G_OBJECT(op), "superfar-token", reinterpret_cast<gpointer>(static_cast<quintptr>(token)));
    g_signal_connect(op, "ask-password", G_CALLBACK(onAskPassword), this);
    g_signal_connect(op, "ask-question", G_CALLBACK(onAskQuestion), this);
    prompts_.emplace_back(token, G_MOUNT_OPERATION(g_object_ref(op)));
    return op;
}

void Mounts::finishPrompt(quint64 token, GMountOperation** op)
{
    *op = nullptr;
    for (auto it = prompts_.begin(); it != prompts_.end(); ++it) {
        if (it->first == token) {
            *op = it->second;
            return;
        }
    }
}

void Mounts::answerPassword(quint64 token, const QString& user, const QString& domain,
                            const QString& password, bool anonymous)
{
    GMountOperation* op = nullptr;
    finishPrompt(token, &op);
    if (!op)
        return;
    g_mount_operation_set_anonymous(op, anonymous);
    if (!anonymous) {
        g_mount_operation_set_username(op, user.toUtf8().constData());
        g_mount_operation_set_domain(op, domain.toUtf8().constData());
        g_mount_operation_set_password(op, password.toUtf8().constData());
    }
    g_mount_operation_reply(op, G_MOUNT_OPERATION_HANDLED);
}

void Mounts::answerQuestion(quint64 token, int choice)
{
    GMountOperation* op = nullptr;
    finishPrompt(token, &op);
    if (!op)
        return;
    g_mount_operation_set_choice(op, choice);
    g_mount_operation_reply(op, G_MOUNT_OPERATION_HANDLED);
}

void Mounts::abortPrompt(quint64 token)
{
    GMountOperation* op = nullptr;
    finishPrompt(token, &op);
    if (op)
        g_mount_operation_reply(op, G_MOUNT_OPERATION_ABORTED);
}

void Mounts::mountPlace(const QString& id, Done done)
{
    GPtr<GVolume> volume(findVolume(monitor_, id));
    if (!volume) {
        done(QStringLiteral("Volume not found"));
        return;
    }
    auto* op = new Op{this, std::move(done), newOperation()};
    g_volume_mount(volume.get(), G_MOUNT_MOUNT_NONE, op->operation, nullptr, &Op::onVolumeMounted, op);
}

void Mounts::unmountPlace(const QString& id, Done done)
{
    GPtr<GMount> mount(findMount(monitor_, id));
    if (!mount) {
        done(QStringLiteral("Mount not found"));
        return;
    }
    auto* op = new Op{this, std::move(done), newOperation()};
    // Removable media are ejected (unmount, then power down), so the
    // stick can be pulled; everything else is unmounted.
    if (g_mount_can_eject(mount.get()))
        g_mount_eject_with_operation(mount.get(), G_MOUNT_UNMOUNT_NONE, op->operation, nullptr, &Op::onEjected, op);
    else
        g_mount_unmount_with_operation(mount.get(), G_MOUNT_UNMOUNT_NONE, op->operation, nullptr,
                                       &Op::onUnmounted, op);
}

void Mounts::mountLocation(const Location& location, Done done)
{
    if (!location.isValid()) {
        done(QStringLiteral("invalid location"));
        return;
    }
    auto* op = new Op{this, std::move(done), newOperation()};
    g_file_mount_enclosing_volume(location.gfile(), G_MOUNT_MOUNT_NONE, op->operation, nullptr,
                                  &Op::onEnclosingMounted, op);
}

} // namespace core
