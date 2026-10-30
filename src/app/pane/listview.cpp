#include "pane/listview.h"

#include "bench.h"
#include "pane/delegate.h"
#include "pane/listmodel.h"

#include <QFontMetrics>
#include <QLocale>
#include <QHeaderView>
#include <QApplication>
#include <QDrag>
#include <QKeyEvent>
#include <QMouseEvent>
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
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    connect(header(), &QHeaderView::sectionResized, this, [this](int column, int, int width) {
        if (!applying_ && column != ListModel::NameColumn && isVisible())
            Q_EMIT columnResized(columnName(column), width);
    });
}

QString ListView::columnName(int column)
{
    switch (column) {
    case ListModel::NameColumn: return QStringLiteral("name");
    case ListModel::SizeColumn: return QStringLiteral("size");
    case ListModel::ItemsColumn: return QStringLiteral("items");
    case ListModel::ModifiedColumn: return QStringLiteral("modified");
    default: return {};
    }
}

void ListView::applyColumns(const QHash<QString, int>& widths, const QStringList& hidden)
{
    applying_ = true;
    for (int c = 0; c < ListModel::ColumnCount; ++c) {
        const QString name = columnName(c);
        setColumnHidden(c, hidden.contains(name) && c != ListModel::NameColumn);
        if (const auto it = widths.constFind(name); it != widths.cend() && it.value() > 0 && c != ListModel::NameColumn)
            header()->resizeSection(c, it.value());
    }
    applying_ = false;
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        pressPos_ = event->pos();
        pressRow_ = indexAt(event->pos()).row();
    }
    QTreeView::mousePressEvent(event);
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && pressRow_ >= 0
        && (event->pos() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
        startDrag(Qt::CopyAction | Qt::MoveAction);
        pressRow_ = -1;
        return;
    }
    QTreeView::mouseMoveEvent(event);
}

void ListView::startDrag(Qt::DropActions supportedActions)
{
    auto* m = qobject_cast<ListModel*>(model());
    if (!m || pressRow_ < 0)
        return;
    const auto files = m->dragLocations(pressRow_);
    if (files.empty())
        return;
    auto* drag = new QDrag(this);
    drag->setMimeData(m->mimeData({m->index(pressRow_, 0)}));
    drag->setPixmap(m->index(pressRow_, 0).data(Qt::DecorationRole).value<QIcon>().pixmap(32));
    drag->exec(supportedActions, Qt::MoveAction);
}

void ListView::sizeColumns()
{
    QHeaderView* h = header();
    if (!model() || h->count() < ListModel::ColumnCount)
        return;
    const QFontMetrics fm(font());
    const int pad = fm.horizontalAdvance(QLatin1Char('x')) * 2;
    // Name takes what is left; the others start at their widest string
    // (rule 12) and can be dragged.
    h->setSectionResizeMode(ListModel::NameColumn, QHeaderView::Stretch);
    h->setSectionResizeMode(ListModel::SizeColumn, QHeaderView::Interactive);
    h->setSectionResizeMode(ListModel::ItemsColumn, QHeaderView::Interactive);
    h->setSectionResizeMode(ListModel::ModifiedColumn, QHeaderView::Interactive);
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
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint()
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        event->accept();
        Q_EMIT textTyped(text);
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

void ListView::closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint)
{
    QTreeView::closeEditor(editor, hint);
    if (!isEditing())
        Q_EMIT editingFinished();
}

void ListView::currentChanged(const QModelIndex& current, const QModelIndex& previous)
{
    QTreeView::currentChanged(current, previous);
    if (previous.isValid())
        viewport()->update(rowRect(previous));
    if (current.isValid())
        viewport()->update(rowRect(current));
    Q_EMIT cursorMoved();
}
