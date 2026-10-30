#include "preview.h"

#include "elidedlabel.h"
#include "jobs/format.h"

#include <QDateTime>
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
    if (mode == 0)
        return {};
    QString s;
    const char* bits = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        s += (mode & (0400 >> i)) ? QLatin1Char(bits[i]) : QLatin1Char('-');
    return QStringLiteral("%1 (%2)").arg(s).arg(mode & 07777, 4, 8, QLatin1Char('0'));
}
} // namespace

Preview::Preview(core::DirSizePool* sizes, QWidget* parent)
    : QWidget(parent)
    , image_(new core::ImageLoader(this))
    , reader_(new core::Reader(this))
    , sizer_(new core::DirSizer(sizes, this))
    , title_(new ElidedLabel(this))
    , info_(new QLabel(this))
    , stack_(new QStackedWidget(this))
    , empty_(new QWidget(this))
    , picture_(new ImageView(this))
    , text_(new QPlainTextEdit(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(title_);
    layout->addWidget(info_);
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

    connect(image_, &core::ImageLoader::decoded, this, &Preview::showImage);
    connect(image_, &core::ImageLoader::failed, this, [this](const QString& m) {
        info_->setText(details_ + QLatin1Char('\n') + tr("Could not decode: %1").arg(m));
    });
    connect(reader_, &core::Reader::opened, this, [this](quint64) { reader_->read(0, kTextCap); });
    connect(reader_, &core::Reader::data, this, [this](quint64, const QByteArray& bytes) {
        text_->setPlainText(QString::fromUtf8(bytes));
        stack_->setCurrentWidget(text_);
        reader_->close();
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
                info_->setText(details_ + QLatin1Char('\n') + t);
            });
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
    image_->cancel();
    reader_->close();
    sizer_->cancelAll();
    picture_->clear();
    text_->clear();
    info_->clear();
    title_->clear();
    details_.clear();
    stack_->setCurrentWidget(empty_);
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
    if (d.contentType.startsWith(QLatin1String("image/"))) {
        const QSize box = stack_->size() * devicePixelRatioF();
        image_->load(item_, static_cast<quint32>(box.width()), static_cast<quint32>(box.height()));
        return;
    }
    const QByteArray type = d.contentType.toUtf8();
    if (d.contentType.startsWith(QLatin1String("text/")) || type.contains("xml") || type.contains("json")
        || type.contains("script") || type == "application/x-empty")
        reader_->open(item_);
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
    info_->setText(details_ + QLatin1Char('\n')
                   + tr("Dimensions: %1 × %2").arg(frame.sourceWidth).arg(frame.sourceHeight));
    stack_->setCurrentWidget(picture_);
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
