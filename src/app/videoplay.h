// The preview's video side: probes the file, has core's FrameStreamer
// decode frames at the slot's size in a child, paces them with its own
// clock, and drives the strip as a transport: play, pause, a playhead,
// seeks. No audio by design, no decoder in the process (rule 8), and
// nothing runs while the slot is hidden (rule 16).
#pragma once

#include "core/location.h"
#include "core/video.h"
#include "pagestrip.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>

class QTimer;
class QWidget;

class VideoPlayer : public QObject {
    Q_OBJECT
public:
    // slot: the widget whose size frames are decoded to fit; strip: the
    // preview's, shared with the PDF side, driven while a video is open.
    VideoPlayer(QWidget* slot, PageStrip* strip, QObject* parent);
    ~VideoPlayer() override;

    // Probes, then plays from the start; frames come through frameReady.
    void start(const core::Location& file);
    // Kills the child and hides the strip.
    void cancel();

    bool active() const { return info_.width > 0; }  // takes the step keys
    bool playing() const { return playing_; }
    void togglePlay();
    void step(Step step);        // start, back 10 s, forward 10 s, end
    void seekTo(double seconds);
    double position() const;

Q_SIGNALS:
    void frameReady(const QImage& image);
    // A line for the info block: dimensions and length, or why there is none.
    void note(const QString& text);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onInfo(quint64 generation, const core::VideoInfo& info);
    void onFrame(quint64 generation, const QByteArray& bgra, quint32 width, quint32 height);
    void onEnded(quint64 generation);
    void onFailed(quint64 generation, const QString& message, bool missing);
    void restart(double at, bool play);  // a new child at that time
    void pause();
    void tick();
    void showTime();

    QWidget* slot_;
    PageStrip* strip_;
    core::FrameStreamer* streamer_;
    core::Location file_;
    quint64 generation_ = 0;
    core::VideoInfo info_;
    double rate_ = 0;      // frames per second as decoded (source rate, capped)
    double start_ = 0;     // the stream's first frame, in seconds
    qint64 consumed_ = 0;  // frames taken from the stream since start_
    QElapsedTimer clock_;  // runs while playing, from start_
    QTimer* timer_;
    bool playing_ = false;
    bool ended_ = false;
};
