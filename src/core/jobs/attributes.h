// Permission bits and ownership for a set of locations and, when asked,
// for everything below the folders among them. One worker walks and
// sets; a refusal is asked about like a copy error, or logged when the
// job runs unattended.
//
// Links are never followed: a link's own owner changes, its mode is
// left (it has none that means anything) and a link named at the top
// is logged as skipped. Below a folder a file takes execute bits only
// where it already has one, as chmod's X does, so "rwxr-xr-x for this
// tree" does not turn every text file into a program. Another
// filesystem mounted below is left alone and logged.
#pragma once

#include "core/jobs/job.h"
#include "core/unixattrs.h"

#include <optional>

namespace core {

struct AttributeChange {
    std::optional<quint32> mode;  // the bits to set, as far as modeMask names them
    quint32 modeMask = 0777;      // the file's other mode bits stay
    OwnerChange owner;
    bool recursive = false;

    bool isEmpty() const { return !mode && owner.isEmpty(); }
};

class AttributesJob : public Job {
    Q_OBJECT
public:
    AttributesJob(std::vector<Location> items, AttributeChange change, QObject* parent = nullptr);

    ~AttributesJob() override { stop(); }

protected:
    void run() override;

private:
    struct Impl;
    friend struct Impl;

    std::vector<Location> items_;
    AttributeChange change_;
};

} // namespace core
