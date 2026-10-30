#include "core/image.h"

#include "core/gobject.h"

#include <glycin.h>

#include <QPointer>

namespace core {

struct ImageLoader::Call {
    QPointer<ImageLoader> owner;
    GCancellable* cancellable = nullptr;  // the loader's at the time of the call
    GPtr<GlyLoader> loader;
    GPtr<GlyImage> image;
    quint32 maxWidth = 0;
    quint32 maxHeight = 0;

    bool stale() const { return !owner || owner->cancellable_ != cancellable; }

    static void onLoaded(GObject*, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(data));
        GError* error = nullptr;
        GlyImage* image = gly_loader_load_finish(call->loader.get(), result, &error);
        GErrorPtr err(error);
        call->image.reset(image);
        if (call->stale())
            return;
        if (!image) {
            Q_EMIT call->owner->failed(errorMessage(err.get()));
            return;
        }
        GPtr<GlyFrameRequest> request(gly_frame_request_new());
        gly_frame_request_set_scale(request.get(), call->maxWidth, call->maxHeight);
        Call* next = call.release();  // handed to the next callback
        gly_image_get_specific_frame_async(image, request.get(), next->cancellable, &Call::onFrame, next);
    }

    static void onFrame(GObject*, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Call> call(static_cast<Call*>(data));
        GError* error = nullptr;
        GPtr<GlyFrame> frame(gly_image_get_specific_frame_finish(call->image.get(), result, &error));
        GErrorPtr err(error);
        if (call->stale())
            return;
        if (!frame) {
            Q_EMIT call->owner->failed(errorMessage(err.get()));
            return;
        }
        ImageFrame out;
        out.width = gly_frame_get_width(frame.get());
        out.height = gly_frame_get_height(frame.get());
        out.stride = gly_frame_get_stride(frame.get());
        GBytes* bytes = gly_frame_get_buf_bytes(frame.get());  // borrowed
        gsize len = 0;
        const auto* p = static_cast<const char*>(g_bytes_get_data(bytes, &len));
        out.bgra = QByteArray(p, static_cast<qsizetype>(len));
        Q_EMIT call->owner->decoded(out);
    }
};

ImageLoader::ImageLoader(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ImageFrame>();
}

ImageLoader::~ImageLoader()
{
    cancel();
}

void ImageLoader::load(const Location& file, quint32 maxWidth, quint32 maxHeight)
{
    cancel();
    if (!file.isValid())
        return;
    cancellable_ = g_cancellable_new();
    auto* call = new Call;
    call->owner = this;
    call->cancellable = cancellable_;
    call->loader.reset(gly_loader_new(file.gfile()));
    call->maxWidth = maxWidth;
    call->maxHeight = maxHeight;
    gly_loader_set_accepted_memory_formats(call->loader.get(), GLY_MEMORY_SELECTION_B8G8R8A8_PREMULTIPLIED);
    gly_loader_load_async(call->loader.get(), cancellable_, &Call::onLoaded, call);
}

void ImageLoader::cancel()
{
    if (!cancellable_)
        return;
    g_cancellable_cancel(cancellable_);
    g_object_unref(cancellable_);
    cancellable_ = nullptr;
}

} // namespace core
