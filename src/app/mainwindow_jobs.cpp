// MainWindow, the jobs: copy, move, extract and remove from the keys,
// the clipboard and drops, and what follows a job. See mainwindow.cpp
// for the rest of the class.
#include "mainwindow.h"

#include "counted.h"
#include "core/admin.h"
#include "core/jobs/extract.h"
#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "core/jobs/undo.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "pane/pane.h"
#include "tree.h"
#include "uris.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <algorithm>

void MainWindow::startTransfer(bool move)
{
    Tab* tab = active_->activeTab();
    if (tab->isArchive()) {
        // Out of an archive: bsdtar extracts the members into the target.
        const std::vector<QByteArray> members = tab->archiveMembers();
        if (members.empty())
            return;
        const core::Location dest = otherPane()->activeTab()->location();
        PromptBar* bar = strip_->prompt(tr("Extract %1 to").arg(tab->selectionText()), tr("Extract"), dest.displayPath(), true);
        const core::Location archive = tab->archiveFile();
        const int depth = tab->archiveDepth();
        const core::Location from = tab->location();
        connect(bar, &PromptBar::accepted, this,
                [this, bar, tab = QPointer<Tab>(tab), from, archive, members, depth](const QString& text) {
            const core::Location target = core::Location::fromCommandLine(QFile::encodeName(text.trimmed()));
            if (!target.isValid())
                return;
            auto* job = new core::ExtractJob(archive, members, target, depth);
            job->setSubject(counted(static_cast<qint64>(members.size()), tr("item"), tr("items")));
            job->setPlan(tr("%1 from %2 to %3").arg(job->subject(), archive.displayPath(), target.displayPath()));
            runJob(job, tab, from, target, bar->queued());
        });
        return;
    }
    const auto sources = tab->selectedLocations();
    if (sources.empty())
        return;
    const core::Location dest = otherPane()->activeTab()->location();
    const QString verb = move ? tr("Move") : tr("Copy");
    const QString what = tab->selectionText();
    // Into an administrator tab: say whose the new files will be.
    PromptBar* bar = strip_->prompt(core::admin::isAdmin(dest) ? tr("%1 %2, to belong to root, to").arg(verb, what)
                                                               : tr("%1 %2 to").arg(verb, what),
                                    verb, dest.displayPath(), true);
    // The folder the items were taken from, as it was when the prompt
    // opened: the tab may have moved on, or closed, by the time it is
    // accepted.
    const core::Location from = tab->location();
    connect(bar, &PromptBar::accepted, this, [this, bar, tab = QPointer<Tab>(tab), from, sources, move, what](const QString& text) {
        const core::Location target = core::Location::fromCommandLine(QFile::encodeName(text.trimmed()));
        if (!target.isValid())
            return;
        if (move && tab && tab->location() == from)
            tab->aboutToRemove(sources);
        auto* job = new core::TransferJob(sources, target, move);
        job->setSubject(what);
        job->setPlan(tr("%1 from %2 to %3").arg(what, from.displayPath(), target.displayPath()));
        runJob(job, tab, from, target, bar->queued());
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
    auto* job = new core::TransferJob(sources, target, move);
    job->setSubject(sources.size() == 1 ? sources.front().displayName()
                                        : counted(static_cast<qint64>(sources.size()), tr("item"), tr("items")));
    job->setPlan(tr("%1 from %2 to %3").arg(job->subject(), parent.displayPath(), target.displayPath()));
    runJob(job, from, from->location(), target);
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
    PromptBar* bar = strip_->prompt(permanent ? tr("Delete %1 permanently? This cannot be undone.").arg(what)
                                              : tr("Move %1 to the trash?").arg(what),
                                    permanent ? tr("Delete") : tr("Trash"), std::nullopt, true);
    const core::Location from = tab->location();
    connect(bar, &PromptBar::accepted, this, [this, bar, tab = QPointer<Tab>(tab), from, sources, permanent, what] {
        if (tab && tab->location() == from)
            tab->aboutToRemove(sources);
        auto* job = new core::RemoveJob(sources, permanent);
        job->setSubject(what);
        job->setPlan(tr("%1 in %2").arg(what, from.displayPath()));
        runJob(job, tab, from, from, bar->queued());
    });
}

void MainWindow::runJob(core::Job* job, const QPointer<Tab>& source, const core::Location& sourceDir,
                        const core::Location& destination, bool queued)
{
    connect(job, &core::Job::finished, this, [this, source, sourceDir, destination] {
        if (source)
            source->clearMarks(sourceDir);  // live, or kept for the tab's return
        refreshAt(sourceDir);
        refreshAt(destination);
        tree_->refresh(sourceDir);
        tree_->refresh(destination);
    });
    strip_->addJob(job, queued);
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

void MainWindow::onJobFinished(core::Job* job)
{
    using Side = core::UndoStack::Side;
    auto* back = qobject_cast<core::UndoJob*>(job);
    if (!back) {
        // What the job did can be undone; nothing can be redone now.
        const QString what = job->subject().isEmpty() ? job->title().toLower()
                                                      : tr("%1 of %2").arg(job->title().toLower(), job->subject());
        undo_.record({what, std::move(job->undoItems())});
        return;
    }
    // What the undo did is the redo, and the other way round; what an
    // abort left untouched waits where it was.
    const bool redo = back->isRedo();
    undo_.put(redo ? Side::Redo : Side::Undo, back->remaining());
    core::UndoEntry inverse{back->what(), std::move(job->undoItems())};
    // The folders it touched, for the tabs and the tree.
    std::vector<core::Location> folders;
    for (const core::UndoItem& item : inverse.items)
        for (const core::Location& at : {item.at, item.other}) {
            const core::Location parent = at.isValid() ? at.parent() : core::Location();
            if (parent.isValid() && folders.size() < 16 && std::find(folders.begin(), folders.end(), parent) == folders.end())
                folders.push_back(parent);
        }
    for (const core::Location& dir : folders) {
        refreshAt(dir);
        tree_->refresh(dir);
    }
    const bool nothing = inverse.items.empty();
    undo_.put(redo ? Side::Undo : Side::Redo, std::move(inverse));
    active_->showMessage(nothing ? (redo ? tr("Could not redo: %1") : tr("Could not undo: %1")).arg(back->what())
                                 : (redo ? tr("Redid: %1") : tr("Undid: %1")).arg(back->what()));
}

void MainWindow::undo(bool redo)
{
    // One at a time: the next entry may build on what this one leaves.
    if (strip_->findChild<core::UndoJob*>()) {
        active_->showMessage(tr("An undo is still running"));
        return;
    }
    auto entry = undo_.take(redo ? core::UndoStack::Side::Redo : core::UndoStack::Side::Undo);
    if (!entry) {
        active_->showMessage(redo ? tr("Nothing to redo") : tr("Nothing to undo"));
        return;
    }
    strip_->addJob(new core::UndoJob(std::move(*entry), redo));
}
