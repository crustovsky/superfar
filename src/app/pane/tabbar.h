// The tab strip under a pane's list. The current tab is painted bold in
// the highlight colour so it stands out in themes that barely tint it.
// Middle click closes a tab, a double click on the empty strip opens
// one, a right click makes the tab current and lists the tab commands.
#pragma once

#include "keymap.h"

#include <QTabBar>

class TabBar : public QTabBar {
    Q_OBJECT
public:
    explicit TabBar(QWidget* parent = nullptr);

Q_SIGNALS:
    void closeRequested(int index);
    void newTabRequested();
    void commandRequested(keymap::Command command);  // from the right-click menu

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
};
