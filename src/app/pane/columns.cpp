#include "pane/columns.h"

#include "core/unixattrs.h"

#include <QCoreApplication>
#include <QLocale>

#include <unistd.h>

namespace columns {

namespace {

const Spec kSpecs[ColumnCount] = {
    {"name", QT_TRANSLATE_NOOP("columns", "Name"), true, false, core::SortBy::Name},
    {"size", QT_TRANSLATE_NOOP("columns", "Size"), true, true, core::SortBy::Size},
    {"items", QT_TRANSLATE_NOOP("columns", "Items"), true, true, core::SortBy::Items},
    {"modified", QT_TRANSLATE_NOOP("columns", "Modified"), true, false, core::SortBy::Modified},
    {"permissions", QT_TRANSLATE_NOOP("columns", "Permissions"), false, false, core::SortBy::Permissions},
    {"owner", QT_TRANSLATE_NOOP("columns", "Owner"), false, false, core::SortBy::Owner},
};

} // namespace

const Spec& spec(int column)
{
    return kSpecs[column >= 0 && column < ColumnCount ? column : NameColumn];
}

QString name(int column)
{
    return column >= 0 && column < ColumnCount ? QString::fromLatin1(kSpecs[column].name) : QString();
}

QString label(int column)
{
    return column >= 0 && column < ColumnCount ? QCoreApplication::translate("columns", kSpecs[column].label)
                                               : QString();
}

int fromName(const QString& name)
{
    for (int c = 0; c < ColumnCount; ++c)
        if (name == QLatin1String(kSpecs[c].name))
            return c;
    return -1;
}

QString widest(int column)
{
    switch (column) {
    case SizeColumn: return QStringLiteral("8888.8 MiB");
    case ItemsColumn: return QLocale().toString(88888888);
    case ModifiedColumn: return QStringLiteral("8888-88-88 88:88");
    case PermissionsColumn: return QStringLiteral("drwxrwxrwx");
    case OwnerColumn: {
        // Names have no widest. Most rows are this user's or root's:
        // room for both, the edge drags for the rest.
        static const QString mine = core::IdNames().owner(::geteuid(), ::getegid());
        return mine.size() > 9 ? mine : QStringLiteral("root:root");
    }
    default: return {};
    }
}

Layout Layout::from(const QStringList& order, const QStringList& hidden)
{
    Layout l;
    l.hidden.fill(false, ColumnCount);
    QList<bool> listed(ColumnCount, false);
    l.order.append(NameColumn);
    listed[NameColumn] = true;
    for (const QString& n : order) {
        const int c = fromName(n);
        if (c < 0 || listed[c])
            continue;
        listed[c] = true;
        l.order.append(c);
    }
    for (int c = 0; c < ColumnCount; ++c) {
        if (!listed[c]) {
            l.order.append(c);
            l.hidden[c] = !kSpecs[c].shown;
        }
        if (c != NameColumn && hidden.contains(QLatin1String(kSpecs[c].name)))
            l.hidden[c] = true;
    }
    return l;
}

QStringList Layout::orderNames() const
{
    QStringList out;
    for (int c : order)
        out << name(c);
    return out;
}

QStringList Layout::hiddenNames() const
{
    QStringList out;
    for (int c : order)
        if (hidden.value(c))
            out << name(c);
    return out;
}

} // namespace columns
