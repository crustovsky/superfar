#include "pane/pane.h"

#include "pane/tab.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kBorder = 2;
}

Pane::Pane(QWidget* parent)
    : QWidget(parent)
    , tabBar_(new QTabBar(this))
    , path_(new QLabel(this))
    , stack_(new QStackedWidget(this))
    , status_(new QLabel(this))
    , space_(new QLabel(this))
    , messageTimer_(new QTimer(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kBorder, kBorder, kBorder, kBorder);
    layout->setSpacing(0);
    layout->addWidget(tabBar_);
    layout->addWidget(path_);
    layout->addWidget(stack_, 1);

    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->addWidget(status_, 1);
    statusRow->addWidget(space_);
    layout->addLayout(statusRow);

    tabBar_->setDocumentMode(true);
    tabBar_->setExpanding(false);
    tabBar_->setDrawBase(false);
    tabBar_->setElideMode(Qt::ElideRight);
    tabBar_->setUsesScrollButtons(true);
    tabBar_->setFocusPolicy(Qt::NoFocus);

    path_->setTextFormat(Qt::PlainText);
    path_->setContentsMargins(4, 2, 4, 2);
    path_->setBackgroundRole(QPalette::Highlight);
    path_->setForegroundRole(QPalette::WindowText);
    status_->setTextFormat(Qt::PlainText);
    status_->setContentsMargins(4, 2, 4, 2);
    space_->setTextFormat(Qt::PlainText);
    space_->setContentsMargins(4, 2, 4, 2);

    messageTimer_->setSingleShot(true);
    messageTimer_->setInterval(2500);
    connect(messageTimer_, &QTimer::timeout, this, [this] { status_->setText(statusText_); });

    connect(tabBar_, &QTabBar::currentChanged, this, &Pane::onCurrentTab);
}

QString Pane::title(const core::Location& dir)
{
    const QString name = dir.displayName();
    return name.isEmpty() ? dir.displayPath() : name;
}

Tab* Pane::activeTab() const
{
    return qobject_cast<Tab*>(stack_->currentWidget());
}

int Pane::tabCount() const
{
    return stack_->count();
}

Tab* Pane::newTab(const core::Location& dir)
{
    auto* tab = new Tab(this);
    tab->setActive(active_);
    const int index = stack_->addWidget(tab);
    tabBar_->addTab(title(dir));

    connect(tab, &Tab::locationChanged, this, [this, tab](const core::Location& loc) {
        const int i = stack_->indexOf(tab);
        tabBar_->setTabText(i, title(loc));
        if (i == stack_->currentIndex()) {
            path_->setText(loc.displayPath());
            Q_EMIT locationChanged(loc);
        }
    });
    connect(tab, &Tab::statusChanged, this, [this, tab](const QString& items, const QString& space) {
        if (stack_->indexOf(tab) == stack_->currentIndex())
            showStatus(items, space);
    });
    connect(tab, &Tab::commandRequested, this, &Pane::commandRequested);

    tab->navigate(dir);
    tabBar_->setCurrentIndex(index);
    return tab;
}

bool Pane::hasFocusInside() const
{
    QWidget* f = QApplication::focusWidget();
    return f && (f == this || isAncestorOf(f));
}

void Pane::closeTab(int index)
{
    if (stack_->count() <= 1 || index < 0 || index >= stack_->count())
        return;
    // Move to the neighbour first so focus never leaves the pane while
    // the old tab goes away.
    if (index == stack_->currentIndex())
        tabBar_->setCurrentIndex(index > 0 ? index - 1 : 1);
    QWidget* w = stack_->widget(index);
    stack_->removeWidget(w);
    tabBar_->removeTab(index);
    w->deleteLater();
}

void Pane::onCurrentTab(int index)
{
    if (index < 0 || index >= stack_->count())
        return;
    stack_->setCurrentIndex(index);
    Tab* tab = activeTab();
    path_->setText(tab->location().displayPath());
    showStatus(QString(), QString());
    if (hasFocusInside())
        tab->setFocus();
    Q_EMIT locationChanged(tab->location());
    tab->refreshStatus();
}

void Pane::setActive(bool active)
{
    if (active == active_)
        return;
    active_ = active;
    path_->setAutoFillBackground(active);
    path_->setForegroundRole(active ? QPalette::HighlightedText : QPalette::WindowText);
    for (int i = 0; i < stack_->count(); ++i)
        qobject_cast<Tab*>(stack_->widget(i))->setActive(active);
    update();
}

bool Pane::execute(keymap::Command command)
{
    using keymap::Command;
    const int n = stack_->count();
    const int cur = stack_->currentIndex();
    switch (command) {
    case Command::NewTab:
        newTab(activeTab()->location());
        return true;
    case Command::CloseTab:
        closeTab(cur);
        return true;
    case Command::NextTab:
        tabBar_->setCurrentIndex((cur + 1) % n);
        return true;
    case Command::PrevTab:
        tabBar_->setCurrentIndex((cur + n - 1) % n);
        return true;
    default:
        return activeTab() && activeTab()->execute(command);
    }
}

void Pane::showMessage(const QString& text)
{
    status_->setText(text);
    messageTimer_->start();
}

void Pane::showStatus(const QString& items, const QString& space)
{
    statusText_ = items;
    space_->setText(space);
    if (!messageTimer_->isActive())
        status_->setText(items);
}

void Pane::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (!active_)
        return;
    QPainter p(this);
    p.setPen(QPen(palette().color(QPalette::Highlight), kBorder));
    p.drawRect(rect().adjusted(kBorder / 2, kBorder / 2, -kBorder / 2 - 1, -kBorder / 2 - 1));
}
