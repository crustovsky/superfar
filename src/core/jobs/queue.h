// The order jobs start in. A job added the usual way starts at once,
// beside whatever runs. A queued one starts when every job added before
// it has ended, by finishing or by abort, or when it is told to stop
// waiting. Three large copies to one disk then run one after another
// instead of fighting over it. Nothing is reordered and nothing
// survives the window. The queue watches jobs; it does not own them.
#pragma once

#include "core/jobs/job.h"

#include <vector>

namespace core {

class JobQueue : public QObject {
    Q_OBJECT
public:
    explicit JobQueue(QObject* parent = nullptr);

    void add(Job* job, bool queued);
    void startNow(Job* job);
    // Jobs running or waiting.
    int count() const { return static_cast<int>(jobs_.size()); }
    bool busy() const { return !jobs_.empty(); }

private:
    void remove(QObject* job);

    std::vector<Job*> jobs_;  // in the order they were added
};

} // namespace core
