// A pane: tab strip, path bar, the tabs, status line. The active pane
// draws an accent border and a highlighted path bar, both from palette
// roles.
#pragma once

#include "config.h"
#include "core/location.h"
#include "keymap.h"
#include "pane/tab.h"

#include <QWidget>

class QLabel;
class QStackedWidget;
class QTabBar;
class MultiRenamePanel;
class PropertiesPanel;
class QTimer;
class Viewer;

struct Services;

class Pane : public QWidget {
    Q_OBJECT
public:
    explicit Pane(const Services& services, QWidget* parent = nullptr);

    Tab* activeTab() const;
    int activeIndex() const;
    int tabCount() const;
    QList<Tab*> tabs() const;
    Tab* newTab(const core::Location& dir);
    Tab* newSearchTab(const core::Location& root, const QString& pattern);
    // Session: what to save, and how to come back.
    QList<Config::TabState> tabStates() const;
    void restore(const QList<Config::TabState>& states, int active);
    void applyColumns(const Config::Columns& columns);

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
    void sessionChanged();  // tabs opened, closed, moved to, or locked
    void columnResized(const QString& column, int width);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool hasFocusInside() const;
    void closeTab(int index);
    void onCurrentTab(int index);
    void showStatus(const QString& items, const QString& space);
    void retitle(Tab* tab);
    Viewer* ensureViewer();
    Tab* makeTab(const core::Location& dir);  // adds and wires a tab without loading it
    void closeOwnOverlay();
    static QString title(const core::Location& dir);

    Config::Columns columns_;

    QTabBar* tabBar_;
    QLabel* path_;
    QStackedWidget* content_;  // the tab stack, or an overlay
    QStackedWidget* stack_;
    Viewer* viewer_ = nullptr;
    PropertiesPanel* props_ = nullptr;
    MultiRenamePanel* rename_ = nullptr;
    QWidget* overlay_ = nullptr;
    QLabel* status_;
    QLabel* space_;
    QTimer* messageTimer_;
    const Services& services_;
    QString statusText_;
    Tab* current_ = nullptr;
    bool active_ = false;
};
