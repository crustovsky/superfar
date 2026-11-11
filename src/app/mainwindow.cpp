#include "mainwindow.h"

#include "counted.h"
#include "bench.h"
#include "cmdline.h"
#include "config.h"
#include "devaids.h"
#include "core/apps.h"
#include "core/dirsize.h"
#include "core/fileops.h"
#include "core/jobs/attributes.h"
#include "core/jobs/extract.h"
#include "core/jobs/pack.h"
#include "core/thumbnails.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "funcbar.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "menu.h"
#include "openwith.h"
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
#include <QMenu>
#include <QMenuBar>
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
    services_.jobs = strip_->queue();
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
    treeSizes_ = treeSizes.size() == 2 ? treeSizes : QList<int>{220, 880};
    split_->setSizes(treeSizes_);

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
        connect(pane, &Pane::columnsChanged, this, [this, side](const QStringList& order, const QStringList& hidden) {
            config_->setColumnLayout(side, order, hidden);
        });
        connect(pane, &Pane::columnsResetRequested, this, [this, pane, side] {
            config_->resetColumns(side);  // back to the config's seed, or the defaults
            pane->applyColumns(config_->columns(side));
        });
        connect(pane, &Pane::sessionChanged, this, [this] { sessionTimer_->start(); });
        connect(pane, &Pane::runJobs, this, [this, pane](const std::vector<core::Job*>& jobs) {
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
        connect(pane, &Pane::packRequested, this,
                [this, pane](const core::Location& parent, const std::vector<QByteArray>& names,
                             const core::Location& archive, const QByteArray& options, bool queued) {
                    auto* job = new core::PackJob(parent, names, archive, options);
                    job->setSubject(names.size() == 1 ? parent.child(names.front()).displayName()
                                                      : counted(static_cast<qint64>(names.size()), tr("item"), tr("items")));
                    job->setPlan(tr("%1 from %2 into %3").arg(job->subject(), parent.displayPath(), archive.displayPath()));
                    const core::Location dest = archive.parent();
                    const QByteArray name = archive.basename();
                    connect(job, &core::Job::finished, this, [this, dest, name] {
                        for (Pane* p : {left_, right_})
                            for (Tab* t : p->tabs())
                                if (t->location() == dest)
                                    t->reloadWithCursor(name);  // the new archive under the cursor
                    });
                    runJob(job, pane->activeTab(), parent, dest, queued);
                });
        connect(pane, &Pane::attributesRequested, this,
                [this, pane](const std::vector<core::Location>& items, const core::AttributeChange& change, bool queued) {
                    // A job, so a tree gets progress, Abort and a log of
                    // what was refused. The marks stay: the owner may
                    // follow the mode.
                    auto* job = new core::AttributesJob(items, change);
                    job->setSubject(items.size() == 1 ? items.front().displayName()
                                                      : counted(static_cast<qint64>(items.size()), tr("item"), tr("items")));
                    const core::Location dir = pane->activeTab()->location();
                    job->setPlan(tr("%1 in %2").arg(job->subject(), dir.displayPath()));
                    connect(job, &core::Job::finished, this, [this, pane, dir] {
                        refreshAt(dir);
                        pane->refreshProperties();
                    });
                    strip_->addJob(job, queued);
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
        connect(pane, &Pane::undoable, this, [this](const QString& what, const std::vector<core::UndoItem>& items) {
            undo_.record({what, items});
        });
    }
    connect(strip_, &JobStrip::jobFinished, this, &MainWindow::onJobFinished);
    connect(tree_, &Tree::folderChosen, this,
            [this](const core::Location& dir) { active_->activeTab()->navigate(dir); });
    // The tree's keys are the window's commands (Tab back to the pane,
    // F8 on a mount), and a drop on a folder there is a transfer.
    connect(tree_, &Tree::commandRequested, this, &MainWindow::execute);
    connect(tree_, &Tree::dropRequested, this, &MainWindow::transfer);
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
    menu::build(menuBar(), [this](keymap::Command c) { execute(c); });
    menuBar()->hide();
    for (QMenu* m : menuBar()->findChildren<QMenu*>())
        connect(m, &QMenu::aboutToHide, this, [this] {
            // The bar leaves with the last open menu, after a choice or Esc.
            QTimer::singleShot(0, this, [this] {
                for (QMenu* open : menuBar()->findChildren<QMenu*>())
                    if (open->isVisible())
                        return;
                menuBar()->hide();
                active_->activeTab()->setFocus();
            });
        });
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
        connect(tab, &Tab::undoable, this, [this](const QString& what, const core::UndoItem& item) {
            undo_.record({what, {item}});
        });
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
        if (!session.tree)
            tree_->hide();
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

    // The first start of a new version says what it is.
    if (config_->lastVersion() != QLatin1String(SUPERFAR_VERSION)) {
        config_->setLastVersion(QStringLiteral(SUPERFAR_VERSION));
        active_->execute(keymap::Command::About);
    }

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

void MainWindow::toggleTree()
{
    if (tree_->isHidden()) {
        showTree();
        return;
    }
    treeSizes_ = split_->sizes();
    if (tree_->hasFocus())
        active_->activeTab()->setFocus();
    tree_->hide();  // the splitter gives its width to the panes
    sessionTimer_->start();
}

void MainWindow::showTree()
{
    if (!tree_->isHidden())
        return;
    tree_->show();  // its showEvent reveals the active tab's folder
    split_->setSizes(treeSizes_);
    sessionTimer_->start();
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
        showTree();
        tree_->setFocus();
        return;
    case Command::ToggleTree:
        toggleTree();
        return;
    case Command::Menu:
        if (menuBar()->isVisible()) {
            menuBar()->hide();
            active_->activeTab()->setFocus();
        } else {
            menuBar()->show();
            if (QMenu* first = menuBar()->findChild<QMenu*>()) {
                menuBar()->setActiveAction(first->menuAction());
                first->popup(menuBar()->mapToGlobal(menuBar()->actionGeometry(first->menuAction()).bottomLeft()));
            }
        }
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
    case Command::Undo:
        undo(false);
        return;
    case Command::Redo:
        undo(true);
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
    case Command::Columns:
        chooseColumns();
        return;
    case Command::Preview:
        togglePreview();
        return;
    case Command::PreviewFirst:
    case Command::PreviewPrev:
    case Command::PreviewNext:
    case Command::PreviewLast:
        // Only a paged or playing preview takes these keys; otherwise the
        // tab gives them their list meaning (first row, page up, ...).
        if (preview_
            && preview_->step(command == Command::PreviewFirst ? Step::First
                              : command == Command::PreviewLast ? Step::Last
                              : command == Command::PreviewPrev ? Step::Prev
                                                                : Step::Next))
            return;
        break;
    case Command::PreviewPlay:
        if (preview_)
            preview_->togglePlay();
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
    case Command::Pack:
        active_->openPack(otherPane()->activeTab()->location());
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
        if (Tab* tab = active_->activeTab(); tab->markedCount() > 0)
            tab->clearMarks();  // last of all: nothing else was open
        return;
    default:
        break;
    }
    if (!active_->execute(command))
        active_->showMessage(tr("Not yet: %1").arg(keymap::label(command)));
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

void MainWindow::saveSession()
{
    Config::Session s;
    s.left = left_->tabStates();
    s.right = right_->tabStates();
    s.leftActive = left_->activeIndex();
    s.rightActive = right_->activeIndex();
    s.activePane = active_ == left_ ? QStringLiteral("left") : QStringLiteral("right");
    s.tree = !tree_->isHidden();
    config_->saveSession(s);
    config_->sync();
}

void MainWindow::saveLayout()
{
    config_->setWindowSize(size());
    if (!tree_->isHidden())  // hidden, the saved width is the one it comes back with
        config_->setSplitterSizes(QStringLiteral("tree"), split_->sizes());
    QList<int> panes = panes_->sizes();
    if (panes.size() > 2)
        panes.resize(2);  // the preview slot is transient
    config_->setSplitterSizes(QStringLiteral("panes"), panes);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Closing ends every job, the queued ones unrun: ask first.
    if (const int jobs = strip_->queue()->count(); jobs > 0 && !quitConfirmed_) {
        event->ignore();
        if (strip_->findChild<PromptBar*>(QStringLiteral("quit")))
            return;  // asked already
        PromptBar* bar = strip_->prompt(tr("%1 running or queued. Abort and quit?")
                                            .arg(counted(jobs, tr("job is"), tr("jobs are"))),
                                        tr("&Quit"), std::nullopt);
        bar->setObjectName(QStringLiteral("quit"));
        connect(bar, &PromptBar::accepted, this, [this] {
            quitConfirmed_ = true;
            close();
        });
        return;
    }
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

void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    if (!painted_) {
        painted_ = true;
        bench::mark("first-frame");
    }
}
