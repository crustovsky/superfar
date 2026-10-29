// Per-entry metadata, asked for only when a row is on screen (rule 1).
// Results carry the generation so a reply for a folder that is no longer
// shown is dropped by the receiver.
#pragma once

#include "core/location.h"

#include <QObject>
#include <QStringList>

typedef struct _GCancellable GCancellable;

namespace core {

struct Metadata {
    bool ok = false;
    quint64 size = 0;
    qint64 modified = 0;    // seconds since the epoch
    QString contentType;    // fast content type, from the name only
    QStringList iconNames;  // theme icon names, best first
};

class MetadataQuery : public QObject {
    Q_OBJECT
public:
    explicit MetadataQuery(QObject* parent = nullptr);
    ~MetadataQuery() override;

    void request(const Location& dir, const QByteArray& name, quint64 generation);
    // Drops every request in flight.
    void cancelAll();

Q_SIGNALS:
    void result(quint64 generation, const QByteArray& name, const core::Metadata& meta);

private:
    struct Request;
    GCancellable* cancellable_ = nullptr;
};

} // namespace core
