#include "mainwindow.h"

#include "bench.h"
#include "core/dirsize.h"
#include "core/fileops.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "funcbar.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "pane/pane.h"
#include "pane/tab.h"
#include "tree.h"

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QPointer>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(const core::Location& left, const core::Location& right, QWidget* parent)
    : QMainWindow(parent)
    , sizes_(new core::DirSizePool(this))
    , tree_(new Tree(this))
    , panes_(new QSplitter(Qt::Horizontal, this))
    , left_(new Pane(sizes_, this))
    , right_(new Pane(sizes_, this))
    , active_(left_)
    , funcbar_(new FuncBar(this))
    , strip_(new JobStrip(this))
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
    layout->addWidget(funcbar_);
    setCentralWidget(central);

    for (Pane* pane : {left_, right_}) {
        connect(pane, &Pane::locationChanged, this, [this, pane](const core::Location& dir) {
            if (pane == active_)
                tree_->reveal(dir);
        });
        connect(pane, &Pane::commandRequested, this, &MainWindow::execute);
    }
    connect(strip_, &JobStrip::jobFinished, this, [](core::Job*) {});
    connect(tree_, &Tree::folderChosen, this,
            [this](const core::Location& dir) { active_->activeTab()->navigate(dir); });
    connect(tree_, &Tree::commandRequested, this, &MainWindow::execute);
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
    };
    watchTab(left_->newTab(left));
    watchTab(right_->newTab(right));
    for (Pane* pane : {left_, right_})
        connect(pane, &Pane::tabOpened, this, watchTab);
    left_->setActive(true);
    left_->activeTab()->setFocus();

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
    case Command::Trash:
        confirmRemove(false);
        return;
    case Command::DeletePermanent:
        confirmRemove(true);
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
