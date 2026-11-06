// The columns a list can show, as one table: the name in the config and
// the state, the header label, the widest text (rule 12), whether the
// column is shown by default and what a click on its header sorts by.
// The model, the view and the column list all read it. A Layout is one
// pane's arrangement of them.
#pragma once

#include "core/sort.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace columns {

enum Column { NameColumn, SizeColumn, ItemsColumn, ModifiedColumn, PermissionsColumn, OwnerColumn, ColumnCount };

struct Spec {
    const char* name;
    const char* label;
    bool shown;  // by default
    bool rightAligned;
    core::SortBy sort;
};
const Spec& spec(int column);
QString name(int column);
QString label(int column);
int fromName(const QString& name);  // -1 when unknown
// The widest text the column shows, for its width; empty for Name,
// which takes what the others leave.
QString widest(int column);

struct Layout {
    QList<int> order;    // every column once, left to right; Name first
    QList<bool> hidden;  // by column; never Name

    // From saved names. Unknown names and repeats are dropped; a column
    // the order does not list comes after the listed ones, shown or not
    // as its default says, so a column added by a later version does
    // not appear in a saved arrangement unasked.
    static Layout from(const QStringList& order, const QStringList& hidden);
    QStringList orderNames() const;
    QStringList hiddenNames() const;
};

} // namespace columns
