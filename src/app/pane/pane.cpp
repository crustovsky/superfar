#include "pane/pane.h"

#include "elidedlabel.h"
#include "pane/listview.h"
#include "help.h"
#include "multirename.h"
#include "properties.h"
#include "pane/tab.h"
#include "services.h"
#include "viewer.h"

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

} // namespace

Pane::Pane(const Services& services, QWidget* parent)
    : QWidget(parent)
    , tabBar_(new QTabBar(this))
    , path_(new ElidedLabel(this))
    , content_(new QStackedWidget(this))
    , stack_(new QStackedWidget(this))
    , status_(new QLabel(this))
    , space_(new QLabel(this))
    , messageTimer_(new QTimer(this))
    , services_(services)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kBorder, kBorder, kBorder, kBorder);
    layout->setSpacing(0);
    layout->addWidget(path_);
    content_->addWidget(stack_);
    layout->addWidget(content_, 1);
    layout->addWidget(tabBar_);

    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->addWidget(status_, 1);
    statusRow->addWidget(space_);
    layout->addLayout(statusRow);

    tabBar_->setShape(QTabBar::RoundedSouth);
    tabBar_->setDocumentMode(true);
    tabBar_->setExpanding(false);
    tabBar_->setDrawBase(false);
    tabBar_->setElideMode(Qt::ElideRight);
    tabBar_->setUsesScrollButtons(true);
    tabBar_->setFocusPolicy(Qt::NoFocus);

    path_->setTextFormat(Qt::PlainText);
    path_->setContentsMargins(4, 2, 4, 2);
    path_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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

int Pane::activeIndex() const
{
    return stack_->currentIndex();
}

QList<Config::TabState> Pane::tabStates() const
{
    QList<Config::TabState> out;
    for (Tab* tab : tabs())
        out.append({tab->location().uri(), tab->showHidden(), tab->isLocked(), tab->recursiveSizes(), tab->thumbnails()});
    return out;
}

void Pane::restore(const QList<Config::TabState>& states, int active)
{
    for (const Config::TabState& s : states) {
        const core::Location dir = core::Location::fromUri(s.uri);
        if (!dir.isValid())
            continue;
        Tab* tab = newTab(dir);
        tab->setShowHidden(s.hidden);
        tab->setRecursiveSizes(s.sizes);
        tab->setThumbnails(s.thumbs);
        tab->setLocked(s.locked);
        retitle(tab);
    }
    if (active >= 0 && active < stack_->count())
        tabBar_->setCurrentIndex(active);
}

void Pane::applyColumns(const Config::Columns& columns)
{
    columns_ = columns;
    for (Tab* tab : tabs())
        tab->view()->applyColumns(columns.widths, columns.hidden);
}

void Pane::retitle(Tab* tab)
{
    const int i = stack_->indexOf(tab);
    if (i < 0)
        return;
    QString t = tab->searchPattern().isEmpty() ? title(tab->location()) : tr("Find %1").arg(tab->searchPattern());
    tabBar_->setTabText(i, tab->isLocked() ? QStringLiteral("[%1]").arg(t) : t);
}

int Pane::tabCount() const
{
    return stack_->count();
}

QList<Tab*> Pane::tabs() const
{
    QList<Tab*> out;
    for (int i = 0; i < stack_->count(); ++i)
        out.append(qobject_cast<Tab*>(stack_->widget(i)));
    return out;
}

Tab* Pane::makeTab(const core::Location& dir)
{
    auto* tab = new Tab(services_, this);
    tab->setActive(active_);
    tab->setCurrent(false);
    const int index = stack_->addWidget(tab);
    tabBar_->addTab(title(dir));

    connect(tab, &Tab::locationChanged, this, [this, tab](const core::Location& loc) {
        retitle(tab);
        if (stack_->indexOf(tab) == stack_->currentIndex()) {
            path_->setText(loc.displayPath());
            Q_EMIT locationChanged(loc);
        }
        Q_EMIT sessionChanged();
    });
    connect(tab, &Tab::openInNewTab, this, [this](const core::Location& dir) { newTab(dir); });
    connect(tab->view(), &ListView::columnResized, this, &Pane::columnResized);
    tab->view()->applyColumns(columns_.widths, columns_.hidden);
    connect(tab, &Tab::statusChanged, this, [this, tab](const QString& items, const QString& space) {
        if (stack_->indexOf(tab) == stack_->currentIndex())
            showStatus(items, space);
    });
    connect(tab, &Tab::commandRequested, this, &Pane::commandRequested);
    connect(tab, &Tab::cursorChanged, this, [this, tab](const core::Location& item) {
        if (stack_->indexOf(tab) == stack_->currentIndex())
            Q_EMIT cursorChanged(item);
    });
    connect(tab, &Tab::message, this, &Pane::showMessage);

    Q_EMIT tabOpened(tab);  // before the first navigation, so it counts as a visit
    tabBar_->setCurrentIndex(index);
    return tab;
}

Tab* Pane::newTab(const core::Location& dir)
{
    Tab* tab = makeTab(dir);
    tab->navigate(dir);
    Q_EMIT sessionChanged();
    return tab;
}

Tab* Pane::newSearchTab(const core::Location& root, const QString& pattern)
{
    Tab* tab = makeTab(root);
    tab->search(root, pattern);
    Q_EMIT sessionChanged();
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
    Q_EMIT sessionChanged();
}

void Pane::onCurrentTab(int index)
{
    if (index < 0 || index >= stack_->count())
        return;
    stack_->setCurrentIndex(index);
    Tab* tab = activeTab();
    if (current_ && current_ != tab)
        current_->setCurrent(false);
    current_ = tab;
    tab->setCurrent(true);
    path_->setText(tab->location().displayPath());
    showStatus(QString(), QString());
    if (hasFocusInside())
        tab->setFocus();
    Q_EMIT locationChanged(tab->location());
    Q_EMIT cursorChanged(tab->cursorLocation());
    tab->refreshStatus();
    Q_EMIT sessionChanged();
}

void Pane::closeOwnOverlay()
{
    setOverlay(nullptr);
    if (Tab* tab = activeTab())
        tab->setFocus();
}

Viewer* Pane::ensureViewer()
{
    if (!viewer_) {
        viewer_ = new Viewer(this);
        connect(viewer_, &Viewer::closed, this, [this] {
            setOverlay(nullptr);
            activeTab()->setFocus();
        });
    }
    return viewer_;
}

void Pane::setOverlay(QWidget* widget)
{
    if (overlay_ == widget)
        return;
    if (overlay_) {
        content_->removeWidget(overlay_);
        if (overlay_ == viewer_)
            viewer_->clear();
        else if (overlay_ != props_ && overlay_ != rename_)
            overlay_->setParent(nullptr);  // the preview belongs to the window
        overlay_ = nullptr;
    }
    if (widget) {
        overlay_ = widget;
        content_->addWidget(widget);
        content_->setCurrentWidget(widget);
    } else {
        content_->setCurrentWidget(stack_);
    }
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
    case Command::LockTab:
        activeTab()->setLocked(!activeTab()->isLocked());
        retitle(activeTab());
        showMessage(activeTab()->isLocked() ? tr("Tab locked") : tr("Tab unlocked"));
        Q_EMIT sessionChanged();
        return true;
    case Command::Cancel:
        if (overlay_ && (overlay_ == viewer_ || overlay_ == props_ || overlay_ == rename_)) {
            closeOwnOverlay();
            return true;
        }
        return activeTab() && activeTab()->execute(command);
    case Command::MultiRename: {
        Tab* tab = activeTab();
        const auto items = tab ? tab->selectedLocations() : std::vector<core::Location>();
        if (items.empty())
            return true;
        if (!rename_) {
            rename_ = new MultiRenamePanel(services_.config, this);
            connect(rename_, &MultiRenamePanel::closed, this, &Pane::closeOwnOverlay);
            connect(rename_, &MultiRenamePanel::message, this, &Pane::showMessage);
            connect(rename_, &MultiRenamePanel::renamed, this, [this](const QByteArray& first) {
                if (Tab* t = activeTab())
                    t->reloadWithCursor(first);
            });
        }
        rename_->show(items, tab->entryNames());
        setOverlay(rename_);
        rename_->setFocus();
        return true;
    }
    case Command::Properties: {
        Tab* tab = activeTab();
        const auto items = tab ? tab->selectedLocations() : std::vector<core::Location>();
        if (items.empty())
            return true;
        if (!props_) {
            props_ = new PropertiesPanel(services_.sizes, this);
            connect(props_, &PropertiesPanel::closed, this, &Pane::closeOwnOverlay);
            connect(props_, &PropertiesPanel::message, this, &Pane::showMessage);
            connect(props_, &PropertiesPanel::changed, this, [this] {
                if (Tab* t = activeTab())
                    t->reload();
            });
        }
        props_->show(items);
        setOverlay(props_);
        props_->setFocus();
        return true;
    }
    case Command::Help:
        ensureViewer()->showText(tr("Help"), help::text());
        setOverlay(viewer_);
        viewer_->setFocus();
        return true;
    case Command::View: {
        Tab* tab = activeTab();
        if (!tab || tab->execute(command))
            return true;  // folders: sizes
        const core::Location file = tab->cursorLocation();
        if (!file.isValid())
            return true;
        ensureViewer()->open(file);
        setOverlay(viewer_);
        viewer_->setFocus();
        return true;
    }
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
