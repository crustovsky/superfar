// One folder view inside a pane: the location, its listing, the model and
// the list view. History and filter land here in later milestones.
#pragma once

#include "core/archive.h"
#include "core/finder.h"
#include "core/listing.h"
#include "core/location.h"
#include "core/monitor.h"
#include "core/sort.h"
#include "core/space.h"
#include "core/unixattrs.h"
#include "keymap.h"
#include "pane/markstore.h"

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
    // Inside an archive (opened by Enter on it): the tab shows one folder
    // of its listing; F5 extracts, everything else is read-only.
    bool isArchive() const { return archive_.isValid(); }
    const core::Location& archiveFile() const { return archive_; }
    int archiveDepth() const;  // folders below the archive root
    std::vector<QByteArray> archiveMembers() const;  // marked or cursor, as paths inside
    QString displayPath() const;
    void reload();  // same folder again, keeping cursor, marks and scroll
    void reloadWithCursor(const QByteArray& name);
    void goUp();
    // Where the cursor goes once the running load ends.
    void setCursorOnLoad(const QByteArray& name) { cursorName_ = name; }
    QByteArray currentName() const;  // raw name under the cursor, empty when none

    // The marked entries, or the cursor entry when nothing is marked.
    std::vector<core::Location> selectedLocations() const;
    QString selectionText() const;  // "3 items" or the one name
    int markedCount() const;
    void clearMarks();
    // A job in dir ended: its marks are spent, whether the tab still
    // shows dir or keeps them for its return.
    void clearMarks(const core::Location& dir);
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
    // The column the rows follow; a header click changes it too.
    core::SortBy sortBy() const;
    bool sortAscending() const;
    void setSort(core::SortBy by, bool ascending);
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
    void sortChanged();                              // for the session
    void mountNeeded(const core::Location& dir);     // the load failed for want of a mount
    void dropRequested(const std::vector<core::Location>& sources, const core::Location& target, bool move);
    void message(const QString& text);  // an error worth a moment in the status line

private:
    void load(const core::Location& dir);
    void onBatch(quint64 generation, std::vector<core::Entry> entries);
    void onFinished(quint64 generation);
    void onFailed(quint64 generation, const QString& error, core::ListFailure why);
    void onSearchFinished(quint64 generation, quint64 scanned);
    void enterArchive(const core::Location& file);
    void onArchiveListed(quint64 generation, std::vector<core::ArchiveEntry> entries);
    void showArchiveDir(const QByteArray& dir);
    void onSpace(quint64 generation, bool ok, quint64 freeBytes);
    void onMonitor();
    void activate(const QModelIndex& index);
    void typed(const QString& text);
    void setFilter(const QString& text);
    void rename(const QByteArray& name, const QString& newName);
    void changeMode(const QByteArray& name, quint32 permissions);
    void changeOwner(const QByteArray& name, const core::OwnerChange& owner);
    void applySort(int column, bool ascending);
    void resort();
    void placeCursor();
    void updateStatus();
    void fallBack(const QString& why);
    // Marks are kept per folder across a round trip: stashed on the way
    // out, taken back when a load for the folder ends.
    QByteArray markKey() const;
    void stashMarks();
    void restoreMarks();

    core::Location location_;
    quint64 generation_ = 0;
    core::Listing* listing_;
    core::Finder* finder_;
    QString search_;  // non-empty while this tab shows search results
    core::ArchiveLister* lister_;
    core::Location archive_;
    QByteArray archiveDir_;  // folder inside the archive, '/' separated, no slashes at the ends
    std::vector<core::ArchiveEntry> archiveEntries_;
    core::SpaceQuery* space_;
    core::Monitor* monitor_;
    ListModel* model_;
    ListView* view_;
    QByteArray cursorName_;       // where the cursor goes once the load ends
    QSet<QByteArray> pendingMarks_;  // marks to restore once the load ends (a reload)
    MarkStore marks_;                // marks of folders left behind
    QString spaceText_;
    int scrollValue_ = -1;  // restored after a reload
    bool current_ = true;
    bool dirty_ = false;    // changed while not current or while editing
    bool locked_ = false;
    bool benchReporter_ = false;
};
