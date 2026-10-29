#include "mainwindow.h"

#include "bench.h"
#include "funcbar.h"
#include "pane/pane.h"
#include "pane/tab.h"
#include "tree.h"

#include <QApplication>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(const core::Location& left, const core::Location& right, QWidget* parent)
    : QMainWindow(parent)
    , tree_(new Tree(this))
    , panes_(new QSplitter(Qt::Horizontal, this))
    , left_(new Pane(this))
    , right_(new Pane(this))
    , active_(left_)
    , funcbar_(new FuncBar(this))
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

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(split, 1);
    layout->addWidget(funcbar_);
    setCentralWidget(central);

    for (Pane* pane : {left_, right_}) {
        connect(pane, &Pane::locationChanged, this, [this, pane](const core::Location& dir) {
            if (pane == active_)
                tree_->reveal(dir);
        });
        connect(pane, &Pane::commandRequested, this, &MainWindow::execute);
    }
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

    left_->newTab(left);
    right_->newTab(right);
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

    // Dev aid: SUPERFAR_GRAB=<file.png> renders the window to a file and
    // quits, so the look can be checked without a screenshot tool.
    if (const QByteArray grabPath = qgetenv("SUPERFAR_GRAB"); !grabPath.isEmpty()) {
        QTimer::singleShot(1500, this, [this, grabPath] {
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
    default:
        break;
    }
    if (!active_->execute(command))
        active_->showMessage(tr("Not yet: %1").arg(keymap::label(command)));
}

void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    if (!painted_) {
        painted_ = true;
        bench::mark("first-frame");
    }
}
