// Trash or permanently delete a set of locations. Trash is one GIO call
// per item; when the backend refuses (another volume without a trash
// folder, a remote share) the error offers permanent deletion. Permanent
// deletion walks directories here, children first.
#pragma once

#include "core/jobs/job.h"

namespace core {

class RemoveJob : public Job {
    Q_OBJECT
public:
    RemoveJob(std::vector<Location> sources, bool permanent, QObject* parent = nullptr);

    ~RemoveJob() override { stop(); }

protected:
    void run() override;

private:
    struct Impl;
    friend struct Impl;

    std::vector<Location> sources_;
    bool permanent_;
};

} // namespace core
