// Watches one directory and tells two kinds of change apart. Entries that
// came or went (created, deleted, renamed, moved) are reported coalesced
// as changed(): a burst becomes one signal after a short quiet period,
// and a steady stream still gets one every couple of seconds. An entry
// that is there and only changed (a file being written, a chmod) is
// reported by name as entryChanged(), so a download does not cost a
// listing of the folder for every block that arrives.
#pragma once

#include "core/location.h"

#include <QHash>
#include <QObject>
#include <QSet>

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
    // At most about once a second, however fast the writes come.
    // settled: whoever wrote it has closed it, or it has been quiet for
    // a while (backends that never say so); an order by size or date
    // can be put right then.
    void entryChanged(const QByteArray& name, bool settled);

private:
    struct Hook;
    void onEvent();
    void onEntryEvent(const QByteArray& name, bool done);
    void flush();
    void settle();

    GFileMonitor* monitor_ = nullptr;
    unsigned long handler_ = 0;
    Location dir_;
    QTimer* timer_;
    qint64 burstStart_ = 0;
    QHash<QByteArray, bool> pending_;  // name -> closed by its writer since
    QSet<QByteArray> unsettled_;       // reported while still being written
    QTimer* tick_;                     // the pace of entryChanged()
    QTimer* quiet_;                    // settles what nobody closed
};

} // namespace core
