// Copy or move a set of locations into a destination folder. Files go
// through g_file_copy or g_file_move with a progress callback, so
// reflinks, splice, metadata and server-side remote copies come for free;
// directories are walked here. A move tries a rename first and falls back
// to copy and delete when GIO says it would have to recurse.
#pragma once

#include "core/jobs/job.h"

namespace core {

class TransferJob : public Job {
    Q_OBJECT
public:
    TransferJob(std::vector<Location> sources, Location destination, bool move,
                QObject* parent = nullptr);

    ~TransferJob() override { stop(); }

protected:
    void run() override;

private:
    struct Impl;
    friend struct Impl;

    std::vector<Location> sources_;
    Location dest_;
    bool move_;
};

} // namespace core
