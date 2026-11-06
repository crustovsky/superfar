#include "preview.h"

#include "archivetext.h"
#include "core/unixattrs.h"
#include "elidedlabel.h"
#include "jobs/format.h"
#include "pagestrip.h"
#include "pdfpages.h"
#include "services.h"
#include "videoplay.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

// Paints an image scaled to fit, centred; the scaled copy is remade only
// when the size changes.
class ImageView : public QWidget {
public:
    using QWidget::QWidget;

    void setImage(const QImage& image)
    {
        image_ = image;
        scaled_ = QPixmap();
        update();
    }
    void clear() { setImage(QImage()); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (image_.isNull())
            return;
        const qreal dpr = devicePixelRatioF();
        const QSize box = size() * dpr;
        if (scaled_.isNull() || scaledFor_ != box) {
            QImage shown = image_.width() > box.width() || image_.height() > box.height()
                ? image_.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                : image_;
            shown.setDevicePixelRatio(dpr);
            scaled_ = QPixmap::fromImage(shown);
            scaledFor_ = box;
        }
        QPainter p(this);
        const QSizeF logical = scaled_.deviceIndependentSize();
        p.drawPixmap(QPointF((width() - logical.width()) / 2, (height() - logical.height()) / 2), scaled_);
    }

private:
    QImage image_;
    QPixmap scaled_;
    QSize scaledFor_;
};

namespace {
constexpr quint32 kTextCap = 1024 * 1024;

QString when(qint64 secs)
{
    return secs > 0 ? QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
}

QString permissions(quint32 mode)
{
    // The letters as the list's Permissions column has them, s and t included.
    if (mode == 0)
        return {};
    return QStringLiteral("%1 (%2)").arg(core::modeString(mode).mid(1)).arg(mode & 07777, 4, 8, QLatin1Char('0'));
}

bool isPdfType(const QString& contentType)
{
    return contentType == QLatin1String("application/pdf") || contentType == QLatin1String("application/x-pdf");
}

bool isVideoType(const QString& contentType)
{
    return contentType.startsWith(QLatin1String("video/"));
}
} // namespace

Preview::Preview(const Services& services, QWidget* parent)
    : QWidget(parent)
    , image_(new core::ImageLoader(this))
    , reader_(new core::Reader(this))
    , sizer_(new core::DirSizer(services.sizes, this))
    , lister_(new core::ArchiveLister(this))
    , thumbs_(services.thumbs)
    , title_(new ElidedLabel(this))
    , info_(new QLabel(this))
    , strip_(new PageStrip(this))
    , stack_(new QStackedWidget(this))
    , empty_(new QWidget(this))
    , picture_(new ImageView(this))
    , text_(new QPlainTextEdit(this))
{
    pdf_ = new PdfPages(stack_, strip_, this);
    video_ = new VideoPlayer(stack_, strip_, this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(title_);
    layout->addWidget(info_);
    layout->addWidget(strip_);
    layout->addWidget(stack_, 1);
    title_->setContentsMargins(4, 2, 4, 2);
    picture_->setMinimumSize(1, 1);
    text_->setReadOnly(true);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    text_->setFocusPolicy(Qt::NoFocus);
    info_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    info_->setContentsMargins(6, 4, 6, 4);
    info_->setWordWrap(true);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    stack_->addWidget(empty_);
    stack_->addWidget(picture_);
    stack_->addWidget(text_);
    setFocusPolicy(Qt::NoFocus);
    textFont_ = text_->font();

    connect(image_, &core::ImageLoader::decoded, this, &Preview::showImage);
    connect(image_, &core::ImageLoader::failed, this, [this](const QString& m) {
        addNote(tr("Could not decode: %1").arg(m));
    });
    connect(thumbs_, &core::Thumbnailer::ready, this, [this](const QString& uri, const QString& path) {
        if (uri != thumbUri_)
            return;
        thumbUri_.clear();
        picture_->setImage(QImage(path));  // tumbler's own output from the cache
        stack_->setCurrentWidget(picture_);
    });
    connect(thumbs_, &core::Thumbnailer::failed, this, [this](const QString& uri) {
        if (uri == thumbUri_)
            thumbUri_.clear();
    });
    connect(reader_, &core::Reader::opened, this, [this](quint64) { reader_->read(0, kTextCap); });
    connect(reader_, &core::Reader::data, this, [this](quint64, const QByteArray& bytes) {
        reader_->close();
        if (bytes.contains('\0')) {
            // Not text whatever the type said: the details stay on their own.
            addNote(tr("Binary content"));
            return;
        }
        text_->setFont(textFont_);
        text_->setPlainText(QString::fromUtf8(bytes));
        stack_->setCurrentWidget(text_);
    });
    connect(lister_, &core::ArchiveLister::listed, this,
            [this](quint64 gen, const std::vector<core::ArchiveEntry>& entries, const core::ArchiveDropped& dropped) {
                if (gen == generation_)
                    showArchive(entries, dropped);
            });
    connect(lister_, &core::ArchiveLister::failed, this, [this](quint64 gen, const QString& m) {
        if (gen == generation_)  // no bsdtar, or an archive it cannot read (encrypted, damaged)
            addNote(m);
    });
    connect(sizer_, &core::DirSizer::result, this,
            [this](quint64 gen, const QByteArray&, const core::DirSize& s) {
                if (gen != generation_)
                    return;
                QString t = tr("%n items directly inside", nullptr, static_cast<int>(s.direct));
                t += tr("\n%1 in %2 files and %3 folders%4")
                         .arg(jobfmt::bytes(s.bytes))
                         .arg(s.files)
                         .arg(s.dirs)
                         .arg(s.complete ? QString() : tr(" (still counting)"));
                addNote(t);
            });
    connect(pdf_, &PdfPages::pageReady, this, &Preview::showPicture);
    connect(pdf_, &PdfPages::note, this, &Preview::addNote);
    connect(video_, &VideoPlayer::frameReady, this, &Preview::showPicture);
    connect(video_, &VideoPlayer::note, this, &Preview::addNote);
}

void Preview::show(const core::Location& item)
{
    if (item == item_)
        return;
    clear();
    item_ = item;
    title_->setText(item.displayName());
    core::queryDetails(item, [this, gen = generation_](const core::Details& d) {
        if (gen == generation_)
            onDetails(d);
    });
}

void Preview::clear()
{
    ++generation_;
    item_ = core::Location();
    thumbUri_.clear();
    image_->cancel();
    reader_->close();
    sizer_->cancelAll();
    lister_->cancel();
    pdf_->cancel();
    video_->cancel();
    picture_->clear();
    text_->clear();
    info_->clear();
    title_->clear();
    details_.clear();
    stack_->setCurrentWidget(empty_);
}

bool Preview::step(Step step)
{
    if (pdf_->active())
        pdf_->step(step);
    else if (video_->active())
        video_->step(step);
    else
        return false;
    return true;
}

bool Preview::togglePlay()
{
    if (!video_->active())
        return false;
    video_->togglePlay();
    return true;
}

void Preview::showPicture(const QImage& image)
{
    thumbUri_.clear();  // a page or frame beats the thumbnail, whenever that lands
    picture_->setImage(image);
    stack_->setCurrentWidget(picture_);
}

void Preview::onDetails(const core::Details& d)
{
    if (!d.ok) {
        info_->setText(tr("Not readable"));
        return;
    }
    details_ = detailsText(d);
    info_->setText(details_);
    if (d.isDir) {
        sizer_->request(item_.parent(), item_.basename(), generation_, true);
        return;
    }
    if (core::isArchiveName(item_.basename()) && !item_.path().isEmpty()) {
        // The same bsdtar run the archive tab pays for; nothing decoded here.
        packedBytes_ = d.size;
        lister_->start(item_, generation_);
        return;
    }
    if (d.contentType.startsWith(QLatin1String("image/"))) {
        const QSize box = stack_->size() * devicePixelRatioF();
        image_->load(item_, static_cast<quint32>(box.width()), static_cast<quint32>(box.height()));
        return;
    }
    if (core::isTextType(d.contentType)) {
        reader_->open(item_);
        return;
    }
    if (isPdfType(d.contentType) && !item_.path().isEmpty()) {
        // The thumbnail first, as it is usually cached; the first page at
        // slot size replaces it when pdftoppm is there.
        showThumbnail(d.contentType);
        pdf_->start(item_);
        return;
    }
    if (isVideoType(d.contentType) && !item_.path().isEmpty()) {
        showThumbnail(d.contentType);
        video_->start(item_);
        return;
    }
    showThumbnail(d.contentType);
}

void Preview::showThumbnail(const QString& contentType)
{
    // Video, PDF, documents, fonts, audio with cover art: whatever tumbler
    // has a thumbnailer for, at its largest size.
    if (!thumbs_->supports(contentType))
        return;
    const QString uri = item_.uri();
    const QString path = thumbs_->request(uri, contentType, core::Thumbnailer::Flavor::XXLarge);
    if (!path.isEmpty()) {
        picture_->setImage(QImage(path));
        stack_->setCurrentWidget(picture_);
    } else {
        thumbUri_ = uri;
    }
}

void Preview::addNote(const QString& text)
{
    info_->setText(details_ + QLatin1Char('\n') + text);
}

void Preview::showImage(const core::ImageFrame& frame)
{
    // The QImage wraps a copy of the bytes it owns through the cleanup.
    auto* bytes = new QByteArray(frame.bgra);
    const QImage image(reinterpret_cast<const uchar*>(bytes->constData()), static_cast<int>(frame.width),
                       static_cast<int>(frame.height), static_cast<qsizetype>(frame.stride),
                       QImage::Format_ARGB32_Premultiplied, [](void* p) { delete static_cast<QByteArray*>(p); },
                       bytes);
    picture_->setImage(image);
    addNote(tr("Dimensions: %1 × %2").arg(frame.sourceWidth).arg(frame.sourceHeight));
    stack_->setCurrentWidget(picture_);
}

void Preview::showArchive(const std::vector<core::ArchiveEntry>& entries, const core::ArchiveDropped& dropped)
{
    addNote(archivetext::summary(core::summarizeArchive(entries), dropped, packedBytes_));
    // The first screen of the tree, no more: the archive tab shows the rest.
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const int fit = std::max(12, text_->viewport()->height() / QFontMetrics(mono).lineSpacing());
    text_->setFont(mono);
    text_->setPlainText(archivetext::tree(entries, fit));
    stack_->setCurrentWidget(text_);
}

QString Preview::detailsText(const core::Details& d) const
{
    QStringList lines;
    lines << (d.typeDescription.isEmpty() ? d.contentType : QStringLiteral("%1 (%2)").arg(d.typeDescription, d.contentType));
    if (d.symlink)
        lines << tr("Link to %1").arg(d.symlinkTarget);
    if (!d.isDir)
        lines << tr("Size: %1 (%2 bytes)").arg(jobfmt::bytes(d.size)).arg(d.size);
    lines << tr("Modified: %1").arg(when(d.modified));
    if (d.accessed > 0)
        lines << tr("Accessed: %1").arg(when(d.accessed));
    if (d.changed > 0)
        lines << tr("Changed: %1").arg(when(d.changed));
    if (const QString p = permissions(d.mode); !p.isEmpty())
        lines << tr("Permissions: %1").arg(p);
    if (!d.owner.isEmpty())
        lines << tr("Owner: %1:%2").arg(d.owner, d.group);
    return lines.join(QLatin1Char('\n'));
}
