// ListModel, the marks: per entry, separate from the cursor, counted
// whether a filter shows them or not. See listmodel.cpp for the rest.
#include "pane/listmodel.h"

#include <QRegularExpression>

void ListModel::setMarked(int row, bool on)
{
    quint8& m = marked_[rows_[row]];
    if ((m != 0) == on)
        return;
    m = on ? 1 : 0;
    markedCount_ += on ? 1 : -1;
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
    Q_EMIT marksChanged();
}

void ListModel::markAll(bool on)
{
    if (!on) {
        // Clearing is for every mark, the ones a filter hides too.
        std::fill(marked_.begin(), marked_.end(), quint8(0));
        markedCount_ = 0;
    } else {
        for (quint32 idx : rows_) {
            markedCount_ += marked_[idx] ? 0 : 1;
            marked_[idx] = 1;
        }
    }
    rowsRepaint();
}

void ListModel::invertMarks()
{
    // The rows shown; a mark the filter hides stays as it was and stays
    // counted.
    for (quint32 idx : rows_) {
        marked_[idx] = marked_[idx] ? 0 : 1;
        markedCount_ += marked_[idx] ? 1 : -1;
    }
    rowsRepaint();
}

int ListModel::markedNotShown() const
{
    int shown = 0;
    for (quint32 idx : rows_)
        shown += marked_[idx];
    return static_cast<int>(std::count(marked_.begin(), marked_.end(), quint8(1))) - shown;
}

QSet<QByteArray> ListModel::markedNames() const
{
    QSet<QByteArray> names;
    for (quint32 i = 0; i < entries_.size(); ++i)
        if (marked_[i])
            names.insert(entries_[i].name);
    return names;
}

std::vector<QByteArray> ListModel::markedNamesInOrder() const
{
    std::vector<QByteArray> names;
    for (quint32 i = 0; i < entries_.size(); ++i)
        if (marked_[i])
            names.push_back(entries_[i].name);
    return names;
}

void ListModel::applyMarks(const QSet<QByteArray>& names)
{
    markedCount_ = 0;
    for (quint32 i = 0; i < entries_.size(); ++i) {
        // Every mark counts, shown or not: a job takes them all (#62).
        marked_[i] = names.contains(entries_[i].name) ? 1 : 0;
        markedCount_ += marked_[i];
    }
    rowsRepaint();
}

void ListModel::rowsRepaint()
{
    if (!rows_.empty())
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    Q_EMIT marksChanged();
}

void ListModel::markGlob(const QString& pattern, bool on)
{
    const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pattern),
                                QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
        return;
    for (quint32 idx : rows_) {
        if (!re.match(entries_[idx].display).hasMatch())
            continue;
        if ((marked_[idx] != 0) != on) {
            marked_[idx] = on ? 1 : 0;
            markedCount_ += on ? 1 : -1;
        }
    }
    rowsRepaint();
}
