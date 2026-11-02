#include "core/sort.h"

#include "core/gobject.h"

#include <QString>

#include <algorithm>
#include <cstring>

namespace core {

void computeSortKey(Entry& entry)
{
    const QByteArray utf8 = entry.display.toUtf8();
    GStr key(g_utf8_collate_key_for_filename(utf8.constData(), utf8.size()));
    entry.sortKey = QByteArray(key.get());
}

namespace {

bool nameLess(const Entry& a, const Entry& b)
{
    const int c = std::strcmp(a.sortKey.constData(), b.sortKey.constData());
    if (c != 0)
        return c < 0;
    return a.name < b.name;
}

} // namespace

void sortByName(std::vector<Entry>& entries, bool ascending)
{
    std::sort(entries.begin(), entries.end(), [ascending](const Entry& a, const Entry& b) {
        if (a.isDir() != b.isDir())
            return a.isDir();
        return ascending ? nameLess(a, b) : nameLess(b, a);
    });
}

namespace {

// -1, 0, 1 on the sort value alone; name ties are broken by the caller.
// Folders without a value compare after those with one, whatever the
// direction, so the unknown ones do not jump to the top on a reverse.
template <typename Value>
int compareValue(Value a, Value b, bool ascending)
{
    if (a == b)
        return 0;
    return (a < b) == ascending ? -1 : 1;
}

} // namespace

void sortEntries(std::vector<Entry>& entries, SortBy by, bool ascending,
                 const QHash<QByteArray, quint64>& folderValues)
{
    if (by == SortBy::Name) {
        sortByName(entries, ascending);
        return;
    }
    auto folderValue = [&folderValues](const Entry& e, quint64* value) {
        const auto it = folderValues.constFind(e.name);
        if (it == folderValues.cend())
            return false;
        *value = it.value();
        return true;
    };
    std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        if (a.isDir() != b.isDir())
            return a.isDir();
        int c = 0;
        if (by == SortBy::Modified) {
            c = compareValue(a.modified, b.modified, ascending);
        } else if (a.isDir()) {
            quint64 va = 0, vb = 0;
            const bool ha = folderValue(a, &va), hb = folderValue(b, &vb);
            if (ha != hb)
                return ha;
            if (ha)
                c = compareValue(va, vb, ascending);
        } else if (by == SortBy::Size) {
            c = compareValue(a.size, b.size, ascending);
        }
        if (c != 0)
            return c < 0;
        return ascending ? nameLess(a, b) : nameLess(b, a);
    });
}

const char* sortName(SortBy by)
{
    switch (by) {
    case SortBy::Size: return "size";
    case SortBy::Modified: return "modified";
    case SortBy::Items: return "items";
    case SortBy::Name: break;
    }
    return "name";
}

SortBy sortFromName(const QString& name)
{
    for (SortBy by : {SortBy::Size, SortBy::Modified, SortBy::Items})
        if (name == QLatin1String(sortName(by)))
            return by;
    return SortBy::Name;
}

} // namespace core
