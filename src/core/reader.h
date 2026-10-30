// Windowed reads of one file through a seekable GIO stream, for the F3
// viewer and the preview's text head. One read is in flight at a time;
// a request made meanwhile replaces any waiting one.
#pragma once

#include "core/location.h"

#include <QByteArray>
#include <QObject>

typedef struct _GFileInputStream GFileInputStream;
typedef struct _GCancellable GCancellable;

namespace core {

class Reader : public QObject {
    Q_OBJECT
public:
    explicit Reader(QObject* parent = nullptr);
    ~Reader() override;

    void open(const Location& file);
    void read(quint64 offset, quint32 length);
    void close();
    bool isOpen() const { return stream_ != nullptr; }

Q_SIGNALS:
    void opened(quint64 size);
    void data(quint64 offset, const QByteArray& bytes);
    void failed(const QString& message);

private:
    struct Call;
    void issue();

    GFileInputStream* stream_ = nullptr;
    GCancellable* cancellable_ = nullptr;
    bool busy_ = false;
    bool wanted_ = false;
    quint64 wantOffset_ = 0;
    quint32 wantLength_ = 0;
};

} // namespace core
