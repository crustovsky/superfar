// Paints a row's cursor and mark state. Colours come from palette roles
// only (rule 11): the cursor row in the active pane is the style's
// selection, a marked row is bold in the highlight colour. The inactive
// pane's cursor outline is drawn by the view, over the whole row.
#pragma once

#include <QStyledItemDelegate>

class ListView;

class RowDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit RowDelegate(ListView* view);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    // The owner cell's line edit completes user and group names.
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    // The rename editor: a line edit with the stem selected, so typing
    // replaces the name and keeps the extension.
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    // Up and Down commit the edit and open the row above or below; Tab
    // and Shift+Tab open the row's next cell that can be edited (name,
    // permissions, owner, as far as they are shown).
    bool eventFilter(QObject* editor, QEvent* event) override;

private:
    QModelIndex nextEditable(const QModelIndex& index, bool forward) const;

    ListView* view_;
};
