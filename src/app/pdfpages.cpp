#include "pdfpages.h"

#include "counted.h"
#include <QWidget>
#include <algorithm>

namespace {
constexpr size_t kCachedPages = 4;
}

PdfPages::PdfPages(QWidget* slot, PageStrip* strip, QObject* parent)
    : QObject(parent)
    , slot_(slot)
    , strip_(strip)
    , renderer_(new core::PageRenderer(this))
{
    connect(strip_, &PageStrip::stepRequested, this, &PdfPages::step);
    connect(renderer_, &core::PageRenderer::infoReady, this, &PdfPages::onInfo);
    connect(renderer_, &core::PageRenderer::rendered, this, &PdfPages::onRendered);
    connect(renderer_, &core::PageRenderer::failed, this, &PdfPages::onFailed);
}

void PdfPages::start(const core::Location& file)
{
    cancel();
    file_ = file;
    renderer_->info(file_, generation_);
}

void PdfPages::cancel()
{
    ++generation_;
    renderer_->cancel();
    file_ = core::Location();
    info_ = core::PdfInfo();
    page_ = 0;
    cache_.clear();
    strip_->clear();
}

void PdfPages::step(Step step)
{
    if (!active())
        return;
    switch (step) {
    case Step::First: goToPage(1); return;
    case Step::Prev: goToPage(page_ - 1); return;
    case Step::Next: goToPage(page_ + 1); return;
    case Step::Last: goToPage(info_.pages); return;
    }
}

void PdfPages::goToPage(int page)
{
    if (info_.pages < 1)
        return;
    page = std::clamp(page, 1, info_.pages);
    page_ = page;
    strip_->setPages(page, info_.pages);
    const double wanted = dpi();
    for (const Cached& c : cache_) {
        if (c.page == page && c.dpi == wanted) {
            Q_EMIT pageReady(c.image);
            return;
        }
    }
    renderer_->render(file_, page, wanted, generation_);
}

double PdfPages::dpi() const
{
    const QSize box = slot_->size() * slot_->devicePixelRatioF();
    return core::fitDpi(info_.widthPt, info_.heightPt, static_cast<quint32>(std::max(1, box.width())),
                        static_cast<quint32>(std::max(1, box.height())));
}

void PdfPages::onInfo(quint64 generation, const core::PdfInfo& info)
{
    if (generation != generation_)
        return;
    info_ = info;
    if (info.pages < 1) {
        Q_EMIT note(info.encrypted ? tr("Encrypted") : tr("No pages"));
        return;
    }
    Q_EMIT note(counted(info.pages, tr("page"), tr("pages")));
    goToPage(1);
}

void PdfPages::onRendered(quint64 generation, int page, double dpi, const core::Raster& raster)
{
    if (generation != generation_)
        return;
    // The QImage wraps a copy of the bytes it owns through the cleanup.
    auto* bytes = new QByteArray(raster.rgb);
    const QImage image(reinterpret_cast<const uchar*>(bytes->constData()), static_cast<int>(raster.width),
                       static_cast<int>(raster.height), static_cast<qsizetype>(raster.width) * 3, QImage::Format_RGB888,
                       [](void* p) { delete static_cast<QByteArray*>(p); }, bytes);
    if (cache_.size() >= kCachedPages)
        cache_.erase(cache_.begin());
    cache_.push_back({page, dpi, image});
    if (page == page_)
        Q_EMIT pageReady(image);
}

void PdfPages::onFailed(quint64 generation, const QString& message, bool missing)
{
    if (generation != generation_ || missing)
        return;  // without poppler the thumbnail stands and nothing is said
    Q_EMIT note(message);
}
