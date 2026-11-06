#include "pane/tabbar.h"

#include "menu.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QStyleOptionTab>
#include <QStylePainter>

TabBar::TabBar(QWidget* parent)
    : QTabBar(parent)
{
    connect(this, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index < 0)
            Q_EMIT newTabRequested();
    });
}

void TabBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        if (const int index = tabAt(event->pos()); index >= 0) {
            Q_EMIT closeRequested(index);
            return;
        }
    }
    QTabBar::mouseReleaseEvent(event);
}

void TabBar::contextMenuEvent(QContextMenuEvent* event)
{
    // The commands are for the current tab, so the tab under the
    // pointer becomes it first.
    if (const int index = tabAt(event->pos()); index >= 0)
        setCurrentIndex(index);
    menu::popup(this, menu::Context::Tabs, QString(), event->globalPos(),
                [this](keymap::Command command) { Q_EMIT commandRequested(command); });
    event->accept();
}

QSize TabBar::tabSizeHint(int index) const
{
    // Every tab is measured bold, so nothing shifts when the current one
    // changes.
    QSize size = QTabBar::tabSizeHint(index);
    QFont bold = font();
    bold.setBold(true);
    size.rwidth() += QFontMetrics(bold).horizontalAdvance(tabText(index)) - fontMetrics().horizontalAdvance(tabText(index));
    return size;
}

void TabBar::paintEvent(QPaintEvent*)
{
    QStylePainter p(this);
    const int current = currentIndex();
    auto draw = [&](int i) {
        QStyleOptionTab opt;
        initStyleOption(&opt, i);
        if (!opt.rect.intersects(rect()))
            return;  // scrolled out
        if (i == current) {
            QFont bold = font();
            bold.setBold(true);
            p.setFont(bold);
            const QColor accent = palette().color(QPalette::Highlight);
            opt.palette.setColor(QPalette::WindowText, accent);
            opt.palette.setColor(QPalette::ButtonText, accent);
            opt.palette.setColor(QPalette::Text, accent);
        }
        p.drawControl(QStyle::CE_TabBarTab, opt);
        p.setFont(font());
    };
    for (int i = 0; i < count(); ++i)
        if (i != current)
            draw(i);
    if (current >= 0)
        draw(current);  // last, so it overlaps its neighbours like Qt does
}
