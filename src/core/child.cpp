#include "core/child.h"

#include "core/gobject.h"

namespace core {

namespace {

QByteArray takeBytes(GBytes* bytes)
{
    if (!bytes)
        return {};
    gsize n = 0;
    const char* data = static_cast<const char*>(g_bytes_get_data(bytes, &n));
    QByteArray out(data, static_cast<qsizetype>(n));
    g_bytes_unref(bytes);
    return out;
}

} // namespace

struct Child::Call {
    Child* self;
    // Its own reference: once cancelled, self has moved on or is gone
    // and must not be touched.
    GPtr<GCancellable> cancellable;
    std::function<void(const Result&)> done;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(data));
        GError* error = nullptr;
        GBytes* out = nullptr;
        GBytes* err = nullptr;
        g_subprocess_communicate_finish(G_SUBPROCESS(source), result, &out, &err, &error);
        GErrorPtr gerr(error);
        Result r;
        r.out = takeBytes(out);
        r.err = takeBytes(err);
        if (g_cancellable_is_cancelled(call->cancellable.get()))
            return;
        // The child is done: nothing to kill later.
        Child* self = call->self;
        g_object_unref(self->process_);
        g_object_unref(self->cancellable_);
        self->process_ = nullptr;
        self->cancellable_ = nullptr;
        r.spawned = true;
        r.ok = !gerr && g_subprocess_get_successful(G_SUBPROCESS(source));
        r.error = errorMessage(gerr.get());
        call->done(r);  // may destroy self
    }
};

QString Child::Result::why(const char* tool, bool lastLineOnly) const
{
    QString message = QString::fromUtf8(err).trimmed();
    if (lastLineOnly)
        if (const qsizetype nl = message.lastIndexOf(QLatin1Char('\n')); nl >= 0)
            message = message.mid(nl + 1).trimmed();
    if (message.isEmpty())
        message = error;
    if (message.isEmpty())
        message = QStringLiteral("%1 failed").arg(QLatin1String(tool));
    return message;
}

void Child::run(const char* const* argv, std::function<void(const Result&)> done, const QByteArray* input,
                const char* const* env)
{
    cancel();
    GPtr<GSubprocessLauncher> launcher(g_subprocess_launcher_new(static_cast<GSubprocessFlags>(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE | (input ? G_SUBPROCESS_FLAGS_STDIN_PIPE : 0))));
    g_subprocess_launcher_setenv(launcher.get(), "LC_ALL", "C.UTF-8", TRUE);
    for (; env && env[0] && env[1]; env += 2)
        g_subprocess_launcher_setenv(launcher.get(), env[0], env[1], TRUE);
    GError* error = nullptr;
    GSubprocess* process = g_subprocess_launcher_spawnv(launcher.get(), argv, &error);
    if (!process) {
        GErrorPtr err(error);
        Result r;
        r.missing = g_error_matches(err.get(), G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT);
        r.error = errorMessage(err.get());
        done(r);
        return;
    }
    process_ = process;
    cancellable_ = g_cancellable_new();
    auto* call = new Call{this, GPtr<GCancellable>(G_CANCELLABLE(g_object_ref(cancellable_))), std::move(done)};
    GBytes* bytes = input ? g_bytes_new(input->constData(), static_cast<gsize>(input->size())) : nullptr;
    g_subprocess_communicate_async(process, bytes, cancellable_, &Call::onDone, call);
    if (bytes)
        g_bytes_unref(bytes);
}

void Child::cancel()
{
    if (!process_)
        return;
    g_cancellable_cancel(cancellable_);
    g_subprocess_force_exit(process_);
    g_object_unref(process_);
    g_object_unref(cancellable_);
    process_ = nullptr;
    cancellable_ = nullptr;
}

} // namespace core
