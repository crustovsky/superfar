#pragma once

#include "core/entry.h"

#include <QByteArray>
#include <QHash>
#include <vector>

namespace core {

// Fills entry.sortKey from entry.display using GLib's filename collation,
// which sorts numbers naturally and respects the locale.
void computeSortKey(Entry& entry);

// Natural sort by name, directories first in either direction.
void sortByName(std::vector<Entry>& entries, bool ascending = true);

class IdNames;

enum class SortBy : quint8 { Name, Size, Modified, Items, Permissions, Owner };

// Directories first in either direction, ties by name. Size and
// Modified read the entries; folders have no size or count of their
// own, so under Size and Items they take their value from folderValues
// (a walked total, an item count), those without one sort after those
// with one, by name. Files under Items keep name order. Permissions
// goes by the permission bits as a number; Owner by user then group,
// by name when names are given, else by id.
void sortEntries(std::vector<Entry>& entries, SortBy by, bool ascending,
                 const QHash<QByteArray, quint64>& folderValues = {}, IdNames* names = nullptr);

// Stable names for the state file, and back; unknown text means Name.
const char* sortName(SortBy by);
SortBy sortFromName(const QString& name);

} // namespace core
