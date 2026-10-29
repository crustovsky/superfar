// Free space of the filesystem holding a location, for the status line.
#pragma once

#include "core/location.h"

#include <QObject>

namespace core {

class SpaceQuery : public QObject {
    Q_OBJECT
public:
    explicit SpaceQuery(QObject* parent = nullptr);

    void request(const Location& dir, quint64 generation);

Q_SIGNALS:
    void result(quint64 generation, bool ok, quint64 freeBytes);

private:
    struct Request;
};

} // namespace core
