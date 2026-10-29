#include "core/sort.h"

#include "core/gobject.h"

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

} // namespace core
