// Tab, what it does when the folder changes under it. See tab.cpp for
// the rest of the class.
#include "pane/tab.h"

#include "core/listing.h"
#include "pane/listmodel.h"
#include "pane/listview.h"

#include <QPointer>

// Entries came or went: the folder is listed again.
void Tab::onMonitor()
{
    if (!current_ || view_->isEditing()) {
        dirty_ = true;
        return;
    }
    Q_EMIT folderChanged();
    reload();
}

// An entry that is there changed: its row is brought up to date where
// it stands. No listing, no reset, so cursor, marks and scroll are not
// touched while a download runs (the monitor paces these to about one
// a second).
void Tab::onEntryChanged(const QByteArray& name, bool settled)
{
    if (!current_ || view_->isEditing()) {
        dirty_ = true;  // rule 13; and an open editor is left alone, as on a reload
        return;
    }
    space_->request(location_, generation_);  // what a growing file takes
    core::queryEntry(location_.child(name), [self = QPointer<Tab>(this), generation = generation_, settled](
                                                bool ok, const core::Entry& fresh) {
        if (!self || generation != self->generation_ || !ok)
            return;  // gone meanwhile: its own event lists the folder again
        self->model_->updateEntry(fresh);
        self->updateStatus();
        // The row does not jump about while the file is written; once
        // it is settled, an order that reads what changed is put right.
        const core::SortBy by = self->model_->sortBy();
        if (settled && by != core::SortBy::Name && by != core::SortBy::Items)
            self->resort();
    });
}
