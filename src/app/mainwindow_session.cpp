// MainWindow, what it keeps and how it closes: the session and the
// layout saved, the question before quitting with jobs still there,
// and the window raised for another program's request. See
// mainwindow.cpp for the rest of the class.
#include "mainwindow.h"

#include "config.h"
#include "counted.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "pane/pane.h"
#include "tree.h"

#include <QCloseEvent>
#include <QSplitter>

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
