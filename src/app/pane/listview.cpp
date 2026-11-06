#include "pane/listview.h"

#include "bench.h"
#include "pane/delegate.h"
#include "pane/listmodel.h"

#include <QFontMetrics>
#include <QHeaderView>
#include <QApplication>
#include <QDrag>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <algorithm>

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
    // Only when Name's floor and the other columns do not fit the pane.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    header()->setStretchLastSection(false);
    // Columns are dragged into place; Name stays first.
    header()->setSectionsMovable(true);
    header()->setFirstSectionMovable(false);
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QWidget::customContextMenuRequested, this, &ListView::showColumnMenu);
    connect(header(), &QHeaderView::sectionMoved, this, [this] {
        if (!applying_)
            Q_EMIT columnsChanged(columnLayout());
    });
    // Sorting stays ours (the model orders its vector); the header only
    // keeps the indicator: a click flips it and reports the new order.
    header()->setSectionsClickable(true);
    header()->setSortIndicatorShown(true);
    header()->setSortIndicator(ListModel::NameColumn, Qt::AscendingOrder);
    connect(header(), &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder order) {
        Q_EMIT sortChanged(column, order == Qt::AscendingOrder);
    });
    setItemDelegate(new RowDelegate(this));
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    connect(header(), &QHeaderView::sectionResized, this, &ListView::onSectionResized);
}

void ListView::onSectionResized(int column, int, int newSize)
{
    if (applying_)
        return;
    QHeaderView* h = header();
    if (column == ListModel::NameColumn) {
        // Dragging the edge between Name and the next column resizes that
        // column; Name itself only ever takes what is left. Qt reports the
        // drag as absolute sizes, so the next column is computed from the
        // width Name asks for, not from a delta that would compound.
        int next = -1, rest = 0;
        for (int v = h->visualIndex(column) + 1; v < h->count(); ++v) {
            const int c = h->logicalIndex(v);
            if (h->isSectionHidden(c))
                continue;
            if (next < 0)
                next = c;
            else
                rest += h->sectionSize(c);
        }
        if (next >= 0) {
            const int width = std::max(h->minimumSectionSize(), viewport()->width() - newSize - rest);
            applying_ = true;
            h->resizeSection(next, width);
            applying_ = false;
            if (isVisible())
                Q_EMIT columnResized(columns::name(next), width);
        }
    } else if (isVisible()) {
        Q_EMIT columnResized(columns::name(column), newSize);
    }
    fitName();
}

void ListView::fitName()
{
    QHeaderView* h = header();
    if (!model() || h->count() < ListModel::ColumnCount)
        return;
    int others = 0;
    for (int c = 1; c < ListModel::ColumnCount; ++c)
        if (!h->isSectionHidden(c))
            others += h->sectionSize(c);
    const int width = std::max(nameFloor(), viewport()->width() - others);
    if (h->sectionSize(ListModel::NameColumn) == width)
        return;
    applying_ = true;
    h->resizeSection(ListModel::NameColumn, width);
    applying_ = false;
}

int ListView::nameFloor() const
{
    return iconSize().width() + QFontMetrics(font()).horizontalAdvance(QLatin1Char('x')) * 8;
}

void ListView::resizeEvent(QResizeEvent* event)
{
    QTreeView::resizeEvent(event);
    fitName();
}

void ListView::applyColumns(const columns::Layout& layout, const QHash<QString, int>& widths)
{
    QHeaderView* h = header();
    if (!model() || h->count() < ListModel::ColumnCount)
        return;
    sizeColumns();  // the font-metric widths, under whatever was saved
    applying_ = true;
    for (int v = 0; v < layout.order.size(); ++v)
        h->moveSection(h->visualIndex(layout.order[v]), v);
    for (int c = ListModel::NameColumn + 1; c < ListModel::ColumnCount; ++c) {
        setColumnHidden(c, layout.hidden.value(c));
        if (const int width = widths.value(columns::name(c)); width > 0)
            h->resizeSection(c, width);  // a hidden one keeps it for its return
    }
    applying_ = false;
    fitName();
}

columns::Layout ListView::columnLayout() const
{
    columns::Layout l;
    const QHeaderView* h = header();
    for (int v = 0; v < h->count(); ++v)
        l.order.append(h->logicalIndex(v));
    for (int c = 0; c < h->count(); ++c)
        l.hidden.append(h->isSectionHidden(c));
    return l;
}

void ListView::setColumnShown(int column, bool shown)
{
    if (column <= ListModel::NameColumn || column >= ListModel::ColumnCount || isColumnHidden(column) != shown)
        return;
    applying_ = true;
    setColumnHidden(column, !shown);
    applying_ = false;
    fitName();
    Q_EMIT columnsChanged(columnLayout());
}

void ListView::showColumnMenu(const QPoint& pos)
{
    // The same list the Columns command shows in a bar, here as a menu.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const columns::Layout layout = columnLayout();
    for (int c : layout.order) {
        if (c == ListModel::NameColumn)
            continue;
        QAction* a = menu->addAction(columns::label(c));
        a->setCheckable(true);
        a->setChecked(!layout.hidden.value(c));
        connect(a, &QAction::toggled, this, [this, c](bool on) { setColumnShown(c, on); });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Reset columns")), &QAction::triggered, this, &ListView::columnsResetRequested);
    menu->popup(header()->mapToGlobal(pos));
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
    // The others start at their widest string (rule 12) and can be
    // dragged at either edge; Name takes what is left (fitName).
    h->setSectionResizeMode(QHeaderView::Interactive);
    applying_ = true;
    for (int c = ListModel::NameColumn + 1; c < ListModel::ColumnCount; ++c)
        h->resizeSection(c, fm.horizontalAdvance(columns::widest(c)) + pad);
    applying_ = false;
    fitName();
}

void ListView::showSort(int column, bool ascending)
{
    header()->setSortIndicator(column, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
}

void ListView::setThumbnailRows(bool on)
{
    const int size = on ? 64 : style()->pixelMetric(QStyle::PM_SmallIconSize);
    setIconSize(QSize(size, size));
    // Uniform row heights take the first row's hint, so force a relayout.
    scheduleDelayedItemsLayout();
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
    if (isEditing()) {
        // Enter in the inline editor commits the name and the line edit
        // passes the key on; it must not open the file as well.
        QTreeView::keyPressEvent(event);
        return;
    }
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
    // Name is always shown and gives the row; the columns fill the
    // viewport, whichever of them are shown and in whatever order.
    const QRect name = visualRect(index.siblingAtColumn(ListModel::NameColumn));
    return QRect(0, name.top(), viewport()->width(), name.height());
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
