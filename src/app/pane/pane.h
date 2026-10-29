// A pane: path bar, the tabs, status line. Milestone 1 holds one tab; the
// tab strip arrives in milestone 2 but the pane / tab split is here now.
#pragma once

#include "core/location.h"

#include <QWidget>

class QLabel;
class QStackedWidget;
class Tab;

class Pane : public QWidget {
    Q_OBJECT
public:
    explicit Pane(QWidget* parent = nullptr);

    Tab* activeTab() const;

Q_SIGNALS:
    void locationChanged(const core::Location& dir);

private:
    QLabel* path_;
    QStackedWidget* tabs_;
    QLabel* status_;
};
