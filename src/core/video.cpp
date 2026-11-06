#include "core/video.h"

#include "core/gobject.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <algorithm>
#include <cmath>

namespace core {

namespace {

// "30000/1001" or "25/1" or "0/0"; 0 when it makes no sense.
double parseRate(const QString& text)
{
    const int slash = text.indexOf(QLatin1Char('/'));
    bool okN = false, okD = true;
    const double num = (slash < 0 ? text : text.left(slash)).toDouble(&okN);
    const double den = slash < 0 ? 1.0 : text.mid(slash + 1).toDouble(&okD);
    if (!okN || !okD || !(den > 0) || !(num > 0))
        return 0;
    const double rate = num / den;
    return std::isfinite(rate) && rate > 0 && rate < 1000 ? rate : 0;
}

double finitePositive(const QJsonValue& v)
{
    const double d = v.isString() ? v.toString().toDouble() : v.toDouble();
    return std::isfinite(d) && d > 0 ? d : 0;
}

} // namespace

VideoInfo parseVideoInfo(const QByteArray& json)
{
    VideoInfo info;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonArray streams = root.value(QLatin1String("streams")).toArray();
    if (streams.isEmpty())
        return info;
    const QJsonObject stream = streams.first().toObject();
    const double w = finitePositive(stream.value(QLatin1String("width")));
    const double h = finitePositive(stream.value(QLatin1String("height")));
    if (w >= 1 && h >= 1 && w <= 16384 && h <= 16384) {
        info.width = static_cast<quint32>(w);
        info.height = static_cast<quint32>(h);
    }
    // The average rate reflects the file; the nominal one is the fallback.
    info.frameRate = parseRate(stream.value(QLatin1String("avg_frame_rate")).toString());
    if (info.frameRate == 0)
        info.frameRate = parseRate(stream.value(QLatin1String("r_frame_rate")).toString());
    info.duration = finitePositive(stream.value(QLatin1String("duration")));
    if (info.duration == 0)
        info.duration = finitePositive(root.value(QLatin1String("format")).toObject().value(QLatin1String("duration")));
    // Phones record sideways and say so; ffmpeg turns the frames upright.
    double rotation = 0;
    for (const QJsonValue& side : stream.value(QLatin1String("side_data_list")).toArray())
        if (const QJsonValue r = side.toObject().value(QLatin1String("rotation")); r.isDouble())
            rotation = r.toDouble();
    if (rotation == 0)
        rotation = stream.value(QLatin1String("tags")).toObject().value(QLatin1String("rotate")).toString().toDouble();
    const int quarter = static_cast<int>(std::lround(std::fmod(std::fabs(rotation), 360.0) / 90.0)) % 4;
    if (quarter == 1 || quarter == 3)
        std::swap(info.width, info.height);
    return info;
}

FrameSize fitFrame(quint32 sourceWidth, quint32 sourceHeight, quint32 boxWidth, quint32 boxHeight)
{
    FrameSize size;
    if (sourceWidth == 0 || sourceHeight == 0 || boxWidth == 0 || boxHeight == 0)
        return size;
    const double scale = std::min({1.0, static_cast<double>(boxWidth) / sourceWidth, static_cast<double>(boxHeight) / sourceHeight});
    size.width = std::max<quint32>(1, static_cast<quint32>(std::lround(sourceWidth * scale)));
    size.height = std::max<quint32>(1, static_cast<quint32>(std::lround(sourceHeight * scale)));
    return size;
}

namespace {

QString toolError(const char* tool, const GError* error)
{
    if (g_error_matches(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        return QStringLiteral("%1 is not installed (package ffmpeg)").arg(QLatin1String(tool));
    return errorMessage(error);
}

bool notInstalled(const GError* error)
{
    return g_error_matches(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT);
}

void release(GSubprocess*& process, GCancellable*& cancellable)
{
    if (process)
        g_object_unref(process);
    if (cancellable)
        g_object_unref(cancellable);
    process = nullptr;
    cancellable = nullptr;
}

} // namespace

struct FrameStreamer::ReadCall {
    QPointer<FrameStreamer> owner;
    quint64 generation = 0;
    int skip = 0;
    QByteArray buffer;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<ReadCall> call(static_cast<ReadCall*>(data));
        gsize read = 0;
        GError* error = nullptr;
        const bool ok = g_input_stream_read_all_finish(G_INPUT_STREAM(source), result, &read, &error);
        GErrorPtr gerr(error);
        if (!call->owner || isCancelled(gerr.get()))
            return;
        FrameStreamer* owner = call->owner;
        if (call->generation != owner->generation_)
            return;  // a stream that was stopped and restarted meanwhile
        if (!ok) {
            owner->pending_ = false;
            Q_EMIT owner->failed(call->generation, errorMessage(gerr.get()), false);
            return;
        }
        if (read < static_cast<gsize>(call->buffer.size())) {
            owner->pending_ = false;
            Q_EMIT owner->ended(call->generation);
            return;
        }
        if (call->skip > 0) {
            --call->skip;
            ReadCall* next = call.release();
            g_input_stream_read_all_async(owner->stdout_, next->buffer.data(), static_cast<gsize>(next->buffer.size()),
                                          G_PRIORITY_DEFAULT, owner->cancellable_, &ReadCall::onDone, next);
            return;
        }
        owner->pending_ = false;
        Q_EMIT owner->frame(call->generation, std::move(call->buffer), owner->size_.width, owner->size_.height);
    }
};

FrameStreamer::FrameStreamer(QObject* parent)
    : QObject(parent)
{
}

FrameStreamer::~FrameStreamer()
{
    stop();
}

void FrameStreamer::probe(const Location& file, quint64 generation)
{
    probe_.cancel();
    const QByteArray path = file.path();
    if (path.isEmpty()) {
        Q_EMIT failed(generation, QStringLiteral("video plays from local files only"), true);
        return;
    }
    const char* argv[] = {"ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=width,height,r_frame_rate,avg_frame_rate,duration:stream_side_data=rotation"
                          ":stream_tags=rotate:format=duration",
                          "-of", "json", path.constData(), nullptr};
    probe_.run(argv, [this, generation](const Child::Result& r) {
        if (!r.spawned) {
            Q_EMIT failed(generation, r.missing ? QStringLiteral("ffprobe is not installed (package ffmpeg)") : r.error,
                          r.missing);
            return;
        }
        if (!r.ok) {
            Q_EMIT failed(generation, r.why("ffprobe", true), false);
            return;
        }
        const VideoInfo info = parseVideoInfo(r.out);
        if (info.width == 0)
            Q_EMIT failed(generation, QStringLiteral("No video stream"), false);
        else
            Q_EMIT infoReady(generation, info);
    });
}

void FrameStreamer::start(const Location& file, double seconds, FrameSize size, double frameRate, quint64 generation)
{
    stop();
    const QByteArray path = file.path();
    if (path.isEmpty() || size.width == 0 || size.height == 0 || !(frameRate > 0)) {
        Q_EMIT failed(generation, QStringLiteral("video plays from local files only"), true);
        return;
    }
    // stderr is silenced, not piped: nobody reads it, and a chatty
    // decoder must never block on it.
    GPtr<GSubprocessLauncher> launcher(g_subprocess_launcher_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE)));
    g_subprocess_launcher_setenv(launcher.get(), "LC_ALL", "C.UTF-8", TRUE);
    const QByteArray at = QByteArray::number(std::max(0.0, seconds), 'f', 3);
    const QByteArray scale = "scale=" + QByteArray::number(size.width) + ':' + QByteArray::number(size.height);
    const QByteArray rate = QByteArray::number(frameRate, 'f', 3);
    // -ss before -i seeks the demuxer and decodes from the keyframe to
    // the exact time; -r makes the output a constant rate so one frame
    // is one tick of the caller's clock.
    const char* argv[] = {"ffmpeg", "-nostdin", "-loglevel", "error", "-ss", at.constData(), "-i", path.constData(),
                          "-an", "-sn", "-dn", "-vf", scale.constData(), "-r", rate.constData(), "-f", "rawvideo",
                          "-pix_fmt", "bgra", "-", nullptr};
    GError* error = nullptr;
    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher.get(), argv, &error);
    if (!proc) {
        GErrorPtr err(error);
        Q_EMIT failed(generation, toolError("ffmpeg", err.get()), notInstalled(err.get()));
        return;
    }
    process_ = proc;
    cancellable_ = g_cancellable_new();
    stdout_ = g_subprocess_get_stdout_pipe(proc);
    generation_ = generation;
    size_ = size;
    pending_ = false;
}

void FrameStreamer::readFrame(int skip)
{
    if (!process_ || pending_)
        return;
    pending_ = true;
    auto* call = new ReadCall{this, generation_, std::clamp(skip, 0, 30), QByteArray()};
    call->buffer.resize(static_cast<qsizetype>(size_.width) * size_.height * 4);
    g_input_stream_read_all_async(stdout_, call->buffer.data(), static_cast<gsize>(call->buffer.size()),
                                  G_PRIORITY_DEFAULT, cancellable_, &ReadCall::onDone, call);
}

void FrameStreamer::stop()
{
    if (!process_)
        return;
    g_cancellable_cancel(cancellable_);
    g_subprocess_force_exit(process_);
    release(process_, cancellable_);
    stdout_ = nullptr;
    pending_ = false;
    ++generation_;  // a read that completes late is not for anyone
}

} // namespace core
