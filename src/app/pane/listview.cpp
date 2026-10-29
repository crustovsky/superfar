#include "pane/listview.h"

#include "bench.h"
#include "pane/listmodel.h"

#include <QFontMetrics>
#include <QHeaderView>
#include <QKeyEvent>

ListView::ListView(QWidget* parent)
    : QTreeView(parent)
{
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setExpandsOnDoubleClick(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionBehavior(SelectRows);
    setSelectionMode(SingleSelection);
    setEditTriggers(NoEditTriggers);
    setSortingEnabled(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    header()->setStretchLastSection(false);
    header()->setSectionsMovable(false);
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
    h->setSectionResizeMode(ListModel::ModifiedColumn, QHeaderView::Fixed);
    h->resizeSection(ListModel::SizeColumn, fm.horizontalAdvance(QStringLiteral("8888.8 MiB")) + pad);
    h->resizeSection(ListModel::ModifiedColumn,
                     fm.horizontalAdvance(QStringLiteral("8888-88-88 88:88")) + pad);
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Backspace && event->modifiers() == Qt::NoModifier) {
        Q_EMIT goUpRequested();
        event->accept();
        return;
    }
    QTreeView::keyPressEvent(event);
}

void ListView::paintEvent(QPaintEvent* event)
{
    QTreeView::paintEvent(event);
    if (!paintedRows_ && model() && model()->rowCount() > 0) {
        paintedRows_ = true;
        bench::mark("first-rows");
    }
}
