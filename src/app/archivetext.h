// The preview's text for an archive: what the listing adds up to, and
// the top of its tree.
#pragma once

#include "core/archive.h"

#include <QString>
#include <vector>

namespace archivetext {

// Files, folders and sizes, what extraction does, the newest member,
// and what is only there when it matters: dropped paths, symlinks,
// executables at the top level.
QString summary(const core::ArchiveSummary& s, const core::ArchiveDropped& dropped, quint64 packedBytes);

// The first maxLines members as an indented tree, folders first at
// each level in the order the archive tab shows, sizes in a column
// (for a fixed-pitch font), then "and N more" when it was cut.
QString tree(const std::vector<core::ArchiveEntry>& entries, int maxLines);

} // namespace archivetext
