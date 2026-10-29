#include "mainwindow.h"

#include "bench.h"
#include "pane/pane.h"
#include "pane/tab.h"
#include "tree.h"

#include <QAction>
#include <QApplication>
#include <QSplitter>
#include <QTimer>

MainWindow::MainWindow(const core::Location& start, QWidget* parent)
    : QMainWindow(parent)
    , tree_(new Tree(this))
    , pane_(new Pane(this))
{
    setWindowTitle(QStringLiteral("superfar"));
    resize(1100, 700);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->addWidget(tree_);
    split->addWidget(pane_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 860});
    setCentralWidget(split);

    connect(pane_, &Pane::locationChanged, tree_, &Tree::reveal);
    connect(tree_, &Tree::folderChosen, this,
            [this](const core::Location& dir) { pane_->activeTab()->navigate(dir); });

    auto* hidden = new QAction(tr("Show hidden files"), this);
    hidden->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    hidden->setCheckable(true);
    connect(hidden, &QAction::toggled, this,
            [this](bool on) { pane_->activeTab()->setShowHidden(on); });
    addAction(hidden);

    auto* quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence(Qt::Key_F10));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);
    addAction(quit);

    pane_->activeTab()->navigate(start);
    pane_->setFocus();

    // Bench aid: SUPERFAR_BENCH_THEN=<folder> navigates there once the
    // first folder has loaded, so memory after leaving a big folder can
    // be measured.
    if (const QByteArray then = qgetenv("SUPERFAR_BENCH_THEN"); !then.isEmpty()) {
        connect(pane_->activeTab(), &Tab::loaded, this, [this, then] {
            disconnect(pane_->activeTab(), &Tab::loaded, this, nullptr);
            QTimer::singleShot(1000, this, [this, then] {
                pane_->activeTab()->navigate(core::Location::fromCommandLine(then));
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

void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    if (!painted_) {
        painted_ = true;
        bench::mark("first-frame");
    }
}
