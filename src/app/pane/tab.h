// One folder view inside a pane: the location, its listing, the model and
// the list view. History, filter and marks land here in later milestones.
#pragma once

#include "core/listing.h"
#include "core/location.h"

#include <QWidget>

class ListModel;
class ListView;

class Tab : public QWidget {
    Q_OBJECT
public:
    explicit Tab(QWidget* parent = nullptr);

    const core::Location& location() const { return location_; }
    void navigate(const core::Location& dir);
    void goUp();

    void setShowHidden(bool on);
    bool showHidden() const;

    ListView* view() const { return view_; }

Q_SIGNALS:
    void locationChanged(const core::Location& dir);
    void statusChanged(const QString& text);
    void loaded();

private:
    void onBatch(quint64 generation, std::vector<core::Entry> entries);
    void onFinished(quint64 generation);
    void onFailed(quint64 generation, const QString& message);
    void activate(const QModelIndex& index);
    void placeCursor();
    void updateStatus();
    QByteArray currentName() const;

    core::Location location_;
    quint64 generation_ = 0;
    core::Listing* listing_;
    ListModel* model_;
    ListView* view_;
    QByteArray cursorName_;  // where the cursor goes once the load ends
};
