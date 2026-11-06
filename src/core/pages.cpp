#include "core/pages.h"

#include "core/gobject.h"

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

PageRenderer::PageRenderer(QObject* parent)
    : QObject(parent)
{
}

PageRenderer::~PageRenderer()
{
    cancel();
}

bool PageRenderer::succeeded(const Child::Result& r, const char* tool, quint64 generation)
{
    if (!r.spawned) {
        Q_EMIT failed(generation,
                      r.missing ? QStringLiteral("%1 is not installed (package poppler)").arg(QLatin1String(tool)) : r.error,
                      r.missing);
        return false;
    }
    if (!r.ok) {
        // pdfinfo exits 1 on a damaged file after printing what it
        // could; the last stderr line says why.
        Q_EMIT failed(generation, r.why(tool, true), false);
        return false;
    }
    return true;
}

void PageRenderer::info(const Location& file, quint64 generation)
{
    info_.cancel();
    const QByteArray path = file.path();
    if (path.isEmpty()) {
        Q_EMIT failed(generation, QStringLiteral("pages render from local files only"), true);
        return;
    }
    const char* argv[] = {"pdfinfo", path.constData(), nullptr};
    info_.run(argv, [this, generation](const Child::Result& r) {
        if (succeeded(r, "pdfinfo", generation))
            Q_EMIT infoReady(generation, parsePdfInfo(r.out));
    });
}

void PageRenderer::render(const Location& file, int page, double dpi, quint64 generation)
{
    render_.cancel();
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
    render_.run(argv, [this, generation, page, dpi](const Child::Result& r) {
        if (!succeeded(r, "pdftoppm", generation))
            return;
        Raster raster = parsePpm(r.out);
        if (raster.rgb.isEmpty())
            Q_EMIT failed(generation, QStringLiteral("pdftoppm wrote no page"), false);
        else
            Q_EMIT rendered(generation, page, dpi, std::move(raster));
    });
}

void PageRenderer::cancel()
{
    info_.cancel();
    render_.cancel();
}

} // namespace core
