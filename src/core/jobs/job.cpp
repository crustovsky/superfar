#include "core/jobs/job.h"

#include "core/gobject.h"

#include <QElapsedTimer>

namespace core {

namespace {
constexpr qint64 kReportIntervalMs = 50;
QElapsedTimer& clock()
{
    static QElapsedTimer t = [] {
        QElapsedTimer e;
        e.start();
        return e;
    }();
    return t;
}
} // namespace

Job::Job(JobKind kind, QObject* parent)
    : QObject(parent)
    , kind_(kind)
{
    qRegisterMetaType<JobProgress>();
    qRegisterMetaType<Conflict>();
    qRegisterMetaType<Failure>();
}

Job::~Job()
{
    stop();
    if (current_)
        g_object_unref(current_);
}

void Job::stop()
{
    abort();
    if (thread_.joinable())
        thread_.join();
}

QString Job::title() const
{
    switch (kind_) {
    case JobKind::Copy: return tr("Copy");
    case JobKind::Move: return tr("Move");
    case JobKind::Trash: return tr("Trash");
    case JobKind::Delete: return tr("Delete");
    case JobKind::Extract: return tr("Extract");
    case JobKind::Pack: return tr("Pack");
    case JobKind::Attributes: return tr("Set attributes");
    }
    return {};
}

void Job::start()
{
    if (thread_.joinable())
        return;
    thread_ = std::thread([this] {
        run();
        done_ = true;
        QMetaObject::invokeMethod(this, [this] {
            if (thread_.joinable())
                thread_.join();
            Q_EMIT finished();
        }, Qt::QueuedConnection);
    });
}

void Job::setPaused(bool paused)
{
    paused_ = paused;
    cv_.notify_all();
}

void Job::skip()
{
    cancelCurrent();
}

void Job::abort()
{
    aborted_ = true;
    paused_ = false;
    cancelCurrent();
    cv_.notify_all();
}

void Job::cancelCurrent()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_)
        g_cancellable_cancel(current_);
}

void Job::answerConflict(ConflictAction action, bool applyToAll)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (applyToAll)
            conflictPolicy_ = action;
        conflictAnswer_ = action;
        answered_ = true;
    }
    cv_.notify_all();
}

void Job::answerError(ErrorAction action, bool applyToAll)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (applyToAll && action != ErrorAction::Retry)
            errorPolicy_ = action;
        errorAnswer_ = action;
        answered_ = true;
    }
    cv_.notify_all();
}

bool Job::waitIfPaused()
{
    if (paused_ && !aborted_) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !paused_ || aborted_; });
    }
    return !aborted_;
}

GCancellable* Job::beginItem()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_)
        g_object_unref(current_);
    current_ = g_cancellable_new();
    if (aborted_)
        g_cancellable_cancel(current_);
    return current_;
}

ConflictAction Job::askConflict(const Conflict& c)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (conflictPolicy_ != ConflictAction::Ask)
            return conflictPolicy_;
        if (unattended_)
            return ConflictAction::Skip;  // the caller logs the skip
        answered_ = false;
    }
    Q_EMIT conflict(c);
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return answered_ || aborted_; });
    return aborted_ ? ConflictAction::Skip : conflictAnswer_;
}

ErrorAction Job::askError(const Failure& f)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (errorPolicy_ != ErrorAction::Ask)
            return errorPolicy_;
        if (unattended_)
            return ErrorAction::Skip;  // the caller logs the failure
        answered_ = false;
    }
    Q_EMIT failure(f);
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return answered_ || aborted_; });
    return aborted_ ? ErrorAction::Abort : errorAnswer_;
}

void Job::report(const JobProgress& p, bool force)
{
    const qint64 now = clock().elapsed();
    if (!force && now - lastReport_ < kReportIntervalMs)
        return;
    lastReport_ = now;
    Q_EMIT progress(p);
}

void Job::logSkip(const QString& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    log_.push_back({LogEntry::Kind::Skipped, path, QString()});
}

void Job::logFail(const QString& path, const QString& message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    log_.push_back({LogEntry::Kind::Failed, path, message});
}

} // namespace core
