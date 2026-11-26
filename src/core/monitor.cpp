#include "core/monitor.h"

#include "core/gobject.h"

#include <QDateTime>
#include <QTimer>

#include <utility>

namespace core {

namespace {
constexpr int kQuietMs = 400;
constexpr qint64 kMaxLatencyMs = 2000;
// GIO reports a file that keeps growing about every 800 ms: a second
// between reports of entries keeps the Size cell ticking calmly.
constexpr int kEntryPaceMs = 1000;
constexpr int kSettleMs = 3000;
} // namespace

struct Monitor::Hook {
    static void onChanged(GFileMonitor*, GFile* file, GFile*, GFileMonitorEvent event, gpointer data)
    {
        auto* self = static_cast<Monitor*>(data);
        const bool inPlace = event == G_FILE_MONITOR_EVENT_CHANGED || event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT
            || event == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED;
        // The folder itself is no row: its own changes list it again.
        if (!inPlace || !file || g_file_equal(file, self->dir_.gfile())) {
            self->onEvent();
            return;
        }
        GStr name(g_file_get_basename(file));
        self->onEntryEvent(QByteArray(name.get()), event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT);
    }
};

Monitor::Monitor(QObject* parent)
    : QObject(parent)
    , timer_(new QTimer(this))
    , tick_(new QTimer(this))
    , quiet_(new QTimer(this))
{
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &Monitor::changed);
    tick_->setSingleShot(true);
    tick_->setInterval(kEntryPaceMs);
    connect(tick_, &QTimer::timeout, this, &Monitor::flush);
    quiet_->setSingleShot(true);
    quiet_->setInterval(kSettleMs);
    connect(quiet_, &QTimer::timeout, this, &Monitor::settle);
}

Monitor::~Monitor()
{
    stop();
}

void Monitor::watch(const Location& dir)
{
    stop();
    if (!dir.isValid())
        return;
    monitor_ = g_file_monitor_directory(dir.gfile(), G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
    if (!monitor_)
        return;
    dir_ = dir;  // backends without monitoring: the tab reloads after jobs instead
    handler_ = g_signal_connect(monitor_, "changed", G_CALLBACK(&Hook::onChanged), this);
}

void Monitor::stop()
{
    timer_->stop();
    tick_->stop();
    quiet_->stop();
    pending_.clear();
    unsettled_.clear();
    if (!monitor_)
        return;
    g_signal_handler_disconnect(monitor_, handler_);
    g_file_monitor_cancel(monitor_);
    g_object_unref(monitor_);
    monitor_ = nullptr;
    handler_ = 0;
}

void Monitor::onEvent()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!timer_->isActive()) {
        burstStart_ = now;
        timer_->start(kQuietMs);
    } else if (now - burstStart_ > kMaxLatencyMs) {
        timer_->stop();
        burstStart_ = now;
        Q_EMIT changed();
    } else {
        timer_->start(kQuietMs);
    }
}

void Monitor::onEntryEvent(const QByteArray& name, bool done)
{
    pending_.insert(name, done);  // the last word counts: written again after a close is not settled
    if (!tick_->isActive()) {
        flush();  // the first at once, the rest at the pace
        tick_->start();
    }
}

void Monitor::flush()
{
    if (pending_.isEmpty())
        return;
    const QHash<QByteArray, bool> batch = std::exchange(pending_, {});
    for (auto it = batch.cbegin(); it != batch.cend(); ++it) {
        if (it.value())
            unsettled_.remove(it.key());
        else
            unsettled_.insert(it.key());
        Q_EMIT entryChanged(it.key(), it.value());
    }
    tick_->start();
    if (unsettled_.isEmpty())
        quiet_->stop();
    else
        quiet_->start();
}

void Monitor::settle()
{
    const QSet<QByteArray> names = std::exchange(unsettled_, {});
    for (const QByteArray& name : names)
        Q_EMIT entryChanged(name, true);
}

} // namespace core
