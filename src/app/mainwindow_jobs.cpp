// MainWindow, the jobs: copy, move, extract and remove from the keys,
// the clipboard and drops, and what follows a job. See mainwindow.cpp
// for the rest of the class.
#include "mainwindow.h"

#include "counted.h"
#include "core/jobs/extract.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "pane/pane.h"
#include "tree.h"
#include "uris.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>

void MainWindow::startTransfer(bool move)
{
    Tab* tab = active_->activeTab();
    if (tab->isArchive()) {
        // Out of an archive: bsdtar extracts the members into the target.
        const std::vector<QByteArray> members = tab->archiveMembers();
        if (members.empty())
            return;
        const core::Location dest = otherPane()->activeTab()->location();
        PromptBar* bar = strip_->prompt(tr("Extract %1 to").arg(tab->selectionText()), tr("Extract"), dest.displayPath());
        const core::Location archive = tab->archiveFile();
        const int depth = tab->archiveDepth();
        const core::Location from = tab->location();
        connect(bar, &PromptBar::accepted, this,
                [this, tab = QPointer<Tab>(tab), from, archive, members, depth](const QString& text) {
            const core::Location target = core::Location::fromCommandLine(QFile::encodeName(text.trimmed()));
            if (target.isValid())
                runJob(new core::ExtractJob(archive, members, target, depth), tab, from, target);
        });
        return;
    }
    const auto sources = tab->selectedLocations();
    if (sources.empty())
        return;
    const core::Location dest = otherPane()->activeTab()->location();
    const QString verb = move ? tr("Move") : tr("Copy");
    PromptBar* bar = strip_->prompt(tr("%1 %2 to").arg(verb, tab->selectionText()), verb, dest.displayPath());
    // The folder the items were taken from, as it was when the prompt
    // opened: the tab may have moved on, or closed, by the time it is
    // accepted.
    const core::Location from = tab->location();
    connect(bar, &PromptBar::accepted, this, [this, tab = QPointer<Tab>(tab), from, sources, move](const QString& text) {
        const core::Location target = core::Location::fromCommandLine(QFile::encodeName(text.trimmed()));
        if (!target.isValid())
            return;
        if (move && tab && tab->location() == from)
            tab->aboutToRemove(sources);
        runJob(new core::TransferJob(sources, target, move), tab, from, target);
    });
}

void MainWindow::copyToClipboard(bool cut)
{
    if (active_->activeTab()->isArchive()) {
        active_->showMessage(tr("F5 extracts archive members"));
        return;
    }
    const auto files = active_->activeTab()->selectedLocations();
    if (files.empty())
        return;
    QApplication::clipboard()->setMimeData(uris::make(files, cut));
    active_->showMessage(cut ? tr("%1 cut").arg(counted(static_cast<qint64>(files.size()), tr("item"), tr("items")))
                             : tr("%1 copied").arg(counted(static_cast<qint64>(files.size()), tr("item"), tr("items"))));
}

void MainWindow::paste()
{
    if (active_->activeTab()->isArchive()) {
        active_->showMessage(tr("Archives are read-only here"));
        return;
    }
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
    if (move && from->location() == parent)
        from->aboutToRemove(sources);
    runJob(new core::TransferJob(sources, target, move), from, from->location(), target);
}

void MainWindow::confirmRemove(bool permanent)
{
    Tab* tab = active_->activeTab();
    if (tab->isArchive()) {
        active_->showMessage(tr("Archives are read-only here"));
        return;
    }
    const auto sources = tab->selectedLocations();
    if (sources.empty())
        return;
    const QString what = tab->selectionText();
    PromptBar* bar = strip_->prompt(permanent ? tr("Delete %1 permanently?").arg(what)
                                              : tr("Move %1 to the trash?").arg(what),
                                    permanent ? tr("Delete") : tr("Trash"), std::nullopt);
    const core::Location from = tab->location();
    connect(bar, &PromptBar::accepted, this, [this, tab = QPointer<Tab>(tab), from, sources, permanent] {
        if (tab && tab->location() == from)
            tab->aboutToRemove(sources);
        runJob(new core::RemoveJob(sources, permanent), tab, from, from);
    });
}

void MainWindow::runJob(core::Job* job, const QPointer<Tab>& source, const core::Location& sourceDir,
                        const core::Location& destination)
{
    connect(job, &core::Job::finished, this, [this, source, sourceDir, destination] {
        if (source)
            source->clearMarks(sourceDir);  // live, or kept for the tab's return
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
