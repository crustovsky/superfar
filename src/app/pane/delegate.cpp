#include "pane/delegate.h"

#include "ownercompletion.h"
#include "pane/listmodel.h"
#include "pane/listview.h"

#include <QApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>

RowDelegate::RowDelegate(ListView* view)
    : QStyledItemDelegate(view)
    , view_(view)
{
}

QWidget* RowDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const
{
    QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (index.column() == ListModel::OwnerColumn)
        if (auto* edit = qobject_cast<QLineEdit*>(editor))
            completeOwners(edit);
    return editor;
}

QModelIndex RowDelegate::nextEditable(const QModelIndex& index, bool forward) const
{
    const QHeaderView* h = view_->header();
    const int n = h->count();
    for (int step = 1; step < n; ++step) {
        const int visual = (h->visualIndex(index.column()) + (forward ? step : n - step)) % n;
        const QModelIndex next = index.siblingAtColumn(h->logicalIndex(visual));
        if (!h->isSectionHidden(next.column()) && (next.flags() & Qt::ItemIsEditable))
            return next;
    }
    return {};
}

void RowDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    QStyledItemDelegate::setEditorData(editor, index);
    auto* edit = qobject_cast<QLineEdit*>(editor);
    if (!edit || index.column() != ListModel::NameColumn)
        return;  // a mode or an owner is replaced whole: the view selects all
    const QString name = edit->text();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const bool isDir = index.data(ListModel::IsDirRole).toBool();
    const int stem = static_cast<int>(dot > 0 && !isDir ? dot : name.size());
    // The view selects everything right after this call; the stem
    // selection is applied once that has happened.
    QMetaObject::invokeMethod(edit, [edit, stem] { edit->setSelection(0, stem); }, Qt::QueuedConnection);
}

bool RowDelegate::eventFilter(QObject* editor, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            auto* w = qobject_cast<QWidget*>(editor);
            Q_EMIT commitData(w);
            Q_EMIT closeEditor(w, key == Qt::Key_Down ? EditNextItem : EditPreviousItem);
            return true;
        }
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            // Along the row. With only the name to edit the key keeps
            // its old meaning: the next row.
            const QPersistentModelIndex next = nextEditable(view_->currentIndex(), key == Qt::Key_Tab);
            if (next.isValid()) {
                auto* w = qobject_cast<QWidget*>(editor);
                Q_EMIT commitData(w);
                Q_EMIT closeEditor(w, NoHint);
                // Once this editor is gone; a reload in between drops the index.
                QMetaObject::invokeMethod(view_, [view = view_, next] {
                    if (next.isValid()) {
                        view->setCurrentIndex(next);
                        view->edit(next);
                    }
                }, Qt::QueuedConnection);
                return true;
            }
        }
    }
    return QStyledItemDelegate::eventFilter(editor, event);
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
