#include "mainwindow.h"

#include "bench.h"
#include "cmdline.h"
#include "core/apps.h"
#include "core/bookmarks.h"
#include "core/dirsize.h"
#include "core/fileops.h"
#include "core/mounts.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "funcbar.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "pane/pane.h"
#include "pane/tab.h"
#include "preview.h"
#include "tree.h"
#include "uris.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QFile>
#include <QKeyEvent>
#include <QPointer>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(const core::Location& left, const core::Location& right, QWidget* parent)
    : QMainWindow(parent)
    , sizes_(new core::DirSizePool(this))
    , mounts_(new core::Mounts(this))
    , bookmarks_(new core::Bookmarks(QByteArray(), this))
    , tree_(new Tree(this))
    , panes_(new QSplitter(Qt::Horizontal, this))
    , left_(new Pane(sizes_, this))
    , right_(new Pane(sizes_, this))
    , active_(left_)
    , funcbar_(new FuncBar(this))
    , strip_(new JobStrip(this))
    , cmdline_(new CommandLine(this))
{
    setWindowTitle(QStringLiteral("superfar"));
    resize(1100, 700);

    panes_->setChildrenCollapsible(false);
    panes_->addWidget(left_);
    panes_->addWidget(right_);
    panes_->setSizes({500, 500});

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->addWidget(tree_);
    split->addWidget(panes_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({220, 880});

    // The bar area sits under the panes behind a draggable handle; extra
    // height goes to a job's graph or a log list.
    auto* vsplit = new QSplitter(Qt::Vertical, this);
    vsplit->addWidget(split);
    vsplit->addWidget(strip_);
    vsplit->setStretchFactor(0, 1);
    vsplit->setStretchFactor(1, 0);
    vsplit->setCollapsible(0, false);
    vsplit->setCollapsible(1, false);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(vsplit, 1);
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
    connect(tree_, &Tree::placeChosen, this, [this](const QString& id) { mountPlace(id, core::Location()); });
    connect(tree_, &Tree::dropRequested, this, &MainWindow::transfer);
    connect(tree_, &Tree::commandRequested, this, &MainWindow::execute);
    connect(mounts_, &core::Mounts::changed, this, &MainWindow::rebuildRoots);
    connect(bookmarks_, &core::Bookmarks::changed, this, &MainWindow::rebuildRoots);
    connect(mounts_, &core::Mounts::askPassword, this,
            [this](quint64 token, const QString& message, const QString& user, const QString& domain,
                   bool needUser, bool needDomain, bool needPassword, bool anonymousOk) {
                AuthBar* bar = strip_->auth(message, user, domain, needUser, needDomain, needPassword, anonymousOk);
                connect(bar, &AuthBar::answered, this,
                        [this, token](const QString& u, const QString& d, const QString& p, bool anon) {
                            mounts_->answerPassword(token, u, d, p, anon);
                        });
                connect(bar, &AuthBar::rejected, this, [this, token] { mounts_->abortPrompt(token); });
            });
    connect(mounts_, &core::Mounts::askQuestion, this,
            [this](quint64 token, const QString& message, const QStringList& choices) {
                ChoiceBar* bar = strip_->choose(message, choices);
                connect(bar, &ChoiceBar::chosen, this, [this, token](int i) { mounts_->answerQuestion(token, i); });
                connect(bar, &ChoiceBar::rejected, this, [this, token] { mounts_->abortPrompt(token); });
            });
    rebuildRoots();
    connect(funcbar_, &FuncBar::commandRequested, this, &MainWindow::execute);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (!now)
            return;
        if (left_->isAncestorOf(now))
            setActivePane(left_);
        else if (right_->isAncestorOf(now))
            setActivePane(right_);
    });

    auto watchTab = [this](Tab* tab) {
        connect(tab, &Tab::folderChanged, this, [this, tab] { tree_->refresh(tab->location()); });
        connect(tab, &Tab::dropRequested, this, &MainWindow::transfer);
        connect(tab, &Tab::mountNeeded, this, [this, tab](const core::Location& dir) { mountFor(tab, dir); });
    };
    watchTab(left_->newTab(left));
    watchTab(right_->newTab(right));
    for (Pane* pane : {left_, right_})
        connect(pane, &Pane::tabOpened, this, watchTab);
    left_->setActive(true);
    left_->activeTab()->setFocus();
    cmdline_->setFolder(left.displayPath());

    // Bench aid: SUPERFAR_BENCH_THEN=<folder> navigates there once the
    // first folder has loaded, so memory after leaving a big folder can
    // be measured.
    if (const QByteArray then = qgetenv("SUPERFAR_BENCH_THEN"); !then.isEmpty()) {
        Tab* tab = left_->activeTab();
        connect(tab, &Tab::loaded, this, [this, tab, then] {
            disconnect(tab, &Tab::loaded, this, nullptr);
            QTimer::singleShot(1000, this, [tab, then] {
                tab->navigate(core::Location::fromCommandLine(then));
            });
        });
    }

    // Dev aid: SUPERFAR_DEV_KEYS="Ctrl+A;F5;Return" posts those keys to the
    // focused widget, one every 400 ms, once the first folder has loaded.
    if (const QByteArray keys = qgetenv("SUPERFAR_DEV_KEYS"); !keys.isEmpty()) {
        Tab* tab = left_->activeTab();
        connect(tab, &Tab::loaded, this, [this, keys] {
            static bool fired = false;
            if (fired)
                return;
            fired = true;
            const QStringList parts = QString::fromLatin1(keys).split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (int i = 0; i < parts.size(); ++i) {
                QTimer::singleShot(400 * (i + 1), this, [this, part = parts[i]] {
                    // The window may not be active under a compositor
                    // that keeps focus elsewhere; its own focus child is
                    // still what a key would reach.
                    QWidget* target = QApplication::focusWidget();
                    if (!target)
                        target = focusWidget();
                    if (!target)
                        return;
                    const QKeySequence seq(part);
                    const QKeyCombination combo = seq[0];
                    const QString text = combo.keyboardModifiers() == Qt::NoModifier && combo.key() < 0x100
                        ? QString(QChar(static_cast<int>(combo.key()))) : QString();
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, combo.key(), combo.keyboardModifiers(), text));
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers(), text));
                });
            }
        });
    }

    // Dev aid: SUPERFAR_GRAB=<file.png> renders the window to a file and
    // quits, so the look can be checked without a screenshot tool.
    if (const QByteArray grabPath = qgetenv("SUPERFAR_GRAB"); !grabPath.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("SUPERFAR_GRAB_MS") > 0
            ? qEnvironmentVariableIntValue("SUPERFAR_GRAB_MS") : 1500;
        QTimer::singleShot(delay, this, [this, grabPath] {
            grab().save(QString::fromLocal8Bit(grabPath));
            qApp->quit();
        });
    }
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
        qApp->quit();
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
    case Command::Trash:
        if (tree_->hasFocus() && tree_->currentPlace()) {
            unmountCurrentPlace();
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
        Pane* host = otherPane();
        host->setOverlay(nullptr);
        preview_->deleteLater();
        preview_ = nullptr;
        return;
    }
    preview_ = new Preview(sizes_);
    placePreview();
    preview_->show(active_->activeTab()->cursorLocation());
}

void MainWindow::placePreview()
{
    // The preview borrows the pane opposite the active one; a hidden pane
    // comes back for it.
    Pane* host = otherPane();
    Pane* other = host == left_ ? right_ : left_;
    if (other->hasOverlay())
        other->setOverlay(nullptr);
    host->show();
    host->setOverlay(preview_);
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

void MainWindow::rebuildRoots()
{
    // Bookmarks, mounts and the trash first; the filesystem last, since
    // it is usually expanded deep and would push the rest out of view.
    std::vector<TreeModel::Root> roots;
    for (const core::Bookmark& b : bookmarks_->list())
        roots.push_back({QStringLiteral("bookmark:") + b.location.uri(), b.name,
                         {QStringLiteral("folder-symbolic"), QStringLiteral("folder")}, b.location, QString(), false});
    for (const core::Place& p : mounts_->places())
        roots.push_back({p.id, p.name, p.iconNames, p.location, p.id, p.canUnmount});
    roots.push_back({QStringLiteral("trash"), tr("Trash"), {QStringLiteral("user-trash-symbolic"), QStringLiteral("user-trash")},
                     core::Location::fromUri(QStringLiteral("trash:///")), QString(), false});
    roots.push_back({QStringLiteral("fs"), tr("Filesystem"), {QStringLiteral("drive-harddisk-symbolic"), QStringLiteral("drive-harddisk")},
                     core::Location::root(), QString(), false});
    tree_->setRoots(roots);
}

void MainWindow::mountPlace(const QString& id, const core::Location& thenOpen)
{
    QPointer<MainWindow> guard(this);
    mounts_->mountPlace(id, [guard, id, thenOpen](const QString& error) {
        if (!guard)
            return;
        if (!error.isEmpty()) {
            guard->active_->showMessage(error);
            return;
        }
        guard->rebuildRoots();
        for (const core::Place& p : guard->mounts_->places())
            if (p.id == id || (thenOpen.isValid() && p.location == thenOpen)) {
                guard->active_->activeTab()->navigate(p.location);
                return;
            }
    });
}

void MainWindow::unmountCurrentPlace()
{
    const TreeModel::Root* place = tree_->currentPlace();
    if (!place || !place->canUnmount)
        return;
    const QString id = place->placeId;
    const core::Location root = place->location;
    PromptBar* bar = strip_->prompt(tr("Unmount %1?").arg(place->name), tr("Unmount"), std::nullopt);
    connect(bar, &PromptBar::accepted, this, [this, id, root] {
        // Tabs inside the mount leave it first, or the unmount is refused.
        for (Pane* pane : {left_, right_})
            for (Tab* tab : pane->tabs())
                if (!tab->location().relativePathFrom(root).isNull())
                    tab->navigate(core::Location::home());
        QPointer<MainWindow> guard(this);
        mounts_->unmountPlace(id, [guard](const QString& error) {
            if (guard && !error.isEmpty())
                guard->active_->showMessage(error);
        });
    });
}

void MainWindow::mountFor(Tab* tab, const core::Location& dir)
{
    QPointer<Tab> guard(tab);
    QPointer<MainWindow> self(this);
    mounts_->mountLocation(dir, [self, guard, dir](const QString& error) {
        if (!self)
            return;
        if (!error.isEmpty()) {
            self->active_->showMessage(error);
            return;
        }
        if (guard && guard->location() == dir)
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
