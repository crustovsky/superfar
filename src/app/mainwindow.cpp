#include "mainwindow.h"

#include "bench.h"
#include "cmdline.h"
#include "config.h"
#include "devaids.h"
#include "core/apps.h"
#include "core/dirsize.h"
#include "core/fileops.h"
#include "core/thumbnails.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "funcbar.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "pane/pane.h"
#include "pane/tab.h"
#include "places.h"
#include "preview.h"
#include "syncpanel.h"
#include "tree.h"
#include "uris.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QMimeData>
#include <QFile>
#include <QKeyEvent>
#include <QPointer>
#include <QSplitter>
#include <QTimer>
#include <memory>
#include <QVBoxLayout>

MainWindow::MainWindow(Config* config, const core::Location& left, const core::Location& right, QWidget* parent)
    : QMainWindow(parent)
    , config_(config)
    , sizes_(new core::DirSizePool(this))
    , thumbs_(new core::Thumbnailer(this))
    , services_{sizes_, thumbs_, config}
    , tree_(new Tree(this))
    , panes_(new QSplitter(config->stackedSplit() ? Qt::Vertical : Qt::Horizontal, this))
    , split_(new QSplitter(Qt::Horizontal, this))
    , vsplit_(new QSplitter(Qt::Vertical, this))
    , previewSlot_(new QWidget(this))
    , left_(new Pane(services_, this))
    , right_(new Pane(services_, this))
    , active_(left_)
    , funcbar_(new FuncBar(this))
    , strip_(new JobStrip(this))
    , cmdline_(new CommandLine(this))
    , sessionTimer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("superfar"));
    resize(config->windowSize().isValid() ? config->windowSize() : QSize(1100, 700));

    panes_->setChildrenCollapsible(false);
    panes_->addWidget(left_);
    panes_->addWidget(right_);
    panes_->addWidget(previewSlot_);
    previewSlot_->hide();
    auto* slotLayout = new QVBoxLayout(previewSlot_);
    slotLayout->setContentsMargins(0, 0, 0, 0);
    const QList<int> paneSizes = config->splitterSizes(QStringLiteral("panes"));
    panes_->setSizes(paneSizes.size() >= 2 ? paneSizes : QList<int>{500, 500});

    split_->setChildrenCollapsible(false);
    split_->addWidget(tree_);
    split_->addWidget(panes_);
    split_->setStretchFactor(0, 0);
    split_->setStretchFactor(1, 1);
    const QList<int> treeSizes = config->splitterSizes(QStringLiteral("tree"));
    split_->setSizes(treeSizes.size() == 2 ? treeSizes : QList<int>{220, 880});

    // The bar area sits under the panes behind a draggable handle; extra
    // height goes to a job's graph or a log list.
    vsplit_->addWidget(split_);
    vsplit_->addWidget(strip_);
    vsplit_->setStretchFactor(0, 1);
    vsplit_->setStretchFactor(1, 0);
    vsplit_->setCollapsible(0, false);
    vsplit_->setCollapsible(1, false);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(vsplit_, 1);
    layout->addWidget(cmdline_);
    layout->addWidget(funcbar_);
    setCentralWidget(central);

    connect(cmdline_, &CommandLine::run, this, &MainWindow::runCommand);
    connect(cmdline_, &CommandLine::dismissed, this, [this] { active_->activeTab()->setFocus(); });
    connect(cmdline_, &CommandLine::insertNameRequested, this, [this] {
        const core::Location item = active_->activeTab()->cursorLocation();
        if (item.isValid())
            cmdline_->insert(QString::fromUtf8(core::shellQuote(item.basename())) + QLatin1Char(' '));
    });

    for (Pane* pane : {left_, right_}) {
        const QString side = pane == left_ ? QStringLiteral("left") : QStringLiteral("right");
        pane->applyColumns(config_->columns(side));
        connect(pane, &Pane::columnResized, this, [this, side](const QString& column, int width) {
            config_->setColumnWidth(side, column, width);
        });
        connect(pane, &Pane::sessionChanged, this, [this] { sessionTimer_->start(); });
        connect(pane, &Pane::runJobs, this, [this, pane](std::vector<core::Job*> jobs) {
            // Sync jobs: each refreshes both folders when done; the panel
            // compares again after the last one.
            auto remaining = std::make_shared<int>(static_cast<int>(jobs.size()));
            for (core::Job* job : jobs) {
                connect(job, &core::Job::finished, this, [this, pane, remaining] {
                    for (Pane* p : {left_, right_})
                        if (Tab* t = p->activeTab())
                            if (!t->monitoring())
                                t->reload();
                    if (--*remaining == 0)
                        if (auto* sync = pane->findChild<SyncPanel*>())
                            sync->compare();
                });
                strip_->addJob(job);
            }
        });
        connect(pane, &Pane::locationChanged, this, [this, pane](const core::Location& dir) {
            if (pane == active_) {
                tree_->reveal(dir);
                cmdline_->setFolder(dir.displayPath());
            }
        });
        connect(pane, &Pane::cursorChanged, this, [this, pane](const core::Location& item) {
            if (pane == active_ && preview_)
                preview_->show(item);
        });
        connect(pane, &Pane::commandRequested, this, &MainWindow::execute);
    }
    connect(strip_, &JobStrip::jobFinished, this, [](core::Job*) {});
    connect(tree_, &Tree::folderChosen, this,
            [this](const core::Location& dir) { active_->activeTab()->navigate(dir); });
    places_ = new Places(tree_, strip_, this);
    connect(places_, &Places::openFolder, this, [this](const core::Location& dir) { active_->activeTab()->navigate(dir); });
    connect(places_, &Places::message, this, [this](const QString& text) { active_->showMessage(text); });
    connect(places_, &Places::leavingMount, this, [this](const core::Location& root) {
        // Tabs inside the mount leave it first, or the unmount is refused.
        for (Pane* pane : {left_, right_})
            for (Tab* tab : pane->tabs())
                if (!tab->location().relativePathFrom(root).isNull())
                    tab->navigate(core::Location::home());
    });
    connect(funcbar_, &FuncBar::commandRequested, this, &MainWindow::execute);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (!now)
            return;
        if (left_->isAncestorOf(now))
            setActivePane(left_);
        else if (right_->isAncestorOf(now))
            setActivePane(right_);
    });

    sessionTimer_->setSingleShot(true);
    sessionTimer_->setInterval(2000);
    connect(sessionTimer_, &QTimer::timeout, this, &MainWindow::saveSession);

    auto watchTab = [this](Tab* tab) {
        connect(tab, &Tab::folderChanged, this, [this, tab] { tree_->refresh(tab->location()); });
        connect(tab, &Tab::navigated, this, [this](const core::Location& dir) {
            config_->recordVisit(dir.uri(), QDateTime::currentSecsSinceEpoch());
        });
        connect(tab, &Tab::dropRequested, this, &MainWindow::transfer);
        connect(tab, &Tab::mountNeeded, this, [this, tab](const core::Location& dir) {
            QPointer<Tab> guard(tab);
            places_->mountFor(dir, [guard, dir] {
                if (guard && guard->location() == dir)
                    guard->navigate(dir);
            });
        });
    };
    for (Pane* pane : {left_, right_})
        connect(pane, &Pane::tabOpened, this, watchTab);
    // The session comes back first; folders on the command line then
    // replace what the active tabs show.
    const Config::Session session = config_->session();
    if (session.valid) {
        left_->restore(session.left, session.leftActive);
        right_->restore(session.right, session.rightActive);
    }
    if (left_->tabCount() == 0)
        left_->newTab(left.isValid() ? left : core::Location::home());
    else if (left.isValid())
        left_->activeTab()->navigate(left);
    if (right_->tabCount() == 0)
        right_->newTab(right.isValid() ? right : core::Location::home());
    else if (right.isValid())
        right_->activeTab()->navigate(right);
    Pane* start = session.valid && session.activePane == QLatin1String("right") && !left.isValid() ? right_ : left_;
    active_ = start;
    start->setActive(true);
    start->activeTab()->setFocus();
    cmdline_->setFolder(start->activeTab()->location().displayPath());
    tree_->reveal(start->activeTab()->location());

    installDevAids(this);
}

void MainWindow::setActivePane(Pane* pane)
{
    if (pane == active_)
        return;
    active_ = pane;
    left_->setActive(pane == left_);
    right_->setActive(pane == right_);
    tree_->reveal(pane->activeTab()->location());
    cmdline_->setFolder(pane->activeTab()->location().displayPath());
    if (preview_) {
        placePreview();
        preview_->show(pane->activeTab()->cursorLocation());
    }
}

void MainWindow::togglePane(Pane* pane)
{
    if (pane->isVisible()) {
        Pane* other = pane == left_ ? right_ : left_;
        if (!other->isVisible()) {
            active_->showMessage(tr("One pane stays open"));
            return;
        }
        pane->hide();
        if (pane == active_)
            other->activeTab()->setFocus();
    } else {
        pane->show();
    }
}

void MainWindow::execute(keymap::Command command)
{
    using keymap::Command;
    switch (command) {
    case Command::Quit:
        close();  // through closeEvent, so the session is saved
        return;
    case Command::SwitchPane:
        if (tree_->hasFocus())
            active_->activeTab()->setFocus();
        else if (otherPane()->isVisible())
            otherPane()->activeTab()->setFocus();
        return;
    case Command::FocusTree:
        tree_->setFocus();
        return;
    case Command::HideLeft:
        togglePane(left_);
        return;
    case Command::HideRight:
        togglePane(right_);
        return;
    case Command::Copy:
        startTransfer(false);
        return;
    case Command::Move:
        startTransfer(true);
        return;
    case Command::Mkdir:
        promptMkdir();
        return;
    case Command::DeletePermanent:
        confirmRemove(true);
        return;
    case Command::NewFile:
        promptNewFile();
        return;
    case Command::MarkGlob:
        promptGlob(true);
        return;
    case Command::UnmarkGlob:
        promptGlob(false);
        return;
    case Command::OpenWith:
        openWith();
        return;
    case Command::Preview:
        togglePreview();
        return;
    case Command::FocusCommandLine:
        cmdline_->setFocus();
        return;
    case Command::CopyClip:
        copyToClipboard(false);
        return;
    case Command::CutClip:
        copyToClipboard(true);
        return;
    case Command::Paste:
        paste();
        return;
    case Command::EditPath:
        promptPath();
        return;
    case Command::GoTo:
        goTo();
        return;
    case Command::Find: {
        Tab* tab = active_->activeTab();
        const core::Location root = tab->location();
        PromptBar* bar = strip_->prompt(tr("Find in %1").arg(root.displayName()), tr("Find"), QString());
        connect(bar, &PromptBar::accepted, this, [this, root](const QString& text) {
            if (!text.trimmed().isEmpty())
                active_->newSearchTab(root, text.trimmed());
        });
        return;
    }
    case Command::Bookmark:
        places_->toggleBookmark(active_->activeTab()->location());
        return;
    case Command::Sync:
        active_->openSync(otherPane()->activeTab()->location());
        return;
    case Command::Trash:
        if (tree_->hasFocus() && tree_->currentPlace()) {
            places_->unmountCurrent();
            return;
        }
        confirmRemove(false);
        return;
    case Command::Cancel:
        if (active_->execute(command))
            return;
        if (preview_) {
            togglePreview();
            return;
        }
        return;
    default:
        break;
    }
    if (!active_->execute(command))
        active_->showMessage(tr("Not yet: %1").arg(keymap::label(command)));
}

void MainWindow::startTransfer(bool move)
{
    Tab* tab = active_->activeTab();
    const auto sources = tab->selectedLocations();
    if (sources.empty())
        return;
    const core::Location dest = otherPane()->activeTab()->location();
    const QString verb = move ? tr("Move") : tr("Copy");
    PromptBar* bar = strip_->prompt(tr("%1 %2 to").arg(verb, tab->selectionText()), verb, dest.displayPath());
    connect(bar, &PromptBar::accepted, this, [this, tab, sources, move](const QString& text) {
        const core::Location target = core::Location::fromCommandLine(QFile::encodeName(text.trimmed()));
        if (!target.isValid())
            return;
        runJob(new core::TransferJob(sources, target, move), tab, target);
    });
}

void MainWindow::promptMkdir()
{
    Tab* tab = active_->activeTab();
    PromptBar* bar = strip_->prompt(tr("New folder"), tr("Create"), QString());
    connect(bar, &PromptBar::accepted, this, [this, tab](const QString& text) {
        const QByteArray name = QFile::encodeName(text.trimmed());
        if (name.isEmpty())
            return;
        const core::Location dir = tab->location();
        QPointer<Tab> guard(tab);
        core::makeDirectory(dir.child(name), [this, guard, dir, name](const QString& error) {
            if (!error.isEmpty()) {
                active_->showMessage(error);
                return;
            }
            if (guard && guard->location() == dir)
                guard->reloadWithCursor(name);
            tree_->refresh(dir);
        });
    });
}

void MainWindow::promptNewFile()
{
    Tab* tab = active_->activeTab();
    PromptBar* bar = strip_->prompt(tr("New file"), tr("Create"), QString());
    connect(bar, &PromptBar::accepted, this, [this, tab](const QString& text) {
        const QByteArray name = QFile::encodeName(text.trimmed());
        if (name.isEmpty())
            return;
        const core::Location dir = tab->location();
        QPointer<Tab> guard(tab);
        core::createFile(dir.child(name), [this, guard, dir, name](const QString& error) {
            if (!error.isEmpty()) {
                active_->showMessage(error);
                return;
            }
            if (guard && guard->location() == dir)
                guard->reloadWithCursor(name);
        });
    });
}

void MainWindow::promptGlob(bool on)
{
    Tab* tab = active_->activeTab();
    PromptBar* bar = strip_->prompt(on ? tr("Select") : tr("Deselect"), on ? tr("Select") : tr("Deselect"),
                                    QStringLiteral("*"));
    QPointer<Tab> guard(tab);
    connect(bar, &PromptBar::accepted, this, [guard, on](const QString& text) {
        if (guard && !text.trimmed().isEmpty())
            guard->markGlob(text.trimmed(), on);
    });
}

void MainWindow::openWith()
{
    Tab* tab = active_->activeTab();
    const core::Location file = tab->cursorLocation();
    if (!file.isValid() || tab->cursorIsDir())
        return;
    QPointer<MainWindow> guard(this);
    core::appsFor(file, [guard, file](std::vector<core::AppChoice> apps) {
        if (!guard)
            return;
        if (apps.empty()) {
            guard->active_->showMessage(tr("No application registered for this type"));
            return;
        }
        QStringList names;
        for (const core::AppChoice& a : apps)
            names << a.name;
        ChoiceBar* bar = guard->strip_->choose(tr("Open %1 with").arg(file.displayName()), names);
        connect(bar, &ChoiceBar::chosen, guard, [guard, apps, file](int index) {
            QString error;
            if (index >= 0 && index < static_cast<int>(apps.size())
                && !core::launchWith(apps[static_cast<size_t>(index)].id, {file}, &error))
                guard->active_->showMessage(error);
        });
    });
}

void MainWindow::togglePreview()
{
    if (preview_) {
        if (config_->previewThirdSlot()) {
            previewSlot_->hide();
        } else {
            otherPane()->setOverlay(nullptr);
        }
        preview_->deleteLater();
        preview_ = nullptr;
        return;
    }
    preview_ = new Preview(services_);
    placePreview();
    preview_->show(active_->activeTab()->cursorLocation());
}

void MainWindow::placePreview()
{
    if (config_->previewThirdSlot()) {
        // Its own slot right of both panes (Opus style).
        previewSlot_->layout()->addWidget(preview_);
        preview_->QWidget::show();
        previewSlot_->show();
        return;
    }
    // Otherwise the preview borrows the pane opposite the active one; a
    // hidden pane comes back for it.
    Pane* host = otherPane();
    Pane* other = host == left_ ? right_ : left_;
    if (other->hasOverlay())
        other->setOverlay(nullptr);
    host->show();
    host->setOverlay(preview_);
}

void MainWindow::goTo()
{
    GoToBar* bar = strip_->goTo();
    const QList<Config::Visit> history = config_->history();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // Shown as paths; the uri behind each is kept for the choice.
    auto shown = std::make_shared<QHash<QString, QString>>();
    auto refill = [bar, history, now, shown](const QString& text) {
        QStringList entries;
        shown->clear();
        for (const Config::Visit& v : Config::rank(history, text, now)) {
            const QString path = core::Location::fromUri(v.uri).displayPath();
            shown->insert(path, v.uri);
            entries << path;
            if (entries.size() >= 50)
                break;
        }
        bar->setEntries(entries);
    };
    refill(QString());
    connect(bar, &GoToBar::textChanged, this, refill);
    connect(bar, &GoToBar::chosen, this, [this, shown](const QString& choice) {
        const QString uri = shown->value(choice);
        const core::Location dir = uri.isEmpty() ? core::Location::fromCommandLine(QFile::encodeName(choice))
                                                 : core::Location::fromUri(uri);
        if (dir.isValid())
            active_->activeTab()->navigate(dir);
    });
}

void MainWindow::saveSession()
{
    Config::Session s;
    s.left = left_->tabStates();
    s.right = right_->tabStates();
    s.leftActive = left_->activeIndex();
    s.rightActive = right_->activeIndex();
    s.activePane = active_ == left_ ? QStringLiteral("left") : QStringLiteral("right");
    config_->saveSession(s);
    config_->sync();
}

void MainWindow::saveLayout()
{
    config_->setWindowSize(size());
    config_->setSplitterSizes(QStringLiteral("tree"), split_->sizes());
    QList<int> panes = panes_->sizes();
    if (panes.size() > 2)
        panes.resize(2);  // the preview slot is transient
    config_->setSplitterSizes(QStringLiteral("panes"), panes);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveLayout();
    saveSession();
    QMainWindow::closeEvent(event);
}

void MainWindow::runCommand(const QString& command)
{
    QString error;
    if (!core::runInTerminal(active_->activeTab()->location(), command, true, &error))
        active_->showMessage(error);
}

void MainWindow::copyToClipboard(bool cut)
{
    const auto files = active_->activeTab()->selectedLocations();
    if (files.empty())
        return;
    QApplication::clipboard()->setMimeData(uris::make(files, cut));
    active_->showMessage(cut ? tr("%n items cut", nullptr, static_cast<int>(files.size()))
                             : tr("%n items copied", nullptr, static_cast<int>(files.size())));
}

void MainWindow::paste()
{
    std::vector<core::Location> files;
    bool cut = false;
    if (!uris::read(QApplication::clipboard()->mimeData(), &files, &cut))
        return;
    transfer(files, active_->activeTab()->location(), cut);
    if (cut)
        QApplication::clipboard()->clear();
}

void MainWindow::transfer(const std::vector<core::Location>& sources, const core::Location& target, bool move)
{
    if (sources.empty() || !target.isValid())
        return;
    // The job's "source tab" clears its marks when done; the tab showing
    // the sources' folder is the best guess.
    Tab* from = active_->activeTab();
    const core::Location parent = sources.front().parent();
    for (Pane* pane : {left_, right_})
        for (Tab* tab : pane->tabs())
            if (tab->location() == parent)
                from = tab;
    runJob(new core::TransferJob(sources, target, move), from, target);
}

void MainWindow::promptPath()
{
    Tab* tab = active_->activeTab();
    PromptBar* bar = strip_->prompt(tr("Go to"), tr("Go"), tab->location().displayPath());
    connect(bar, &PromptBar::accepted, this, [tab](const QString& text) {
        QPointer<Tab> guard(tab);
        const core::Location dir = core::Location::fromCommandLine(QFile::encodeName(text.trimmed()));
        if (guard && dir.isValid())
            guard->navigate(dir);
    });
}

void MainWindow::showFolder(const core::Location& dir)
{
    if (dir.isValid())
        active_->activeTab()->navigate(dir);
    raiseWindow();
}

void MainWindow::showItem(const core::Location& item)
{
    const core::Location parent = item.parent();
    if (parent.isValid()) {
        Tab* tab = active_->activeTab();
        tab->navigate(parent);
        tab->setCursorOnLoad(item.basename());
    }
    raiseWindow();
}

void MainWindow::raiseWindow()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::confirmRemove(bool permanent)
{
    Tab* tab = active_->activeTab();
    const auto sources = tab->selectedLocations();
    if (sources.empty())
        return;
    const QString what = tab->selectionText();
    PromptBar* bar = strip_->prompt(permanent ? tr("Delete %1 permanently?").arg(what)
                                              : tr("Move %1 to the trash?").arg(what),
                                    permanent ? tr("Delete") : tr("Trash"), std::nullopt);
    connect(bar, &PromptBar::accepted, this, [this, tab, sources, permanent] {
        runJob(new core::RemoveJob(sources, permanent), tab, tab->location());
    });
}

void MainWindow::runJob(core::Job* job, Tab* sourceTab, const core::Location& destination)
{
    QPointer<Tab> source(sourceTab);
    const core::Location sourceDir = sourceTab->location();
    connect(job, &core::Job::finished, this, [this, source, sourceDir, destination] {
        if (source && source->location() == sourceDir)
            source->clearMarks();
        refreshAt(sourceDir);
        refreshAt(destination);
        tree_->refresh(sourceDir);
        tree_->refresh(destination);
    });
    strip_->addJob(job);
}

void MainWindow::refreshAt(const core::Location& dir)
{
    // Monitored tabs catch up on their own; the rest (remote backends
    // without monitors) reload explicitly.
    for (Pane* pane : {left_, right_})
        for (Tab* tab : pane->tabs())
            if (tab->location() == dir && !tab->monitoring())
                tab->reload();
}

void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    if (!painted_) {
        painted_ = true;
        bench::mark("first-frame");
    }
}
