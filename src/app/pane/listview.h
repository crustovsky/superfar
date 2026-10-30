// The file list of one tab: a flat tree view over ListModel. The cursor
// is the current index; marks live in the model; keys go through the
// keymap and come out as commands.
#pragma once

#include "keymap.h"

#include <QTreeView>

class ListView : public QTreeView {
    Q_OBJECT
public:
    explicit ListView(QWidget* parent = nullptr);

    // Fixed widths for the numeric columns from font metrics (rule 12).
    void sizeColumns();

    void setActivePane(bool active);
    // Only the first tab of the first pane reports bench marks.
    void setBenchReporter(bool on) { benchReporter_ = on; }
    bool activePane() const { return active_; }

Q_SIGNALS:
    void commandRequested(keymap::Command command);
    // A printable key with no command bound: the quick filter takes it.
    void textTyped(const QString& text);
    void cursorMoved();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void currentChanged(const QModelIndex& current, const QModelIndex& previous) override;

private:
    QRect rowRect(const QModelIndex& index) const;

    bool active_ = false;
    bool paintedRows_ = false;
    bool benchReporter_ = false;
};
