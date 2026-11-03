// Pack entries of one folder into a new archive with bsdtar in a child
// process; the archive's extension picks the format (-a), options go to
// bsdtar's --options. Names are written NUL-separated to a list file,
// so a name starting with a dash is a name. The scan walks the items
// for the totals; bsdtar's -v on stderr then reports each member.
#pragma once

#include "core/jobs/job.h"

#include <QByteArray>
#include <vector>

namespace core {

class PackJob : public Job {
    Q_OBJECT
public:
    PackJob(Location parent, std::vector<QByteArray> names, Location archive, QByteArray options,
            QObject* parentObject = nullptr);
    ~PackJob() override { stop(); }

protected:
    void run() override;

private:
    struct Scan;
    Location parent_;
    std::vector<QByteArray> names_;
    Location archive_;
    QByteArray options_;
};

} // namespace core
