// A pane: tab strip, path bar, the tabs, status line. The active pane
// draws an accent border and a highlighted path bar, both from palette
// roles.
#pragma once

#include "core/location.h"
#include "keymap.h"
#include "pane/tab.h"

#include <QWidget>

class QLabel;
class QStackedWidget;
class QTabBar;
class QTimer;
class Viewer;

namespace core {
class DirSizePool;
}

class Pane : public QWidget {
    Q_OBJECT
public:
    explicit Pane(core::DirSizePool* sizes, QWidget* parent = nullptr);

    Tab* activeTab() const;
    int tabCount() const;
    QList<Tab*> tabs() const;
    Tab* newTab(const core::Location& dir);

    void setActive(bool active);
    bool isActive() const { return active_; }

    // Runs a pane-scoped command, else hands it to the active tab.
    bool execute(keymap::Command command);
    // Shows text in the status line for a moment.
    void showMessage(const QString& text);
    // Something shown in place of the tabs: the viewer, or the preview
    // borrowed by the other pane. Null clears it.
    void setOverlay(QWidget* widget);
    bool hasOverlay() const { return overlay_ != nullptr; }

Q_SIGNALS:
    void locationChanged(const core::Location& dir);  // of the active tab
    void cursorChanged(const core::Location& item);   // of the active tab
    void commandRequested(keymap::Command command);
    void tabOpened(Tab* tab);

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
    QStackedWidget* content_;  // the tab stack, or an overlay
    QStackedWidget* stack_;
    Viewer* viewer_ = nullptr;
    QWidget* overlay_ = nullptr;
    QLabel* status_;
    QLabel* space_;
    QTimer* messageTimer_;
    core::DirSizePool* sizes_;
    QString statusText_;
    Tab* current_ = nullptr;
    bool active_ = false;
};
