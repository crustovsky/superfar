// The tab strip under a pane's list. The current tab is painted bold in
// the highlight colour so it stands out in themes that barely tint it.
// Middle click closes a tab, a double click on the empty strip opens one.
#pragma once

#include <QTabBar>

class TabBar : public QTabBar {
    Q_OBJECT
public:
    explicit TabBar(QWidget* parent = nullptr);

Q_SIGNALS:
    void closeRequested(int index);
    void newTabRequested();

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
};
