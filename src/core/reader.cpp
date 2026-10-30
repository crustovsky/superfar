#include "core/reader.h"

#include "core/gobject.h"

#include <QPointer>

namespace core {

struct Reader::Call {
    QPointer<Reader> owner;
    quint64 offset = 0;
    QByteArray buffer;

    static void onOpened(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(data));
        GError* error = nullptr;
        GFileInputStream* stream = g_file_read_finish(G_FILE(source), result, &error);
        GErrorPtr err(error);
        if (!call->owner || isCancelled(err.get())) {
            if (stream)
                g_object_unref(stream);
            return;
        }
        if (!stream) {
            Q_EMIT call->owner->failed(errorMessage(err.get()));
            return;
        }
        Reader* r = call->owner;
        r->stream_ = stream;
        g_file_input_stream_query_info_async(stream, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_PRIORITY_DEFAULT,
                                             r->cancellable_, &Call::onSize, call.release());
    }

    static void onSize(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(data));
        GError* error = nullptr;
        GPtr<GFileInfo> info(g_file_input_stream_query_info_finish(G_FILE_INPUT_STREAM(source), result, &error));
        GErrorPtr err(error);
        if (!call->owner || isCancelled(err.get()))
            return;
        const quint64 size = info ? static_cast<quint64>(g_file_info_get_size(info.get())) : 0;
        Q_EMIT call->owner->opened(size);
        call->owner->issue();
    }

    static void onRead(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(data));
        GError* error = nullptr;
        gsize got = 0;
        g_input_stream_read_all_finish(G_INPUT_STREAM(source), result, &got, &error);
        GErrorPtr err(error);
        if (!call->owner || isCancelled(err.get()))
            return;
        Reader* r = call->owner;
        r->busy_ = false;
        if (err) {
            Q_EMIT r->failed(errorMessage(err.get()));
            return;
        }
        call->buffer.resize(static_cast<qsizetype>(got));
        Q_EMIT r->data(call->offset, call->buffer);
        r->issue();
    }
};

Reader::Reader(QObject* parent)
    : QObject(parent)
{
}

Reader::~Reader()
{
    close();
}

void Reader::open(const Location& file)
{
    close();
    if (!file.isValid())
        return;
    cancellable_ = g_cancellable_new();
    auto* call = new Call{this, 0, {}};
    g_file_read_async(file.gfile(), G_PRIORITY_DEFAULT, cancellable_, &Call::onOpened, call);
}

void Reader::read(quint64 offset, quint32 length)
{
    wanted_ = true;
    wantOffset_ = offset;
    wantLength_ = length;
    issue();
}

void Reader::issue()
{
    if (!stream_ || busy_ || !wanted_)
        return;
    wanted_ = false;
    busy_ = true;
    GError* error = nullptr;
    if (!g_seekable_seek(G_SEEKABLE(stream_), static_cast<goffset>(wantOffset_), G_SEEK_SET, cancellable_, &error)) {
        GErrorPtr err(error);
        busy_ = false;
        Q_EMIT failed(errorMessage(err.get()));
        return;
    }
    auto* call = new Call{this, wantOffset_, QByteArray(static_cast<qsizetype>(wantLength_), Qt::Uninitialized)};
    g_input_stream_read_all_async(G_INPUT_STREAM(stream_), call->buffer.data(), wantLength_,
                                  G_PRIORITY_DEFAULT, cancellable_, &Call::onRead, call);
}

void Reader::close()
{
    if (cancellable_) {
        g_cancellable_cancel(cancellable_);
        g_object_unref(cancellable_);
        cancellable_ = nullptr;
    }
    if (stream_) {
        g_input_stream_close_async(G_INPUT_STREAM(stream_), G_PRIORITY_DEFAULT, nullptr, nullptr, nullptr);
        g_object_unref(stream_);
        stream_ = nullptr;
    }
    busy_ = false;
    wanted_ = false;
}

} // namespace core
