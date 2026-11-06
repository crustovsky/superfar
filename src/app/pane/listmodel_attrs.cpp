// Mode and owner in the list (ListModel's attribute half): their text,
// and edits that come back from a cell as requests for the tab.
#include "pane/listmodel.h"

QString ListModel::attributeText(const core::Entry& e, int column, bool forEdit) const
{
    if (e.mode == 0)
        return {};  // the backend did not say (an archive member, some remotes)
    if (column == OwnerColumn)
        return ids_.owner(e.uid, e.gid);
    QString mode = core::modeString(e.mode);
    if (forEdit)
        return mode.mid(1);  // the type is not for changing
    if (e.symlink)
        mode[0] = QLatin1Char('l');  // the bits are the target's, as the size is
    return mode;
}

bool ListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || !(flags(index) & Qt::ItemIsEditable))
        return false;
    const core::Entry& e = entryAt(index.row());
    const QString text = value.toString().trimmed();
    switch (index.column()) {
    case NameColumn: {
        const QString newName = value.toString();  // as typed: spaces are part of a name
        if (newName.isEmpty() || newName == e.display || newName.contains(QLatin1Char('/')))
            return false;
        Q_EMIT renameRequested(e.name, newName);
        return true;
    }
    case PermissionsColumn: {
        if (text.isEmpty() || text == attributeText(e, PermissionsColumn, true))
            return false;
        const auto mode = core::parseMode(text);
        if (!mode) {
            Q_EMIT editRejected(tr("Not a mode: %1 (as rwxr-xr-x or 0755)").arg(text));
            return false;
        }
        if (*mode == (e.mode & 07777))
            return false;
        Q_EMIT modeRequested(e.name, *mode);
        return true;
    }
    case OwnerColumn: {
        if (text.isEmpty() || text == attributeText(e, OwnerColumn, true))
            return false;
        QString error;
        auto change = core::parseOwner(text, &error);
        if (!change) {
            Q_EMIT editRejected(error);
            return false;
        }
        // Only the half that differs: a plain user may change the group
        // of a file while naming its owner as it is.
        if (change->uid == e.uid)
            change->uid.reset();
        if (change->gid == e.gid)
            change->gid.reset();
        if (change->isEmpty())
            return false;
        Q_EMIT ownerRequested(e.name, *change);
        return true;
    }
    default:
        return false;
    }
}

void ListModel::updateAttributes(const QByteArray& name, std::optional<quint32> permissions,
                                 const core::OwnerChange& owner)
{
    const int row = rowOf(name);
    if (row < 0)
        return;  // filtered out; the next load has it
    core::Entry& e = entries_[rows_[static_cast<size_t>(row)]];
    if (permissions)
        e.mode = (e.mode & ~07777u) | (*permissions & 07777u);
    if (owner.uid)
        e.uid = *owner.uid;
    if (owner.gid)
        e.gid = *owner.gid;
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}
