// The preview's PDF side: asks core's PageRenderer for the page count
// and for pages at the slot's size, keeps the last few rendered pages so
// stepping back is instant, and drives the page strip. Owns no decoder:
// the pixels come from pdftoppm in a child (rule 8).
#pragma once

#include "core/location.h"
#include "core/pages.h"

#include <QImage>
#include <QObject>
#include <vector>

class PageStrip;
class QWidget;

class PdfPages : public QObject {
    Q_OBJECT
public:
    // slot: the widget whose size a page is rendered to fit. The strip is
    // created as a child of stripParent for the caller to lay out.
    PdfPages(QWidget* slot, QWidget* stripParent);

    PageStrip* strip() const { return strip_; }
    // Starts with the first page; pages come through pageReady.
    void start(const core::Location& file);
    // Kills the children, drops the cache and hides the strip.
    void cancel();

    int pageCount() const { return info_.pages; }
    int page() const { return page_; }
    void goToPage(int page);  // clamped to the document

Q_SIGNALS:
    void pageReady(const QImage& image);
    // A line for the info block: the page count, or why there is none.
    void note(const QString& text);

private:
    void onInfo(quint64 generation, const core::PdfInfo& info);
    void onRendered(quint64 generation, int page, double dpi, const core::Raster& raster);
    void onFailed(quint64 generation, const QString& message, bool missing);
    double dpi() const;

    QWidget* slot_;
    PageStrip* strip_;
    core::PageRenderer* renderer_;
    core::Location file_;
    quint64 generation_ = 0;
    core::PdfInfo info_;
    int page_ = 0;  // 1-based, 0 while nothing is shown
    struct Cached {
        int page;
        double dpi;
        QImage image;
    };
    std::vector<Cached> cache_;
};
