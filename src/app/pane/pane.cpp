#include "counted.h"
#include "pane/pane.h"

#include "core/sort.h"

#include "elidedlabel.h"
#include "pane/listview.h"
#include "help.h"
#include "multirename.h"
#include "packpanel.h"
#include "properties.h"
#include "syncpanel.h"
#include "pane/tab.h"
#include "pane/tabbar.h"
#include "services.h"
#include "viewer.h"

#include <QApplication>
#include <QFileInfo>
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
    , tabBar_(new TabBar(this))
    , path_(new ElidedLabel(this))
    , content_(new QStackedWidget(this))
    , stack_(new QStackedWidget(this))
    , status_(new ElidedLabel(this, Qt::ElideMiddle))
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
    connect(tabBar_, &TabBar::closeRequested, this, &Pane::closeTab);
    connect(tabBar_, &TabBar::newTabRequested, this, [this] { newTab(core::Location::home()); });
    connect(tabBar_, &TabBar::commandRequested, this, [this](keymap::Command command) {
        // This pane's tabs, whichever pane is active: the strip takes no
        // focus, so the pane does it here.
        if (Tab* tab = activeTab())
            tab->setFocus();
        execute(command);
    });
    path_->installEventFilter(this);
}

bool Pane::eventFilter(QObject* watched, QEvent* event)
{
    // The path bar: a click brings the pane to the front, a double click
    // edits the path (Ctrl+L), for typing a remote location.
    if (watched == path_) {
        if (event->type() == QEvent::MouseButtonPress) {
            if (Tab* tab = activeTab())
                tab->setFocus();
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            Q_EMIT commandRequested(keymap::Command::EditPath);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
    for (Tab* tab : tabs()) {
        Config::TabState t;
        t.uri = tab->location().uri();
        t.hidden = tab->showHidden();
        t.locked = tab->isLocked();
        t.sizes = tab->recursiveSizes();
        t.thumbs = tab->thumbnails();
        t.cursor = QString::fromLatin1(tab->currentName().toPercentEncoding());
        t.sort = QLatin1String(core::sortName(tab->sortBy()));
        t.descending = !tab->sortAscending();
        out.append(t);
    }
    return out;
}

void Pane::restore(const QList<Config::TabState>& states, int active)
{
    for (const Config::TabState& s : states) {
        const core::Location dir = core::Location::fromUri(s.uri);
        if (!dir.isValid())
            continue;
        Tab* tab = newTab(dir);
        if (!s.cursor.isEmpty())
            tab->setCursorOnLoad(QByteArray::fromPercentEncoding(s.cursor.toLatin1()));
        tab->setShowHidden(s.hidden);
        tab->setRecursiveSizes(s.sizes);
        tab->setThumbnails(s.thumbs);
        tab->setSort(core::sortFromName(s.sort), !s.descending);
        tab->setLocked(s.locked);
        retitle(tab);
    }
    if (active >= 0 && active < stack_->count())
        tabBar_->setCurrentIndex(active);
}

void Pane::applyColumns(const Config::Columns& columns)
{
    columns_ = columns;
    const columns::Layout layout = columnLayout();
    for (Tab* tab : tabs())
        tab->view()->applyColumns(layout, columns.widths);
}

void Pane::moveColumn(int column, int delta)
{
    if (Tab* tab = activeTab())
        tab->view()->moveColumn(column, delta);  // comes back through columnsChanged
}

columns::Layout Pane::columnLayout() const
{
    return columns::Layout::from(columns_.order, columns_.hidden);
}

void Pane::setColumnShown(int column, bool shown)
{
    if (Tab* tab = activeTab())
        tab->view()->setColumnShown(column, shown);  // comes back through columnsChanged
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
            path_->setText(tab->displayPath());
            Q_EMIT locationChanged(loc);
        }
        Q_EMIT sessionChanged();
    });
    connect(tab, &Tab::openInNewTab, this, [this](const core::Location& dir) { newTab(dir); });
    connect(tab, &Tab::sortChanged, this, &Pane::sessionChanged);
    connect(tab->view(), &ListView::columnResized, this, [this](const QString& column, int width) {
        columns_.widths.insert(column, width);
        Q_EMIT columnResized(column, width);
    });
    connect(tab->view(), &ListView::columnsChanged, this, [this](const columns::Layout& layout) {
        columns_.order = layout.orderNames();
        columns_.hidden = layout.hiddenNames();
        Q_EMIT columnsChanged(columns_.order, columns_.hidden);
    });
    connect(tab->view(), &ListView::columnsResetRequested, this, &Pane::columnsResetRequested);
    tab->view()->applyColumns(columnLayout(), columns_.widths);
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
    tab->view()->applyColumns(columnLayout(), columns_.widths);  // what another tab changed meanwhile
    tab->setCurrent(true);
    path_->setText(tab->displayPath());
    showStatus(QString(), QString());
    if (hasFocusInside())
        tab->setFocus();
    Q_EMIT locationChanged(tab->location());
    Q_EMIT cursorChanged(tab->cursorLocation());
    tab->refreshStatus();
    Q_EMIT sessionChanged();
}

void Pane::openSync(const core::Location& other)
{
    Tab* tab = activeTab();
    if (!tab || !other.isValid())
        return;
    if (!sync_) {
        sync_ = new SyncPanel(this);
        connect(sync_, &SyncPanel::closed, this, &Pane::closeOwnOverlay);
        connect(sync_, &SyncPanel::message, this, &Pane::showMessage);
        connect(sync_, &SyncPanel::runJobs, this, &Pane::runJobs);
    }
    sync_->show(tab->location(), other);
    setOverlay(sync_);
    sync_->setFocus();
}

void Pane::openPack(const core::Location& other)
{
    Tab* tab = activeTab();
    if (!tab)
        return;
    if (tab->isArchive()) {
        showMessage(tr("Archives are read-only here"));
        return;
    }
    if (tab->location().path().isEmpty()) {
        showMessage(tr("Archives are made from local folders only"));
        return;
    }
    const auto items = tab->selectedLocations();
    if (items.empty())
        return;
    if (!pack_) {
        pack_ = new PackPanel(services_.config, this);
        connect(pack_, &PackPanel::closed, this, &Pane::closeOwnOverlay);
        connect(pack_, &PackPanel::accepted, this,
                [this](const QByteArray& name, const QByteArray& options, const core::Location& destination) {
                    Tab* t = activeTab();
                    if (!t)
                        return;
                    std::vector<QByteArray> names;
                    for (const core::Location& l : t->selectedLocations())
                        names.push_back(l.basename());
                    closeOwnOverlay();
                    Q_EMIT packRequested(t->location(), names, destination.child(name), options);
                });
    }
    // One item names the archive after itself (a file without its
    // extension); more take the folder's name.
    QByteArray stem = tab->location().basename();
    if (items.size() == 1) {
        stem = items.front().basename();
        const int dot = stem.lastIndexOf('.');
        if (dot > 0 && !QFileInfo(items.front().displayPath()).isDir())
            stem = stem.left(dot);
    }
    if (stem.isEmpty())
        stem = "archive";
    pack_->show(tab->selectionText(), stem, tab->location(), other);
    setOverlay(pack_);
    pack_->setFocus();
}

void Pane::refreshProperties()
{
    if (props_ && overlay_ == props_)
        props_->reread();
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
        else if (overlay_ != props_ && overlay_ != rename_ && overlay_ != sync_ && overlay_ != pack_)
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
        if (overlay_ && (overlay_ == viewer_ || overlay_ == props_ || overlay_ == rename_ || overlay_ == sync_ || overlay_ == pack_)) {
            closeOwnOverlay();
            return true;
        }
        return activeTab() && activeTab()->execute(command);
    case Command::MultiRename: {
        Tab* tab = activeTab();
        if (!tab)
            return true;
        const auto items = tab->selectedLocations();
        if (items.empty())
            return true;
        if (!rename_) {
            rename_ = new MultiRenamePanel(services_.config, this);
            connect(rename_, &MultiRenamePanel::closed, this, &Pane::closeOwnOverlay);
            connect(rename_, &MultiRenamePanel::message, this, &Pane::showMessage);
            connect(rename_, &MultiRenamePanel::batchDone, this,
                    [this](const std::vector<std::pair<core::Location, core::Location>>& steps) {
                        std::vector<core::UndoItem> items;
                        int temporary = 0;
                        for (const auto& [from, to] : steps) {
                            items.push_back(core::UndoItem::moved(from, to, to.isNative()));
                            temporary += to.basename().startsWith(".superfar-rename-") ? 1 : 0;
                        }
                        const auto renamed = static_cast<qint64>(steps.size()) - temporary;
                        Q_EMIT undoable(tr("rename of %1").arg(counted(renamed, tr("item"), tr("items"))), items);
                    });
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
            connect(props_, &PropertiesPanel::applyRequested, this, &Pane::attributesRequested);
        }
        props_->show(items);
        setOverlay(props_);
        props_->setFocus();
        return true;
    }
    case Command::Help:
        showText(tr("Help"), help::text());
        return true;
    case Command::About:
        showText(tr("About"), help::about());
        return true;
    case Command::View: {
        Tab* tab = activeTab();
        if (!tab || tab->execute(command))
            return true;  // folders: sizes
        if (tab->isArchive()) {
            showMessage(tr("F5 extracts the member first"));
            return true;
        }
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

void Pane::showText(const QString& title, const QString& text)
{
    ensureViewer()->showText(title, text);
    setOverlay(viewer_);
    viewer_->setFocus();
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
