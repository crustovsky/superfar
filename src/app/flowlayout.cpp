#include "flowlayout.h"

#include <QHBoxLayout>
#include <QWidget>

#include <algorithm>

FlowLayout::FlowLayout(int spacing)
    : spacing_(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
        delete item;
}

QWidget* FlowLayout::cell(std::initializer_list<QWidget*> widgets, QWidget* parent)
{
    auto* box = new QWidget(parent);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    bool expands = false;
    for (QWidget* w : widgets) {
        const bool grows = w->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag;
        row->addWidget(w, grows ? 1 : 0);
        expands = expands || grows;
    }
    box->setSizePolicy(expands ? QSizePolicy::Expanding : QSizePolicy::Fixed, QSizePolicy::Fixed);
    return box;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    items_.append(item);
}

int FlowLayout::count() const
{
    return static_cast<int>(items_.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return items_.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
    return arrange(QRect(0, 0, width, 0), false);
}

QSize FlowLayout::sizeHint() const
{
    // One line when there is room for it.
    int width = 0, height = 0;
    for (const QLayoutItem* item : items_) {
        if (item->isEmpty())
            continue;
        width += item->sizeHint().width() + (width > 0 ? spacing_ : 0);
        height = std::max(height, item->sizeHint().height());
    }
    return {width, height};
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : items_)
        if (!item->isEmpty())
            size = size.expandedTo(item->minimumSize());
    return size;
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, true);
}

// Fills lines from the left, each as tall as its tallest item; returns
// the height used. An item wider than the line is cut to it.
int FlowLayout::arrange(const QRect& rect, bool apply) const
{
    int y = rect.y();
    qsizetype first = 0;
    while (first < items_.size()) {
        // What goes on this line.
        qsizetype last = first;
        int used = 0, height = 0, growers = 0;
        for (; last < items_.size(); ++last) {
            const QLayoutItem* item = items_[last];
            if (item->isEmpty())
                continue;
            const int w = std::min(item->sizeHint().width(), rect.width());
            if (used > 0 && used + spacing_ + w > rect.width())
                break;
            used += w + (used > 0 ? spacing_ : 0);
            height = std::max(height, item->sizeHint().height());
            growers += (item->expandingDirections() & Qt::Horizontal) ? 1 : 0;
        }
        if (apply) {
            const int spare = std::max(0, rect.width() - used);
            int x = rect.x(), given = 0, grower = 0;
            for (qsizetype i = first; i < last; ++i) {
                QLayoutItem* item = items_[i];
                if (item->isEmpty())
                    continue;
                int w = std::min(item->sizeHint().width(), rect.width());
                if (item->expandingDirections() & Qt::Horizontal) {
                    ++grower;
                    const int share = grower == growers ? spare - given : spare / growers;
                    w += share;
                    given += share;
                }
                const int h = item->sizeHint().height();
                item->setGeometry(QRect(x, y + (height - h) / 2, w, h));
                x += w + spacing_;
            }
        }
        y += height + (last < items_.size() ? spacing_ : 0);
        first = last;
    }
    return y - rect.y();
}
