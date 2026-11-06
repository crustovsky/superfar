// Video frames for the preview without a decoder in the process (rule
// 8): ffprobe says what the stream is, ffmpeg decodes in a child and
// writes raw BGRA frames at the slot's size to its stdout, one fixed
// size each, and the app reads them one at a time at the stream's
// pace. Reading no frames holds ffmpeg on the full pipe, so pause costs
// nothing; seeking restarts the child at the new time.
#pragma once

#include "core/child.h"
#include "core/location.h"

#include <QByteArray>
#include <QObject>
#include <QString>

typedef struct _GCancellable GCancellable;
typedef struct _GSubprocess GSubprocess;
typedef struct _GInputStream GInputStream;

namespace core {

struct VideoInfo {
    double duration = 0;   // seconds, 0 when the container does not say
    double frameRate = 0;  // frames per second, 0 when unknown
    quint32 width = 0;     // as displayed: rotation metadata applied
    quint32 height = 0;
};

// Parses "ffprobe -of json" output with the stream and format entries
// the streamer asks for. Exposed for tests.
VideoInfo parseVideoInfo(const QByteArray& json);

// The size the frames are decoded to: the source fitted into the box,
// never upscaled; at least 1x1.
struct FrameSize {
    quint32 width = 1;
    quint32 height = 1;
};
FrameSize fitFrame(quint32 sourceWidth, quint32 sourceHeight, quint32 boxWidth, quint32 boxHeight);

class FrameStreamer : public QObject {
    Q_OBJECT
public:
    explicit FrameStreamer(QObject* parent = nullptr);
    ~FrameStreamer() override;

    // Local files only: the tools need a path.
    void probe(const Location& file, quint64 generation);
    // Starts decoding at the given second, frames of the given size and
    // rate (frames above the source rate are duplicates, below it are
    // dropped by ffmpeg). Any earlier stream stops.
    void start(const Location& file, double seconds, FrameSize size, double frameRate, quint64 generation);
    // Reads skip + 1 frames and delivers the last: skipping is how a
    // caller that fell behind catches up without queueing pictures.
    // Ignored while a read is in flight.
    void readFrame(int skip = 0);
    bool reading() const { return pending_; }
    void stop();  // kills the child; probe is left alone

Q_SIGNALS:
    void infoReady(quint64 generation, core::VideoInfo info);
    // bgra holds width * height * 4 bytes, premultiplied as ffmpeg
    // writes them (opaque video: the same thing).
    void frame(quint64 generation, QByteArray bgra, quint32 width, quint32 height);
    void ended(quint64 generation);  // the stream ran out
    // missing is true when the tool is not installed: the caller keeps
    // quiet then, the thumbnail already says what it can.
    void failed(quint64 generation, const QString& message, bool missing);

private:
    struct ReadCall;

    Child probe_;  // ffprobe, run to its end
    // The stream is read frame by frame while ffmpeg runs, so it is
    // held here rather than in a Child.
    GCancellable* cancellable_ = nullptr;
    GSubprocess* process_ = nullptr;
    GInputStream* stdout_ = nullptr;  // owned by process_
    quint64 generation_ = 0;
    FrameSize size_;
    bool pending_ = false;
};

} // namespace core

Q_DECLARE_METATYPE(core::VideoInfo)
