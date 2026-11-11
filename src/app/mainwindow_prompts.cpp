// MainWindow, the prompts and lists it puts up in the strip: new
// folder, new file, mark by pattern, the column list, open with, go to
// and the path. See mainwindow.cpp for the rest of the class.
#include "mainwindow.h"

#include "core/fileops.h"
#include "jobs/bars.h"
#include "jobs/strip.h"
#include "openwith.h"
#include "pane/pane.h"
#include "tree.h"

#include <QDateTime>
#include <QFile>

void MainWindow::promptMkdir()
{
    Tab* tab = active_->activeTab();
    if (tab->isArchive()) {
        active_->showMessage(tr("Archives are read-only here"));
        return;
    }
    PromptBar* bar = strip_->prompt(tr("New folder"), tr("Create"), QString());
    connect(bar, &PromptBar::accepted, this, [this, guard = QPointer<Tab>(tab)](const QString& text) {
        const QByteArray name = QFile::encodeName(text.trimmed());
        if (name.isEmpty())
            return;
        if (!guard) {
            active_->showMessage(tr("The tab this was for is closed"));
            return;
        }
        const core::Location dir = guard->location();
        // The answer may come after the tab, or the window, has gone.
        core::makeDirectory(dir.child(name), [self = QPointer<MainWindow>(this), guard, dir, name](const QString& error) {
            if (!self)
                return;
            if (!error.isEmpty()) {
                self->active_->showMessage(error);
                return;
            }
            const core::Location made = dir.child(name);
            self->undo_.record({tr("new folder %1").arg(made.displayName()),
                                {core::UndoItem::created(made, made.isNative())}});
            if (guard && guard->location() == dir)
                guard->reloadWithCursor(name);
            self->tree_->refresh(dir);
        });
    });
}

void MainWindow::promptNewFile()
{
    Tab* tab = active_->activeTab();
    if (tab->isArchive()) {
        active_->showMessage(tr("Archives are read-only here"));
        return;
    }
    PromptBar* bar = strip_->prompt(tr("New file"), tr("Create"), QString());
    connect(bar, &PromptBar::accepted, this, [this, guard = QPointer<Tab>(tab)](const QString& text) {
        const QByteArray name = QFile::encodeName(text.trimmed());
        if (name.isEmpty())
            return;
        if (!guard) {
            active_->showMessage(tr("The tab this was for is closed"));
            return;
        }
        const core::Location dir = guard->location();
        core::createFile(dir.child(name), [self = QPointer<MainWindow>(this), guard, dir, name](const QString& error) {
            if (!self)
                return;
            if (!error.isEmpty()) {
                self->active_->showMessage(error);
                return;
            }
            const core::Location made = dir.child(name);
            self->undo_.record({tr("new file %1").arg(made.displayName()),
                                {core::UndoItem::created(made, made.isNative())}});
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

void MainWindow::chooseColumns()
{
    // The header's right-click list and its drag, for the keyboard: the
    // rows are the columns left to right. Name is not among them: it
    // stays, and first.
    Pane* pane = active_;
    CheckBar* bar = strip_->checks((pane == left_ ? tr("Columns of the left pane") : tr("Columns of the right pane"))
                                   + tr(": Space shows or hides, Shift+Up and Shift+Down move, Esc closes"));
    auto fill = [bar, pane] {
        const columns::Layout layout = pane->columnLayout();
        QStringList labels;
        QList<bool> shown;
        for (int c : layout.order.mid(1)) {
            labels << columns::label(c);
            shown << !layout.hidden.value(c);
        }
        bar->setItems(labels, shown, tr("Reset columns"));
    };
    fill();
    connect(bar, &CheckBar::toggled, pane, [pane](int index, bool on) {
        pane->setColumnShown(pane->columnLayout().order.value(index + 1, -1), on);
    });
    connect(bar, &CheckBar::moveRequested, pane, [bar, pane, fill](int index, int delta) {
        const QList<int> before = pane->columnLayout().order;
        pane->moveColumn(before.value(index + 1, -1), delta);
        if (pane->columnLayout().order == before)
            return;  // at an end already
        fill();
        bar->setCurrent(index + delta);  // the cursor goes with its column
    });
    connect(bar, &CheckBar::actionChosen, pane, [pane, fill] {
        pane->resetColumns();
        fill();
    });
}

void MainWindow::openWith()
{
    Tab* tab = active_->activeTab();
    if (tab->isArchive()) {
        active_->showMessage(tr("F5 extracts; open it from the extracted copy"));
        return;
    }
    // The marked items, else the cursor's; folders too. The list is for
    // the type of the first one: with marks the cursor is usually on
    // something else by now.
    const std::vector<core::Location> items = tab->selectedLocations();
    if (items.empty())
        return;
    const QString title = tr("Open %1 with").arg(tab->selectionText());
    QPointer<MainWindow> guard(this);
    core::appsFor(items.front(), [guard, items, title](const QString& type, std::vector<core::AppChoice> apps) {
        if (!guard)
            return;
        OpenWithBar* bar = guard->strip_->openWith(title, std::move(apps));
        connect(bar, &OpenWithBar::appChosen, guard, [guard, items, type](const core::AppChoice& app, bool makeDefault) {
            QString error;
            const bool ok = core::launchWith(app.id, items, &error, {type, makeDefault});
            if (!error.isEmpty())
                guard->active_->showMessage(error);
            else if (ok && makeDefault)
                guard->active_->showMessage(tr("%1 is the default for %2 now").arg(app.name, type));
        });
        connect(bar, &OpenWithBar::commandTyped, guard, [guard, items](const QString& command, bool inTerminal) {
            QString error;
            if (!core::launchCommand(command, items, inTerminal, &error))
                guard->active_->showMessage(error);
        });
    });
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
