#include "pane/delegate.h"

#include "pane/listmodel.h"
#include "pane/listview.h"

#include <QApplication>
#include <QLineEdit>
#include <QPainter>

RowDelegate::RowDelegate(ListView* view)
    : QStyledItemDelegate(view)
    , view_(view)
{
}

void RowDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    QStyledItemDelegate::setEditorData(editor, index);
    auto* edit = qobject_cast<QLineEdit*>(editor);
    if (!edit)
        return;
    const QString name = edit->text();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const bool isDir = index.data(ListModel::IsDirRole).toBool();
    edit->setSelection(0, static_cast<int>(dot > 0 && !isDir ? dot : name.size()));
}

void RowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);

    const auto* model = qobject_cast<const ListModel*>(index.model());
    const bool marked = model && model->isMarked(index.row());
    const bool cursor = view_->activePane() && view_->currentIndex().row() == index.row();

    if (cursor)
        opt.state |= QStyle::State_Selected;
    if (marked) {
        opt.font.setBold(true);
        if (!cursor)
            opt.palette.setColor(QPalette::Text, opt.palette.color(QPalette::Highlight));
    }
    if (index.data(ListModel::PartialRole).toBool())
        opt.font.setItalic(true);  // a folder total still being walked

    const QWidget* widget = opt.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
}
