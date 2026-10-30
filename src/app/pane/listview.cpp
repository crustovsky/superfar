#include "pane/listview.h"

#include "bench.h"
#include "pane/delegate.h"
#include "pane/listmodel.h"

#include <QFontMetrics>
#include <QLocale>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPainter>

ListView::ListView(QWidget* parent)
    : QTreeView(parent)
{
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setExpandsOnDoubleClick(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionBehavior(SelectRows);
    setSelectionMode(NoSelection);  // marks are the model's, not Qt's
    setEditTriggers(NoEditTriggers);
    setSortingEnabled(false);
    setTabKeyNavigation(true);  // so Tab reaches keyPressEvent
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    header()->setStretchLastSection(false);
    header()->setSectionsMovable(false);
    setItemDelegate(new RowDelegate(this));
}

void ListView::sizeColumns()
{
    QHeaderView* h = header();
    if (!model() || h->count() < ListModel::ColumnCount)
        return;
    const QFontMetrics fm(font());
    const int pad = fm.horizontalAdvance(QLatin1Char('x')) * 2;
    h->setSectionResizeMode(ListModel::NameColumn, QHeaderView::Stretch);
    h->setSectionResizeMode(ListModel::SizeColumn, QHeaderView::Fixed);
    h->setSectionResizeMode(ListModel::ItemsColumn, QHeaderView::Fixed);
    h->setSectionResizeMode(ListModel::ModifiedColumn, QHeaderView::Fixed);
    h->resizeSection(ListModel::SizeColumn, fm.horizontalAdvance(QStringLiteral("8888.8 MiB")) + pad);
    h->resizeSection(ListModel::ItemsColumn, fm.horizontalAdvance(QLocale().toString(88888888)) + pad);
    h->resizeSection(ListModel::ModifiedColumn,
                     fm.horizontalAdvance(QStringLiteral("8888-88-88 88:88")) + pad);
}

void ListView::setActivePane(bool active)
{
    if (active == active_)
        return;
    active_ = active;
    viewport()->update();
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    const keymap::Command cmd = keymap::lookup(event);
    if (cmd != keymap::Command::None) {
        event->accept();
        Q_EMIT commandRequested(cmd);
        return;
    }
    QTreeView::keyPressEvent(event);
}

QRect ListView::rowRect(const QModelIndex& index) const
{
    const int last = model()->columnCount() - 1;
    return visualRect(index.siblingAtColumn(0)).united(visualRect(index.siblingAtColumn(last)));
}

void ListView::paintEvent(QPaintEvent* event)
{
    QTreeView::paintEvent(event);
    if (!active_ && currentIndex().isValid()) {
        QPainter p(viewport());
        p.setPen(QPen(palette().color(QPalette::Highlight), 1));
        p.drawRect(rowRect(currentIndex()).adjusted(0, 0, -1, -1));
    }
    if (benchReporter_ && !paintedRows_ && model() && model()->rowCount() > 0) {
        paintedRows_ = true;
        bench::mark("first-rows");
    }
}

void ListView::currentChanged(const QModelIndex& current, const QModelIndex& previous)
{
    QTreeView::currentChanged(current, previous);
    if (previous.isValid())
        viewport()->update(rowRect(previous));
    if (current.isValid())
        viewport()->update(rowRect(current));
}
