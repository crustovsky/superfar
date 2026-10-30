// Image decoding for the preview through libglycin, which runs the
// decoder in its own sandboxed process (rule 8). The frame comes back
// scaled to fit the requested box, as premultiplied BGRA bytes the app
// wraps in a QImage without copying again.
#pragma once

#include "core/location.h"

#include <QByteArray>
#include <QObject>

typedef struct _GCancellable GCancellable;

namespace core {

struct ImageFrame {
    quint32 width = 0;   // of the bytes below, reduced to about the box
    quint32 height = 0;
    quint32 stride = 0;
    quint32 sourceWidth = 0;  // what the file holds
    quint32 sourceHeight = 0;
    QByteArray bgra;  // premultiplied, stride bytes per row
};

class ImageLoader : public QObject {
    Q_OBJECT
public:
    explicit ImageLoader(QObject* parent = nullptr);
    ~ImageLoader() override;

    // Cancels any decode in flight and starts a new one. Most decoders
    // return the full frame whatever the box, so a frame more than twice
    // the box is box-averaged down here before it crosses to the app,
    // which bounds the bytes a huge photo costs (rule 16).
    void load(const Location& file, quint32 maxWidth, quint32 maxHeight);
    void cancel();

Q_SIGNALS:
    void decoded(const core::ImageFrame& frame);
    void failed(const QString& message);

private:
    struct Call;
    GCancellable* cancellable_ = nullptr;
};

} // namespace core

Q_DECLARE_METATYPE(core::ImageFrame)
