// The tab strip under a pane's list. The current tab is painted bold in
// the highlight colour so it stands out in themes that barely tint it.
#pragma once

#include <QTabBar>

class TabBar : public QTabBar {
    Q_OBJECT
public:
    explicit TabBar(QWidget* parent = nullptr);

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
};
