// Small one-shot operations that are not worth a job.
#pragma once

#include "core/location.h"

#include <QString>
#include <functional>

namespace core {

// Creates the directory; done() runs on the main thread with an empty
// message on success.
void makeDirectory(const Location& dir, std::function<void(const QString& error)> done);

// Creates an empty file; fails if it exists.
void createFile(const Location& file, std::function<void(const QString& error)> done);

// Sets the unix permission bits.
void setMode(const Location& file, quint32 mode, std::function<void(const QString& error)> done);

// Renames in place. done() gets the name the filesystem actually used,
// raw and for display.
void renameEntry(const Location& file, const QString& newName,
                 std::function<void(const QString& error, const QByteArray& actualName,
                                    const QString& display)> done);

} // namespace core
