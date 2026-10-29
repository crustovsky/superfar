#include "core/monitor.h"

#include "core/gobject.h"

#include <QDateTime>
#include <QTimer>

namespace core {

namespace {
constexpr int kQuietMs = 400;
constexpr qint64 kMaxLatencyMs = 2000;
} // namespace

struct Monitor::Hook {
    static void onChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer data)
    {
        static_cast<Monitor*>(data)->onEvent();
    }
};

Monitor::Monitor(QObject* parent)
    : QObject(parent)
    , timer_(new QTimer(this))
{
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &Monitor::changed);
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
        return;  // backends without monitoring: the tab reloads after jobs instead
    handler_ = g_signal_connect(monitor_, "changed", G_CALLBACK(&Hook::onChanged), this);
}

void Monitor::stop()
{
    timer_->stop();
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

} // namespace core
