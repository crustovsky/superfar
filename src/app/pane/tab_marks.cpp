// Tab, the marks: what a command acts on, the marks of folders left
// behind, marking by pattern. See tab.cpp for the rest of the class.
#include "pane/tab.h"

#include "counted.h"
#include "pane/listmodel.h"
#include "pane/listview.h"

std::vector<core::Location> Tab::selectedLocations() const
{
    std::vector<core::Location> out;
    if (model_->markedCount() > 0) {
        for (const QByteArray& name : model_->markedNamesInOrder())
            out.push_back(location_.child(name));
    } else if (const QByteArray name = currentName(); !name.isEmpty()) {
        out.push_back(location_.child(name));
    }
    return out;
}

QString Tab::selectionText() const
{
    const int marked = model_->markedCount();
    if (marked > 0) {
        // A job takes every mark, so the prompt says when some of them
        // are not on screen.
        const int unseen = model_->markedNotShown();
        const QString items = counted(marked, tr("item"), tr("items"));
        return unseen > 0 ? tr("%1 (%2 not shown)").arg(items).arg(unseen) : items;
    }
    const QModelIndex idx = view_->currentIndex();
    return idx.isValid() ? model_->entryAt(idx.row()).display : QString();
}

int Tab::markedCount() const
{
    return model_->markedCount();
}

void Tab::clearMarks()
{
    model_->markAll(false);
}

void Tab::clearMarks(const core::Location& dir)
{
    if (location_ == dir && !isArchive() && search_.isEmpty())
        model_->markAll(false);
    else
        marks_.forget(dir.uri().toUtf8());
}

QByteArray Tab::markKey() const
{
    // One folder view, one key: an archive folder and a search are not
    // the folder their location names.
    QByteArray key = location_.uri().toUtf8();
    if (isArchive())
        key += '\0' + archiveDir_;
    else if (!search_.isEmpty())
        key += '\0' + search_.toUtf8();
    return key;
}

void Tab::stashMarks()
{
    if (location_.isValid() && model_->markedCount() > 0)
        marks_.remember(markKey(), model_->markedNames());
}

void Tab::restoreMarks()
{
    // A reload carries its marks itself; what the store holds for this
    // folder is spent either way, so a cleared set does not come back.
    QSet<QByteArray> names = marks_.take(markKey());
    if (!pendingMarks_.isEmpty()) {
        names = std::move(pendingMarks_);
        pendingMarks_.clear();
    }
    if (!names.isEmpty())
        model_->applyMarks(names);
}

void Tab::markGlob(const QString& pattern, bool on)
{
    model_->markGlob(pattern, on);
}
