#pragma once

#include "core/entry.h"

#include <vector>

namespace core {

// Fills entry.sortKey from entry.display using GLib's filename collation,
// which sorts numbers naturally and respects the locale.
void computeSortKey(Entry& entry);

// Natural sort by name, directories first in either direction.
void sortByName(std::vector<Entry>& entries, bool ascending = true);

} // namespace core
