#include "core/pages.h"

#include "core/gobject.h"

#include <QPointer>
#include <algorithm>
#include <cctype>

namespace core {

PdfInfo parsePdfInfo(const QByteArray& output)
{
    PdfInfo info;
    int rotation = 0;
    for (const QByteArray& raw : output.split('\n')) {
        const int colon = raw.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray key = raw.left(colon).trimmed();
        const QByteArray value = raw.mid(colon + 1).trimmed();
        if (key == "Pages") {
            info.pages = std::max(0, value.toInt());
        } else if (key == "Page size") {
            // "612 x 792 pts (letter)"
            const QList<QByteArray> parts = value.split(' ');
            if (parts.size() >= 3 && parts[1] == "x") {
                info.widthPt = parts[0].toDouble();
                info.heightPt = parts[2].toDouble();
            }
        } else if (key == "Page rot") {
            rotation = value.toInt();
        } else if (key == "Encrypted") {
            info.encrypted = value.startsWith("yes");
        }
    }
    if (rotation == 90 || rotation == 270)
        std::swap(info.widthPt, info.heightPt);
    if (!(info.widthPt > 0) || !(info.heightPt > 0))
        info.widthPt = info.heightPt = 0;
    return info;
}

Raster parsePpm(const QByteArray& data)
{
    // "P6" then width, height and maxval, each preceded by whitespace,
    // then one whitespace byte and the pixels. pdftoppm writes no
    // comments; a reader of its output need not accept them.
    Raster out;
    if (!data.startsWith("P6"))
        return out;
    int pos = 2;
    quint64 numbers[3] = {0, 0, 0};
    for (quint64& n : numbers) {
        while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos])))
            ++pos;
        const int start = pos;
        while (pos < data.size() && data[pos] >= '0' && data[pos] <= '9' && pos - start < 10)
            n = n * 10 + static_cast<quint64>(data[pos++] - '0');
        if (pos == start)
            return out;
    }
    if (pos >= data.size() || !std::isspace(static_cast<unsigned char>(data[pos])))
        return out;
    ++pos;
    if (numbers[0] == 0 || numbers[1] == 0 || numbers[2] != 255 || numbers[0] > 32768 || numbers[1] > 32768)
        return out;
    const quint64 bytes = numbers[0] * numbers[1] * 3;
    if (static_cast<quint64>(data.size() - pos) < bytes)
        return out;
    out.width = static_cast<quint32>(numbers[0]);
    out.height = static_cast<quint32>(numbers[1]);
    out.rgb = data.mid(pos, static_cast<qsizetype>(bytes));
    return out;
}

double fitDpi(double widthPt, double heightPt, quint32 boxWidth, quint32 boxHeight)
{
    if (!(widthPt > 0) || !(heightPt > 0) || boxWidth == 0 || boxHeight == 0)
        return 72;
    const double scale = std::min(boxWidth / widthPt, boxHeight / heightPt);
    // Bounded both ways: a tiny slot still renders legibly for the zoom
    // ImageView does, and a huge page in a huge slot stays a few MB.
    return std::clamp(72 * scale, 18.0, 400.0);
}

namespace {

QString toolError(const char* tool, const GError* error)
{
    if (g_error_matches(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        return QStringLiteral("%1 is not installed (package poppler)").arg(QLatin1String(tool));
    return errorMessage(error);
}

} // namespace

struct PageRenderer::Call {
    QPointer<PageRenderer> owner;
    quint64 generation = 0;
    bool isInfo = false;
    int page = 0;
    double dpi = 0;

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
        // The child is done: nothing to kill later.
        if (Child& child = call->isInfo ? call->owner->info_ : call->owner->render_; child.process == proc.get()) {
            g_object_unref(child.process);
            g_object_unref(child.cancellable);
            child = Child();
        }
        gsize outSize = 0;
        const char* outData = out ? static_cast<const char*>(g_bytes_get_data(out, &outSize)) : nullptr;
        const QByteArray output(outData, static_cast<qsizetype>(outSize));
        if (gerr || !g_subprocess_get_successful(proc.get())) {
            // pdfinfo exits 1 on a damaged file after printing what it
            // could; the last stderr line says why.
            gsize n = 0;
            const char* text = err ? static_cast<const char*>(g_bytes_get_data(err, &n)) : nullptr;
            QString message = text && n ? QString::fromUtf8(text, static_cast<int>(n)).trimmed() : errorMessage(gerr.get());
            if (const int nl = message.lastIndexOf(QLatin1Char('\n')); nl >= 0)
                message = message.mid(nl + 1).trimmed();
            if (message.isEmpty())
                message = QStringLiteral("%1 failed").arg(QLatin1String(call->isInfo ? "pdfinfo" : "pdftoppm"));
            Q_EMIT call->owner->failed(call->generation, message, false);
            return;
        }
        if (call->isInfo) {
            Q_EMIT call->owner->infoReady(call->generation, parsePdfInfo(output));
            return;
        }
        Raster raster = parsePpm(output);
        if (raster.rgb.isEmpty()) {
            Q_EMIT call->owner->failed(call->generation, QStringLiteral("pdftoppm wrote no page"), false);
            return;
        }
        Q_EMIT call->owner->rendered(call->generation, call->page, call->dpi, std::move(raster));
    }
};

PageRenderer::PageRenderer(QObject* parent)
    : QObject(parent)
{
}

PageRenderer::~PageRenderer()
{
    cancel();
}

void PageRenderer::info(const Location& file, quint64 generation)
{
    cancelOne(info_);
    const QByteArray path = file.path();
    if (path.isEmpty()) {
        Q_EMIT failed(generation, QStringLiteral("pages render from local files only"), true);
        return;
    }
    const char* argv[] = {"pdfinfo", path.constData(), nullptr};
    spawn(info_, argv, new Call{this, generation, true, 0, 0}, "pdfinfo", generation);
}

void PageRenderer::render(const Location& file, int page, double dpi, quint64 generation)
{
    cancelOne(render_);
    const QByteArray path = file.path();
    if (path.isEmpty() || page < 1) {
        Q_EMIT failed(generation, QStringLiteral("pages render from local files only"), true);
        return;
    }
    const QByteArray pageText = QByteArray::number(page);
    const QByteArray dpiText = QByteArray::number(dpi, 'f', 2);
    // No output root: a single page goes to stdout as a binary PPM.
    const char* argv[] = {"pdftoppm", "-r", dpiText.constData(), "-f", pageText.constData(), "-l",
                          pageText.constData(), path.constData(), nullptr};
    spawn(render_, argv, new Call{this, generation, false, page, dpi}, "pdftoppm", generation);
}

void PageRenderer::spawn(Child& child, const char* const* argv, Call* call, const char* tool, quint64 generation)
{
    std::unique_ptr<Call> owned(call);
    GPtr<GSubprocessLauncher> launcher(g_subprocess_launcher_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE)));
    g_subprocess_launcher_setenv(launcher.get(), "LC_ALL", "C.UTF-8", TRUE);
    GError* error = nullptr;
    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher.get(), argv, &error);
    if (!proc) {
        GErrorPtr err(error);
        Q_EMIT failed(generation, toolError(tool, err.get()),
                      g_error_matches(err.get(), G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT));
        return;
    }
    child.process = proc;
    child.cancellable = g_cancellable_new();
    g_subprocess_communicate_async(proc, nullptr, child.cancellable, &Call::onDone, owned.release());
}

void PageRenderer::cancel()
{
    cancelOne(info_);
    cancelOne(render_);
}

void PageRenderer::cancelOne(Child& child)
{
    if (!child.cancellable)
        return;
    // Cancelling the read does not stop the child; a page half rendered
    // is work nobody will see (rule 16).
    g_cancellable_cancel(child.cancellable);
    g_subprocess_force_exit(child.process);
    g_object_unref(child.process);
    g_object_unref(child.cancellable);
    child = Child();
}

} // namespace core
