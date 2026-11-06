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

// Sets the mode bits named by mask from mode and leaves the file's
// others as they are: with the mask 0777, read, write and execute
// change while setuid, setgid and sticky stay.
void setMode(const Location& file, quint32 mode, quint32 mask, std::function<void(const QString& error)> done);
// The mode a file gets: its old bits outside the mask, the new inside.
constexpr quint32 mergedMode(quint32 old, quint32 mode, quint32 mask)
{
    return (old & 07777 & ~mask) | (mode & mask);
}

// A sibling name that does not exist yet: "name (2).ext", "name (3).ext"
// and so on. Synchronous; for job workers answering a Rename.
Location uniqueSibling(const Location& file);

// Renames in place. done() gets the name the filesystem actually used,
// raw and for display.
void renameEntry(const Location& file, const QString& newName,
                 std::function<void(const QString& error, const QByteArray& actualName,
                                    const QString& display)> done);

} // namespace core
