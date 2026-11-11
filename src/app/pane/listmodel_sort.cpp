// The order of the rows (ListModel's sort half): the column, the
// direction, and the folder values that counts and walks supply late.
#include "pane/listmodel.h"

#include "core/sort.h"

#include <QTimer>

ListModel::Column ListModel::columnOf(core::SortBy by)
{
    for (int c = 0; c < ColumnCount; ++c)
        if (columns::spec(c).sort == by)
            return static_cast<Column>(c);
    return NameColumn;
}

core::SortBy ListModel::sortOf(int column)
{
    return columns::spec(column).sort;
}

void ListModel::sortEntries()
{
    core::sortEntries(entries_, sortBy_, sortAscending_,
                      sortNeedsFolders() ? folderValues() : QHash<QByteArray, quint64>(), &ids_);
}

bool ListModel::sortNeedsFolders() const
{
    return sortBy_ == core::SortBy::Items || (sortBy_ == core::SortBy::Size && recursive_);
}

QHash<QByteArray, quint64> ListModel::folderValues() const
{
    // Only finished results: a walk still running would reorder the
    // list on every partial total.
    QHash<QByteArray, quint64> values;
    for (auto it = dirCache_.cbegin(); it != dirCache_.cend(); ++it) {
        const core::DirSize& d = it.value();
        if (!d.ok || !d.complete)
            continue;
        if (sortBy_ == core::SortBy::Size) {
            if (!d.recursive)
                continue;
            values.insert(it.key(), d.bytes);
        } else {
            if (recursive_ && !d.recursive)
                continue;
            values.insert(it.key(), d.recursive ? d.total() : d.direct);
        }
    }
    return values;
}

void ListModel::requestSortValues()
{
    // An order by count or walked size needs every folder's value, not
    // only the painted ones; dirSize() asks for what is not known or
    // pending, and keeps rule 9 (local only). Hidden rows wait until
    // they are shown.
    if (!sortNeedsFolders() || !walksHere())
        return;
    for (quint32 idx : rows_)
        if (entries_[idx].isDir())
            dirSize(entries_[idx]);
}

void ListModel::setSort(core::SortBy by, bool ascending)
{
    if (by == sortBy_ && ascending == sortAscending_)
        return;
    sortBy_ = by;
    sortAscending_ = ascending;
    resort();
    requestSortValues();
}

void ListModel::resort()
{
    resortTimer_->stop();
    const QSet<QByteArray> marks = markedCount_ > 0 ? markedNames() : QSet<QByteArray>();
    beginResetModel();
    sortEntries();
    rebuildRows();
    endResetModel();
    if (!marks.isEmpty())
        applyMarks(marks);  // marks are per entry index, which the sort moved
}
