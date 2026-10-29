// One folder view inside a pane: the location, its listing, the model and
// the list view. History and filter land here in later milestones.
#pragma once

#include "core/listing.h"
#include "core/location.h"
#include "core/space.h"
#include "keymap.h"

#include <QSet>
#include <QWidget>

class ListModel;
class ListView;

class Tab : public QWidget {
    Q_OBJECT
public:
    explicit Tab(QWidget* parent = nullptr);

    const core::Location& location() const { return location_; }
    void navigate(const core::Location& dir);
    void reload();  // same folder again, keeping cursor and marks
    void goUp();

    void setShowHidden(bool on);
    bool showHidden() const;

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

private:
    void load(const core::Location& dir);
    void onBatch(quint64 generation, std::vector<core::Entry> entries);
    void onFinished(quint64 generation);
    void onFailed(quint64 generation, const QString& message);
    void onSpace(quint64 generation, bool ok, quint64 freeBytes);
    void activate(const QModelIndex& index);
    void placeCursor();
    void updateStatus();
    QByteArray currentName() const;

    core::Location location_;
    quint64 generation_ = 0;
    core::Listing* listing_;
    core::SpaceQuery* space_;
    ListModel* model_;
    ListView* view_;
    QByteArray cursorName_;       // where the cursor goes once the load ends
    QSet<QByteArray> pendingMarks_;  // marks to restore once the load ends
    QString spaceText_;
    bool benchReporter_ = false;
};
