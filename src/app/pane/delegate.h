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

private:
    ListView* view_;
};
