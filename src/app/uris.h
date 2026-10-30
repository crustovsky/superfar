// File lists on the clipboard and in drags, in the forms other file
// managers speak: text/uri-list, and x-special/gnome-copied-files which
// carries whether the files were cut or copied.
#pragma once

#include "core/location.h"

#include <vector>

class QMimeData;

namespace uris {

QMimeData* make(const std::vector<core::Location>& files, bool cut);
// False when the data holds no files. cut is set from the gnome format
// when present, else false.
bool read(const QMimeData* data, std::vector<core::Location>* files, bool* cut);
bool holdsFiles(const QMimeData* data);

} // namespace uris
