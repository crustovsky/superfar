#include "core/jobs/queue.h"

#include <algorithm>

namespace core {

JobQueue::JobQueue(QObject* parent)
    : QObject(parent)
{
}

void JobQueue::add(Job* job, bool queued)
{
    jobs_.push_back(job);
    connect(job, &Job::finished, this, [this, job] { remove(job); });
    connect(job, &QObject::destroyed, this, &JobQueue::remove);
    // With nothing ahead of it a queued job has nothing to wait for.
    if (!queued || jobs_.front() == job)
        job->start();
}

void JobQueue::startNow(Job* job)
{
    job->start();
}

void JobQueue::remove(QObject* job)
{
    const auto it = std::find(jobs_.begin(), jobs_.end(), job);
    if (it == jobs_.end())
        return;
    jobs_.erase(it);
    // Whatever is first now has seen everything before it end. A
    // paused job has not ended: what is behind it stays held.
    if (!jobs_.empty())
        jobs_.front()->start();
}

} // namespace core
