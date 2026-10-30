// One folder view inside a pane: the location, its listing, the model and
// the list view. History and filter land here in later milestones.
#pragma once

#include "core/finder.h"
#include "core/listing.h"
#include "core/location.h"
#include "core/monitor.h"
#include "core/space.h"
#include "keymap.h"

#include <QSet>
#include <QWidget>

class ListModel;
class ListView;
struct Services;

class Tab : public QWidget {
    Q_OBJECT
public:
    explicit Tab(const Services& services, QWidget* parent = nullptr);

    const core::Location& location() const { return location_; }
    void navigate(const core::Location& dir);
    // Find in tree: the tab lists matches below root, named by their
    // path relative to it, and otherwise behaves like a folder.
    void search(const core::Location& root, const QString& pattern);
    const QString& searchPattern() const { return search_; }
    void reload();  // same folder again, keeping cursor, marks and scroll
    void reloadWithCursor(const QByteArray& name);
    void goUp();
    // Where the cursor goes once the running load ends.
    void setCursorOnLoad(const QByteArray& name) { cursorName_ = name; }

    // The marked entries, or the cursor entry when nothing is marked.
    std::vector<core::Location> selectedLocations() const;
    QString selectionText() const;  // "3 items" or the one name
    void clearMarks();
    bool monitoring() const;
    // The cursor entry; invalid when the list is empty.
    core::Location cursorLocation() const;
    QStringList entryNames() const;  // display names of every entry, hidden included
    bool cursorIsDir() const;
    void markGlob(const QString& pattern, bool on);

    // Whether this tab is the one its pane shows (rule 13).
    void setCurrent(bool current);

    void setShowHidden(bool on);
    bool showHidden() const;
    bool recursiveSizes() const;
    void setRecursiveSizes(bool on);
    bool thumbnails() const;
    void setThumbnails(bool on);
    // A locked tab keeps its folder: navigation opens a new tab instead.
    void setLocked(bool on) { locked_ = on; }
    bool isLocked() const { return locked_; }

    void setActive(bool active);
    void refreshStatus() { updateStatus(); }
    // Runs a tab-scoped command; false when the command is not the tab's.
    bool execute(keymap::Command command);

    ListView* view() const { return view_; }

Q_SIGNALS:
    void locationChanged(const core::Location& dir);
    void statusChanged(const QString& items, const QString& space);
    void commandRequested(keymap::Command command);
    void loaded();
    void folderChanged();  // the monitor saw something; a reload follows
    void cursorChanged(const core::Location& item);  // invalid when none
    void navigated(const core::Location& dir);       // a user navigation, for history
    void openInNewTab(const core::Location& dir);    // navigation while locked
    void mountNeeded(const core::Location& dir);     // the load failed for want of a mount
    void dropRequested(const std::vector<core::Location>& sources, const core::Location& target, bool move);
    void message(const QString& text);  // an error worth a moment in the status line

private:
    void load(const core::Location& dir);
    void onBatch(quint64 generation, std::vector<core::Entry> entries);
    void onFinished(quint64 generation);
    void onFailed(quint64 generation, const QString& message, bool notMounted);
    void onSearchFinished(quint64 generation, quint64 scanned);
    void onSpace(quint64 generation, bool ok, quint64 freeBytes);
    void onMonitor();
    void activate(const QModelIndex& index);
    void typed(const QString& text);
    void setFilter(const QString& text);
    void rename(const QByteArray& name, const QString& newName);
    void placeCursor();
    void updateStatus();
    QByteArray currentName() const;

    core::Location location_;
    quint64 generation_ = 0;
    core::Listing* listing_;
    core::Finder* finder_;
    QString search_;  // non-empty while this tab shows search results
    core::SpaceQuery* space_;
    core::Monitor* monitor_;
    ListModel* model_;
    ListView* view_;
    QByteArray cursorName_;       // where the cursor goes once the load ends
    QSet<QByteArray> pendingMarks_;  // marks to restore once the load ends
    QString spaceText_;
    int scrollValue_ = -1;  // restored after a reload
    bool current_ = true;
    bool dirty_ = false;    // changed while not current or while editing
    bool locked_ = false;
    bool benchReporter_ = false;
};
