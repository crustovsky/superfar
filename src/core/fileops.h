// Small one-shot operations that are not worth a job.
#pragma once

#include "core/location.h"

#include <QString>
#include <functional>

namespace core {

// Creates the directory; done() runs on the main thread with an empty
// message on success.
void makeDirectory(const Location& dir, std::function<void(const QString& error)> done);

} // namespace core
