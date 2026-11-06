// PDF pages for the preview without a decoder in the process (rule 8):
// poppler's command line tools run in a child, as bsdtar does for
// archives. pdfinfo gives the page count and the first page's size;
// pdftoppm renders one page to its stdout as a binary PPM, whose header
// is three numbers and whose body the app wraps in a QImage as it is.
#pragma once

#include "core/child.h"
#include "core/location.h"

#include <QByteArray>
#include <QObject>
#include <QString>


namespace core {

struct PdfInfo {
    int pages = 0;
    double widthPt = 0;   // the first page as rendered: rotation applied
    double heightPt = 0;
    bool encrypted = false;
};

// Parses "pdfinfo" output. pages stays 0 when the Pages line is missing.
// Exposed for tests.
PdfInfo parsePdfInfo(const QByteArray& output);

struct Raster {
    quint32 width = 0;
    quint32 height = 0;
    QByteArray rgb;  // packed, 3 bytes per pixel, width * 3 per row
};

// Parses a binary PPM (P6, maxval 255) as pdftoppm writes it. Returns an
// empty raster when the header or the body does not fit. Exposed for
// tests.
Raster parsePpm(const QByteArray& data);

// The dots per inch that fit a page of the given size in points into a
// box of pixels, keeping the aspect ratio.
double fitDpi(double widthPt, double heightPt, quint32 boxWidth, quint32 boxHeight);

class PageRenderer : public QObject {
    Q_OBJECT
public:
    explicit PageRenderer(QObject* parent = nullptr);
    ~PageRenderer() override;

    // Local files only: the tools need a path. Each cancels its
    // predecessor of the same kind; info and render may run together.
    void info(const Location& file, quint64 generation);
    void render(const Location& file, int page, double dpi, quint64 generation);
    void cancel();

Q_SIGNALS:
    void infoReady(quint64 generation, core::PdfInfo info);
    void rendered(quint64 generation, int page, double dpi, core::Raster raster);
    // missing is true when the tool is not installed: the caller keeps
    // quiet then, the thumbnail already says what it can.
    void failed(quint64 generation, const QString& message, bool missing);

private:
    // False, with failed() emitted, when the run did not give output.
    bool succeeded(const Child::Result& r, const char* tool, quint64 generation);
    // One child of each kind at a time, killed on cancel.
    Child info_;
    Child render_;
};

} // namespace core

Q_DECLARE_METATYPE(core::PdfInfo)
Q_DECLARE_METATYPE(core::Raster)
