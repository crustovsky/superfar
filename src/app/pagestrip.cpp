#include "pagestrip.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <cmath>

namespace {

QString clock(double seconds)
{
    const qint64 s = static_cast<qint64>(std::floor(std::max(0.0, seconds)));
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

constexpr int kSliderRange = 1000;

} // namespace

PageStrip::PageStrip(QWidget* parent)
    : QWidget(parent)
    , layout_(new QHBoxLayout(this))
    , label_(new QLabel(this))
    , slider_(new QSlider(Qt::Horizontal, this))
    , seekTimer_(new QTimer(this))
{
    layout_->setContentsMargins(4, 0, 4, 2);
    layout_->setSpacing(2);
    play_ = button("media-playback-start", QStyle::SP_MediaPlay);
    first_ = button("go-first", QStyle::SP_MediaSkipBackward);
    prev_ = button("go-previous", QStyle::SP_MediaSeekBackward);
    next_ = button("go-next", QStyle::SP_MediaSeekForward);
    last_ = button("go-last", QStyle::SP_MediaSkipForward);
    layout_->addStretch(1);
    layout_->addWidget(play_);
    layout_->addWidget(first_);
    layout_->addWidget(prev_);
    layout_->addWidget(slider_, 1);
    layout_->addWidget(label_);
    layout_->addWidget(next_);
    layout_->addWidget(last_);
    layout_->addStretch(1);
    label_->setContentsMargins(6, 0, 6, 0);
    slider_->setRange(0, kSliderRange);
    slider_->setFocusPolicy(Qt::NoFocus);
    slider_->setMinimumWidth(60);
    seekTimer_->setSingleShot(true);
    seekTimer_->setInterval(80);
    setFocusPolicy(Qt::NoFocus);
    connect(first_, &QToolButton::clicked, this, [this] { Q_EMIT stepRequested(Step::First); });
    connect(prev_, &QToolButton::clicked, this, [this] { Q_EMIT stepRequested(Step::Prev); });
    connect(next_, &QToolButton::clicked, this, [this] { Q_EMIT stepRequested(Step::Next); });
    connect(last_, &QToolButton::clicked, this, [this] { Q_EMIT stepRequested(Step::Last); });
    connect(play_, &QToolButton::clicked, this, &PageStrip::playToggled);
    // A drag seeks once the hand rests for a moment, a click seeks at once.
    connect(slider_, &QSlider::sliderMoved, seekTimer_, qOverload<>(&QTimer::start));
    connect(slider_, &QSlider::sliderReleased, this, [this] {
        seekTimer_->stop();
        Q_EMIT seekRequested(static_cast<double>(slider_->value()) / kSliderRange);
    });
    connect(slider_, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove)  // page steps and clicks on the groove
            QTimer::singleShot(0, this, [this] { Q_EMIT seekRequested(static_cast<double>(slider_->value()) / kSliderRange); });
    });
    connect(seekTimer_, &QTimer::timeout, this, [this] {
        Q_EMIT seekRequested(static_cast<double>(slider_->value()) / kSliderRange);
    });
    hide();
}

QToolButton* PageStrip::button(const char* themeName, int fallback)
{
    auto* b = new QToolButton(this);
    b->setIcon(QIcon::fromTheme(QLatin1String(themeName), style()->standardIcon(static_cast<QStyle::StandardPixmap>(fallback))));
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

void PageStrip::setPages(int page, int count)
{
    if (count < 2) {
        hide();
        return;
    }
    play_->hide();
    slider_->hide();
    layout_->setStretch(0, 1);
    layout_->setStretch(layout_->count() - 1, 1);
    label_->setText(tr("Page %1 / %2").arg(page).arg(count));
    first_->setToolTip(tr("First page"));
    prev_->setToolTip(tr("Previous page"));
    next_->setToolTip(tr("Next page"));
    last_->setToolTip(tr("Last page"));
    first_->setEnabled(page > 1);
    prev_->setEnabled(page > 1);
    next_->setEnabled(page < count);
    last_->setEnabled(page < count);
    show();
}

void PageStrip::setTime(double position, double total, bool playing)
{
    play_->setIcon(playing ? QIcon::fromTheme(QStringLiteral("media-playback-pause"), style()->standardIcon(QStyle::SP_MediaPause))
                           : QIcon::fromTheme(QStringLiteral("media-playback-start"), style()->standardIcon(QStyle::SP_MediaPlay)));
    play_->setToolTip(playing ? tr("Pause") : tr("Play"));
    play_->show();
    layout_->setStretch(0, 0);
    layout_->setStretch(layout_->count() - 1, 0);
    if (total > 0) {
        slider_->show();
        if (!slider_->isSliderDown()) {
            const QSignalBlocker quiet(slider_);
            slider_->setValue(static_cast<int>(std::lround(std::clamp(position / total, 0.0, 1.0) * kSliderRange)));
        }
        label_->setText(QStringLiteral("%1 / %2").arg(clock(position), clock(total)));
    } else {
        slider_->hide();
        label_->setText(clock(position));
    }
    first_->setToolTip(tr("Start"));
    prev_->setToolTip(tr("Back 10 seconds"));
    next_->setToolTip(tr("Forward 10 seconds"));
    last_->setToolTip(tr("End"));
    for (QToolButton* b : {first_, prev_, next_, last_})
        b->setEnabled(true);
    show();
}

void PageStrip::clear()
{
    seekTimer_->stop();
    hide();
}
