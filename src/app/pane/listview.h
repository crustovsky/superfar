// The file list of one tab: a flat tree view over ListModel. The cursor
// is the current index; marks live in the model; keys go through the
// keymap and come out as commands.
#pragma once

#include "keymap.h"

#include <QHash>
#include <QTreeView>

class ListView : public QTreeView {
    Q_OBJECT
public:
    explicit ListView(QWidget* parent = nullptr);

    // Fixed widths for the numeric columns from font metrics (rule 12).
    void sizeColumns();
    // Config: saved widths by column name and the hidden set.
    void applyColumns(const QHash<QString, int>& widths, const QStringList& hidden);
    static QString columnName(int column);

    // The header's sort indicator; the header flips it on a click and
    // sortChanged() reports both.
    void showSort(int column, bool ascending);
    void setActivePane(bool active);
    // Thumbnail mode: taller rows with a bigger decoration.
    void setThumbnailRows(bool on);
    // Only the first tab of the first pane reports bench marks.
    void setBenchReporter(bool on) { benchReporter_ = on; }
    bool activePane() const { return active_; }
    // The cursor row across the whole list, for its outline and repaint.
    QRect rowRect(const QModelIndex& index) const;
    bool isEditing() const { return state() == EditingState; }

Q_SIGNALS:
    void commandRequested(keymap::Command command);
    // A printable key with no command bound: the quick filter takes it.
    void textTyped(const QString& text);
    void cursorMoved();
    void editingFinished();
    void columnResized(const QString& column, int width);  // by the user
    void sortChanged(int column, bool ascending);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    // Marks are not Qt's selection, so the drag start is ours too.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void currentChanged(const QModelIndex& current, const QModelIndex& previous) override;
    void closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void fitName();  // Name takes the width the other columns leave
    void onSectionResized(int column, int oldSize, int newSize);

    bool active_ = false;
    bool paintedRows_ = false;
    bool benchReporter_ = false;
    QPoint pressPos_;
    int pressRow_ = -1;
    bool applying_ = false;
};
