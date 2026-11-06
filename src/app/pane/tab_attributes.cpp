// Mode and owner changes asked for from a cell of the list (Tab's
// attribute half). One file, one call, no job: a refusal is a message
// in the pane.
#include "pane/tab.h"

#include "core/fileops.h"
#include "pane/listmodel.h"

#include <QPointer>

void Tab::changeMode(const QByteArray& name, quint32 permissions)
{
    const core::Location dir = location_;
    QPointer<Tab> guard(this);
    // The whole mode as typed, special bits included. A row over a link
    // shows its target, so that is what changes.
    core::setMode(dir.child(name), permissions, 07777, [guard, dir, name, permissions](const QString& error) {
        if (!guard)
            return;
        if (!error.isEmpty())
            Q_EMIT guard->message(error);
        else if (guard->location_ == dir)
            guard->model_->updateAttributes(name, permissions, {});
    }, true);
}

void Tab::changeOwner(const QByteArray& name, const core::OwnerChange& owner)
{
    const core::Location dir = location_;
    QPointer<Tab> guard(this);
    core::setOwner(dir.child(name), owner, [guard, dir, name, owner](const QString& error) {
        if (!guard)
            return;
        if (!error.isEmpty())
            Q_EMIT guard->message(error);
        else if (guard->location_ == dir)
            guard->model_->updateAttributes(name, std::nullopt, owner);
    }, true);
}
