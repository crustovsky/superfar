// Watches one directory and reports changes coalesced: a burst of events
// becomes one signal after a short quiet period, and a steady stream
// still gets one signal every couple of seconds.
#pragma once

#include "core/location.h"

#include <QObject>

class QTimer;
typedef struct _GFileMonitor GFileMonitor;

namespace core {

class Monitor : public QObject {
    Q_OBJECT
public:
    explicit Monitor(QObject* parent = nullptr);
    ~Monitor() override;

    void watch(const Location& dir);
    void stop();
    bool active() const { return monitor_ != nullptr; }

Q_SIGNALS:
    void changed();

private:
    struct Hook;
    void onEvent();

    GFileMonitor* monitor_ = nullptr;
    unsigned long handler_ = 0;
    QTimer* timer_;
    qint64 burstStart_ = 0;
};

} // namespace core
