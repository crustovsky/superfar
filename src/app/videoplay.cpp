#include "videoplay.h"

#include <QEvent>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kMaxRate = 30;    // a preview needs no more frames than this
constexpr double kSeekStep = 10;   // seconds for Prev and Next
constexpr double kEndMargin = 0.2; // Last shows a frame this far before the end
} // namespace

VideoPlayer::VideoPlayer(QWidget* slot, PageStrip* strip, QObject* parent)
    : QObject(parent)
    , slot_(slot)
    , strip_(strip)
    , streamer_(new core::FrameStreamer(this))
    , timer_(new QTimer(this))
{
    connect(strip_, &PageStrip::stepRequested, this, &VideoPlayer::step);
    connect(strip_, &PageStrip::playToggled, this, &VideoPlayer::togglePlay);
    connect(strip_, &PageStrip::seekRequested, this, [this](double fraction) {
        if (active() && info_.duration > 0)
            seekTo(fraction * info_.duration);
    });
    connect(streamer_, &core::FrameStreamer::infoReady, this, &VideoPlayer::onInfo);
    connect(streamer_, &core::FrameStreamer::frame, this, &VideoPlayer::onFrame);
    connect(streamer_, &core::FrameStreamer::ended, this, &VideoPlayer::onEnded);
    connect(streamer_, &core::FrameStreamer::failed, this, &VideoPlayer::onFailed);
    connect(timer_, &QTimer::timeout, this, &VideoPlayer::tick);
    slot_->installEventFilter(this);
}

VideoPlayer::~VideoPlayer()
{
    // Not cancel(): the strip, an older sibling, may already be gone.
    streamer_->stop();
}

void VideoPlayer::start(const core::Location& file)
{
    cancel();
    file_ = file;
    streamer_->probe(file_, generation_);
}

void VideoPlayer::cancel()
{
    ++generation_;
    timer_->stop();
    streamer_->stop();
    file_ = core::Location();
    info_ = core::VideoInfo();
    playing_ = false;
    ended_ = false;
    consumed_ = 0;
    start_ = 0;
    strip_->clear();
}

bool VideoPlayer::eventFilter(QObject* watched, QEvent* event)
{
    // A hidden slot (pane hidden, preview moved) plays to nobody.
    if (watched == slot_ && event->type() == QEvent::Hide && playing_)
        pause();
    return QObject::eventFilter(watched, event);
}

double VideoPlayer::position() const
{
    return rate_ > 0 ? start_ + static_cast<double>(consumed_) / rate_ : start_;
}

void VideoPlayer::onInfo(quint64 generation, const core::VideoInfo& info)
{
    if (generation != generation_)
        return;
    info_ = info;
    rate_ = info.frameRate > 0 ? std::min(info.frameRate, kMaxRate) : 25;
    QString line = tr("%1 × %2").arg(info.width).arg(info.height);
    if (info.frameRate > 0)
        line += tr(", %1 fps").arg(info.frameRate, 0, 'g', 4);
    if (info.duration > 0)
        line += tr(", %1 s").arg(info.duration, 0, 'f', 1);
    Q_EMIT note(line);
    restart(0, false);  // the first frame, still: play is a key away
}

void VideoPlayer::restart(double at, bool play)
{
    if (!active())
        return;
    timer_->stop();
    const QSize box = slot_->size() * slot_->devicePixelRatioF();
    const core::FrameSize size = core::fitFrame(info_.width, info_.height,
                                                static_cast<quint32>(std::max(1, box.width())),
                                                static_cast<quint32>(std::max(1, box.height())));
    at = std::max(0.0, at);
    if (info_.duration > 0)
        at = std::min(at, std::max(0.0, info_.duration - kEndMargin));
    streamer_->start(file_, at, size, rate_, generation_);
    start_ = at;
    consumed_ = 0;
    ended_ = false;
    playing_ = play;
    clock_.start();
    if (play)
        timer_->start(static_cast<int>(std::max(1.0, 1000 / rate_)));
    // The first frame right away: paused, this is the one that stays.
    consumed_ = 1;
    streamer_->readFrame(0);
    showTime();
}

void VideoPlayer::tick()
{
    if (slot_->window()->isMinimized()) {
        pause();
        return;
    }
    // Frames due by the clock; the pipe holds what we have not read, so
    // falling behind means skipping, never a queue of pictures.
    const qint64 due = static_cast<qint64>(std::floor(clock_.elapsed() * rate_ / 1000.0));
    const qint64 behind = due - consumed_;
    if (behind <= 0 || streamer_->reading())
        return;
    consumed_ += behind;
    streamer_->readFrame(static_cast<int>(behind - 1));
}

void VideoPlayer::onFrame(quint64 generation, const QByteArray& bgra, quint32 width, quint32 height)
{
    if (generation != generation_)
        return;
    // The QImage wraps a copy of the bytes it owns through the cleanup.
    auto* bytes = new QByteArray(bgra);
    const QImage image(reinterpret_cast<const uchar*>(bytes->constData()), static_cast<int>(width),
                       static_cast<int>(height), static_cast<qsizetype>(width) * 4, QImage::Format_RGB32,
                       [](void* p) { delete static_cast<QByteArray*>(p); }, bytes);
    Q_EMIT frameReady(image);
    showTime();
}

void VideoPlayer::onEnded(quint64 generation)
{
    if (generation != generation_)
        return;
    timer_->stop();
    playing_ = false;
    ended_ = true;
    if (info_.duration > 0) {
        start_ = info_.duration;
        consumed_ = 0;
    }
    showTime();
}

void VideoPlayer::onFailed(quint64 generation, const QString& message, bool missing)
{
    if (generation != generation_ || missing)
        return;  // without ffmpeg the thumbnail stands and nothing is said
    timer_->stop();
    playing_ = false;
    Q_EMIT note(message);
}

void VideoPlayer::pause()
{
    timer_->stop();
    playing_ = false;
    showTime();
}

void VideoPlayer::togglePlay()
{
    if (!active())
        return;
    if (playing_) {
        pause();
        return;
    }
    if (ended_) {
        restart(0, true);
        return;
    }
    // Resume where the stream waits: the clock starts over from here.
    start_ = position();
    consumed_ = 0;
    clock_.start();
    playing_ = true;
    timer_->start(static_cast<int>(std::max(1.0, 1000 / rate_)));
    showTime();
}

void VideoPlayer::step(Step step)
{
    if (!active())
        return;
    switch (step) {
    case Step::First: restart(0, playing_); return;
    case Step::Prev: seekTo(position() - kSeekStep); return;
    case Step::Next: seekTo(position() + kSeekStep); return;
    case Step::Last:
        if (info_.duration > 0)
            restart(info_.duration - kEndMargin, false);
        return;
    }
}

void VideoPlayer::seekTo(double seconds)
{
    restart(seconds, playing_);
}

void VideoPlayer::showTime()
{
    strip_->setTime(position(), info_.duration, playing_);
}
