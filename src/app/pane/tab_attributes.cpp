// Mode and owner changes asked for from a cell of the list (Tab's
// attribute half). One file, one call, no job: a refusal is a message
// in the pane.
#include "pane/tab.h"

#include "core/fileops.h"
#include "pane/listmodel.h"

#include <QPointer>

// The row's mode and owner as they are before a cell edit, for undo.
// Nothing for a row over a link: the edit goes to its target, whose
// attributes the row does not hold.
std::optional<core::UndoItem> Tab::attributesBefore(const QByteArray& name) const
{
    const int row = model_->rowOf(name);
    if (row < 0)
        return std::nullopt;
    const core::Entry& e = model_->entryAt(row);
    if (e.symlink || e.mode == 0)
        return std::nullopt;
    core::UndoItem item;
    item.kind = core::UndoItem::Kind::Attributes;
    item.at = location_.child(name);
    item.mode[0] = item.mode[1] = e.mode & 07777;
    item.uid[0] = item.uid[1] = e.uid;
    item.gid[0] = item.gid[1] = e.gid;
    return item;
}

void Tab::changeMode(const QByteArray& name, quint32 permissions)
{
    const core::Location dir = location_;
    QPointer<Tab> guard(this);
    // The whole mode as typed, special bits included. A row over a link
    // shows its target, so that is what changes.
    std::optional<core::UndoItem> before = attributesBefore(name);
    core::setMode(dir.child(name), permissions, 07777, [guard, dir, name, permissions, before](const QString& error) mutable {
        if (!guard)
            return;
        if (!error.isEmpty()) {
            Q_EMIT guard->message(error);
            return;
        }
        if (before) {
            before->mode[1] = permissions & 07777;
            Q_EMIT guard->undoable(tr("permissions of %1").arg(before->at.displayName()), *before);
        }
        if (guard->location_ == dir)
            guard->model_->updateAttributes(name, permissions, {});
    }, true);
}

void Tab::changeOwner(const QByteArray& name, const core::OwnerChange& owner)
{
    const core::Location dir = location_;
    QPointer<Tab> guard(this);
    std::optional<core::UndoItem> before = attributesBefore(name);
    if (before && (before->mode[0] & 06000))
        before.reset();  // the change drops setuid and setgid: not ours to put back by guess
    core::setOwner(dir.child(name), owner, [guard, dir, name, owner, before](const QString& error) mutable {
        if (!guard)
            return;
        if (!error.isEmpty()) {
            Q_EMIT guard->message(error);
            return;
        }
        if (before) {
            before->uid[1] = owner.uid.value_or(before->uid[0]);
            before->gid[1] = owner.gid.value_or(before->gid[0]);
            Q_EMIT guard->undoable(tr("owner of %1").arg(before->at.displayName()), *before);
        }
        if (guard->location_ == dir)
            guard->model_->updateAttributes(name, std::nullopt, owner);
    }, true);
}
