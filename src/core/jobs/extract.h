// Extract members of an archive into a folder with bsdtar in a child
// process. Members are paths inside the archive; stripComponents drops
// that many leading folders so what sits in the current archive folder
// lands directly in the destination.
#pragma once

#include "core/jobs/job.h"

namespace core {

class ExtractJob : public Job {
    Q_OBJECT
public:
    ExtractJob(Location archive, std::vector<QByteArray> members, Location destination, int stripComponents,
               QObject* parent = nullptr);
    ~ExtractJob() override { stop(); }

protected:
    void run() override;

private:
    Location archive_;
    std::vector<QByteArray> members_;
    Location dest_;
    int strip_;
};

} // namespace core
