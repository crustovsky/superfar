// A pane: tab strip, path bar, the tabs, status line. The active pane
// draws an accent border and a highlighted path bar, both from palette
// roles.
#pragma once

#include "core/location.h"
#include "keymap.h"

#include <QWidget>

class QLabel;
class QStackedWidget;
class QTabBar;
class QTimer;
class Tab;

class Pane : public QWidget {
    Q_OBJECT
public:
    explicit Pane(QWidget* parent = nullptr);

    Tab* activeTab() const;
    int tabCount() const;
    Tab* newTab(const core::Location& dir);

    void setActive(bool active);
    bool isActive() const { return active_; }

    // Runs a pane-scoped command, else hands it to the active tab.
    bool execute(keymap::Command command);
    // Shows text in the status line for a moment.
    void showMessage(const QString& text);

Q_SIGNALS:
    void locationChanged(const core::Location& dir);  // of the active tab
    void commandRequested(keymap::Command command);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool hasFocusInside() const;
    void closeTab(int index);
    void onCurrentTab(int index);
    void showStatus(const QString& items, const QString& space);
    static QString title(const core::Location& dir);

    QTabBar* tabBar_;
    QLabel* path_;
    QStackedWidget* stack_;
    QLabel* status_;
    QLabel* space_;
    QTimer* messageTimer_;
    QString statusText_;
    bool active_ = false;
};
