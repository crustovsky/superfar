#include "pane/tab.h"

#include "bench.h"
#include "core/apps.h"
#include "core/fileops.h"
#include "core/sort.h"
#include "pane/listmodel.h"
#include "pane/listview.h"
#include "services.h"

#include <QLocale>
#include <QPointer>
#include <QScrollBar>
#include <QVBoxLayout>

Tab::Tab(const Services& services, QWidget* parent)
    : QWidget(parent)
    , listing_(new core::Listing(this))
    , finder_(new core::Finder(this))
    , lister_(new core::ArchiveLister(this))
    , space_(new core::SpaceQuery(this))
    , monitor_(new core::Monitor(this))
    , model_(new ListModel(services.sizes, services.thumbs, this))
    , view_(new ListView(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(view_);

    view_->setModel(model_);
    view_->sizeColumns();
    setFocusProxy(view_);

    static bool reporterClaimed = false;
    if (!reporterClaimed) {
        reporterClaimed = true;
        benchReporter_ = true;
        view_->setBenchReporter(true);
    }

    connect(listing_, &core::Listing::batch, this, &Tab::onBatch);
    connect(listing_, &core::Listing::finished, this, &Tab::onFinished);
    connect(listing_, &core::Listing::failed, this, &Tab::onFailed);
    connect(finder_, &core::Finder::matches, this, &Tab::onBatch);
    connect(finder_, &core::Finder::finished, this, &Tab::onSearchFinished);
    connect(lister_, &core::ArchiveLister::listed, this, &Tab::onArchiveListed);
    connect(lister_, &core::ArchiveLister::failed, this, [this](quint64 gen, const QString& m) {
        if (gen == generation_)
            Q_EMIT statusChanged(m, QString());
    });
    connect(space_, &core::SpaceQuery::result, this, &Tab::onSpace);
    connect(monitor_, &core::Monitor::changed, this, &Tab::onMonitor);
    connect(model_, &ListModel::marksChanged, this, &Tab::updateStatus);
    connect(view_, &QAbstractItemView::activated, this, &Tab::activate);
    connect(view_, &ListView::commandRequested, this, &Tab::commandRequested);
    connect(view_, &ListView::textTyped, this, &Tab::typed);
    connect(view_, &ListView::cursorMoved, this, [this] {
        Q_EMIT cursorChanged(cursorLocation());
        updateStatus();  // the status line names the cursor item in full
    });
    connect(model_, &ListModel::renameRequested, this, &Tab::rename);
    connect(model_, &ListModel::dropRequested, this, &Tab::dropRequested);
    connect(view_, &ListView::editingFinished, this, [this] {
        if (dirty_ && current_) {
            dirty_ = false;
            reload();
        }
    });
}

void Tab::navigate(const core::Location& dir)
{
    if (locked_ && location_.isValid() && dir != location_) {
        Q_EMIT openInNewTab(dir);
        return;
    }
    pendingMarks_.clear();
    load(dir);
    if (location_.isValid())
        Q_EMIT navigated(location_);
}

void Tab::reload()
{
    pendingMarks_ = model_->markedNames();
    cursorName_ = currentName();
    scrollValue_ = view_->verticalScrollBar()->value();
    if (!search_.isEmpty())
        search(location_, search_);
    else if (isArchive())
        showArchiveDir(archiveDir_);  // the listing is in hand; stay in this folder
    else
        load(location_);
}

void Tab::search(const core::Location& root, const QString& pattern)
{
    if (!root.isValid() || pattern.isEmpty())
        return;
    ++generation_;
    listing_->cancel();
    monitor_->stop();
    location_ = root;
    search_ = pattern;
    spaceText_.clear();
    model_->setFilter(QString());
    model_->reset(root);
    finder_->start(root, pattern, generation_);
    Q_EMIT locationChanged(location_);
    Q_EMIT statusChanged(tr("Searching for %1").arg(pattern), QString());
}

void Tab::onSearchFinished(quint64 generation, quint64 scanned)
{
    if (generation != generation_)
        return;
    model_->finishLoad();
    if (!pendingMarks_.isEmpty()) {
        model_->applyMarks(pendingMarks_);
        pendingMarks_.clear();
    }
    placeCursor();
    Q_EMIT statusChanged(tr("%n matches for %1", nullptr, model_->rowCount()).arg(search_)
                             + tr(", %n entries scanned", nullptr, static_cast<int>(scanned)),
                         QString());
    Q_EMIT loaded();
}

void Tab::reloadWithCursor(const QByteArray& name)
{
    pendingMarks_ = model_->markedNames();
    cursorName_ = name;
    load(location_);
}

std::vector<core::Location> Tab::selectedLocations() const
{
    std::vector<core::Location> out;
    if (model_->markedCount() > 0) {
        for (const QByteArray& name : model_->markedNamesInOrder())
            out.push_back(location_.child(name));
    } else if (const QByteArray name = currentName(); !name.isEmpty()) {
        out.push_back(location_.child(name));
    }
    return out;
}

QString Tab::selectionText() const
{
    const int marked = model_->markedCount();
    if (marked > 0)
        return tr("%n items", nullptr, marked);
    const QModelIndex idx = view_->currentIndex();
    return idx.isValid() ? model_->entryAt(idx.row()).display : QString();
}

void Tab::clearMarks()
{
    model_->markAll(false);
}

bool Tab::monitoring() const
{
    return monitor_->active();
}

QStringList Tab::entryNames() const
{
    QStringList out;
    for (int i = 0; i < model_->totalCount(); ++i)
        out << model_->entryAtIndex(i).display;
    return out;
}

core::Location Tab::cursorLocation() const
{
    const QByteArray name = currentName();
    return name.isEmpty() ? core::Location() : location_.child(name);
}

bool Tab::cursorIsDir() const
{
    const QModelIndex idx = view_->currentIndex();
    return idx.isValid() && model_->entryAt(idx.row()).isDir();
}

void Tab::markGlob(const QString& pattern, bool on)
{
    model_->markGlob(pattern, on);
}

void Tab::typed(const QString& text)
{
    setFilter(model_->filter() + text);
}

void Tab::setFilter(const QString& text)
{
    cursorName_ = currentName();
    model_->setFilter(text);
    placeCursor();
    updateStatus();
}

void Tab::rename(const QByteArray& name, const QString& newName)
{
    const core::Location dir = location_;
    QPointer<Tab> guard(this);
    core::renameEntry(dir.child(name), newName,
                      [guard, dir, name](const QString& error, const QByteArray& actual, const QString& display) {
        if (!guard)
            return;
        if (!error.isEmpty()) {
            Q_EMIT guard->message(error);
            return;
        }
        if (guard->location_ != dir)
            return;
        // While a neighbouring row is being edited a reload would close
        // its editor; the entry changes in place and the reload waits.
        if (guard->view_->isEditing()) {
            guard->model_->renameInPlace(name, actual, display);
            guard->dirty_ = true;
        } else {
            guard->reloadWithCursor(actual);
        }
    });
}

void Tab::setCurrent(bool current)
{
    current_ = current;
    if (!current)
        model_->pauseWork();  // rule 13: walks stop with the tab out of sight
    if (current && dirty_) {
        dirty_ = false;
        reload();
    }
}

void Tab::onMonitor()
{
    if (!current_ || view_->isEditing()) {
        dirty_ = true;
        return;
    }
    Q_EMIT folderChanged();
    reload();
}

void Tab::load(const core::Location& dir)
{
    if (!dir.isValid())
        return;
    ++generation_;
    finder_->cancel();
    search_.clear();
    lister_->cancel();
    archive_ = core::Location();
    archiveDir_.clear();
    archiveEntries_.clear();
    monitor_->stop();
    location_ = dir;
    spaceText_.clear();
    model_->setFilter(QString());
    model_->reset(dir);
    listing_->start(dir, generation_);
    Q_EMIT locationChanged(location_);
    Q_EMIT statusChanged(tr("Loading"), QString());
}

void Tab::goUp()
{
    if (isArchive() && !archiveDir_.isEmpty()) {
        const int slash = archiveDir_.lastIndexOf('/');
        cursorName_ = slash < 0 ? archiveDir_ : archiveDir_.mid(slash + 1);
        showArchiveDir(slash < 0 ? QByteArray() : archiveDir_.left(slash));
        return;
    }
    const core::Location parent = location_.parent();
    if (!parent.isValid())
        return;
    cursorName_ = location_.basename();
    navigate(parent);
}

void Tab::setShowHidden(bool on)
{
    if (on == model_->showHidden())
        return;
    cursorName_ = currentName();
    model_->setShowHidden(on);
    placeCursor();
    updateStatus();
}

bool Tab::showHidden() const
{
    return model_->showHidden();
}

bool Tab::recursiveSizes() const
{
    return model_->recursiveSizes();
}

void Tab::setRecursiveSizes(bool on)
{
    model_->setRecursiveSizes(on);
    updateStatus();
}

bool Tab::thumbnails() const
{
    return model_->thumbnails();
}

void Tab::setThumbnails(bool on)
{
    if (on == model_->thumbnails())
        return;
    if (on && model_->thumbnailer()->unavailable()) {
        Q_EMIT message(tr("No thumbnailer on the session bus: install tumbler"));
        return;
    }
    cursorName_ = currentName();
    model_->setThumbnails(on);
    view_->setThumbnailRows(on);
    placeCursor();
}

void Tab::setActive(bool active)
{
    view_->setActivePane(active);
}

bool Tab::execute(keymap::Command command)
{
    using keymap::Command;
    const QModelIndex cur = view_->currentIndex();
    switch (command) {
    case Command::Mark:
        if (cur.isValid()) {
            model_->toggleMarked(cur.row());
            if (cur.row() + 1 < model_->rowCount())
                view_->setCurrentIndex(model_->index(cur.row() + 1, 0));
        }
        return true;
    case Command::MarkToggle:
        if (!model_->filter().isEmpty())
            typed(QStringLiteral(" "));  // Space selects only while no filter is set
        else if (cur.isValid())
            model_->toggleMarked(cur.row());
        return true;
    case Command::Cancel:
        if (model_->filter().isEmpty())
            return false;
        setFilter(QString());
        return true;
    case Command::Rename:
        if (isArchive()) {
            Q_EMIT message(tr("Archives are read-only here"));
            return true;
        }
        if (cur.isValid())
            view_->edit(cur.siblingAtColumn(ListModel::NameColumn));
        return true;
    case Command::Edit: {
        const QByteArray name = currentName();
        if (name.isEmpty() || cursorIsDir() || isArchive())
            return true;
        QString error;
        if (!core::runInTerminal(location_, core::editorCommand() + QLatin1Char(' ')
                                     + QString::fromUtf8(core::shellQuote(name)), false, &error))
            Q_EMIT message(error);
        return true;
    }
    case Command::MarkAll:
        model_->markAll(true);
        return true;
    case Command::InvertMarks:
        model_->invertMarks();
        return true;
    case Command::GoUp:
        if (!model_->filter().isEmpty())
            setFilter(model_->filter().chopped(1));  // Backspace edits the filter first
        else
            goUp();
        return true;
    case Command::Activate:
        activate(cur);
        return true;
    case Command::Reload:
        reload();
        return true;
    case Command::ToggleHidden:
        setShowHidden(!showHidden());
        return true;
    case Command::ToggleSizes:
        model_->setRecursiveSizes(!model_->recursiveSizes());
        updateStatus();
        return true;
    case Command::ToggleThumbnails:
        setThumbnails(!thumbnails());
        return true;
    case Command::View: {
        // F3 on folders walks them; files get the viewer in milestone 5.
        bool any = false;
        if (model_->markedCount() > 0) {
            for (int row = 0; row < model_->rowCount(); ++row)
                if (model_->isMarked(row) && model_->entryAt(row).isDir()) {
                    model_->requestSize(row);
                    any = true;
                }
        } else if (cur.isValid() && model_->entryAt(cur.row()).isDir()) {
            model_->requestSize(cur.row());
            any = true;
        }
        return any;
    }
    default:
        return false;
    }
}

void Tab::onBatch(quint64 generation, std::vector<core::Entry> entries)
{
    if (generation != generation_)
        return;
    model_->append(std::move(entries));
    if (!view_->currentIndex().isValid() && model_->rowCount() > 0)
        view_->setCurrentIndex(model_->index(0, 0));
}

void Tab::onFinished(quint64 generation)
{
    if (generation != generation_)
        return;
    model_->finishLoad();
    if (!pendingMarks_.isEmpty()) {
        model_->applyMarks(pendingMarks_);
        pendingMarks_.clear();
    }
    placeCursor();
    updateStatus();
    space_->request(location_, generation_);
    monitor_->watch(location_);
    if (benchReporter_)
        bench::mark("loaded");
    Q_EMIT loaded();
}

void Tab::onFailed(quint64 generation, const QString& message, core::ListFailure why)
{
    if (generation != generation_)
        return;
    if (why == core::ListFailure::NotDirectory && core::isArchiveName(location_.basename())) {
        enterArchive(location_);
        return;
    }
    if (why == core::ListFailure::NotSupported && !location_.isNative()) {
        // trash://, network://, smb:// and friends are gvfs backends.
        Q_EMIT statusChanged(tr("%1 locations need gvfs, which is not installed")
                                 .arg(location_.uri().section(QLatin1Char(':'), 0, 0)), QString());
        return;
    }
    Q_EMIT statusChanged(message, QString());
    if (why == core::ListFailure::NotMounted)
        Q_EMIT mountNeeded(location_);
}


void Tab::onSpace(quint64 generation, bool ok, quint64 freeBytes)
{
    if (generation != generation_)
        return;
    spaceText_ = ok ? tr("%1 free").arg(QLocale().formattedDataSize(
                          static_cast<qint64>(freeBytes), 1, QLocale::DataSizeIecFormat))
                    : QString();
    updateStatus();
}

void Tab::activate(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    const core::Entry& e = model_->entryAt(index.row());
    if (isArchive()) {
        if (e.isDir()) {
            cursorName_.clear();
            showArchiveDir(archiveDir_.isEmpty() ? e.name : archiveDir_ + '/' + e.name);
        } else {
            Q_EMIT message(tr("F5 extracts; open it from the extracted copy"));
        }
        return;
    }
    if (e.isDir() || core::isArchiveName(e.name)) {
        // A folder, or an archive that the failed listing turns into one.
        cursorName_.clear();
        navigate(location_.child(e.name));
        return;
    }
    QPointer<Tab> guard(this);
    core::openDefault(location_.child(e.name), [guard](const QString& error) {
        if (guard && !error.isEmpty())
            Q_EMIT guard->message(error);
    });
}

void Tab::placeCursor()
{
    if (model_->rowCount() == 0) {
        cursorName_.clear();
        return;
    }
    int row = cursorName_.isEmpty() ? -1 : model_->rowOf(cursorName_);
    const bool restoring = row >= 0;
    if (row < 0)
        row = 0;
    const QModelIndex idx = model_->index(row, 0);
    view_->setCurrentIndex(idx);
    if (scrollValue_ >= 0) {
        view_->verticalScrollBar()->setValue(scrollValue_);
        scrollValue_ = -1;
        view_->scrollTo(idx, QAbstractItemView::EnsureVisible);
    } else {
        view_->scrollTo(idx, restoring ? QAbstractItemView::PositionAtCenter
                                       : QAbstractItemView::PositionAtTop);
    }
    cursorName_.clear();
}

void Tab::updateStatus()
{
    const int shown = model_->rowCount();
    const int hidden = model_->hiddenCount();
    const int marked = model_->markedCount();
    QString text = model_->filter().isEmpty()
        ? (search_.isEmpty() ? tr("%n items", nullptr, shown) : tr("%n matches for %1", nullptr, shown).arg(search_))
        : tr("filter %1: %2 of %3").arg(model_->filter()).arg(shown).arg(model_->totalCount());
    if (isArchive())
        text += tr(", in archive");
    // The Name column may be too narrow for it; the status line is not.
    if (const QModelIndex cur = view_->currentIndex(); cur.isValid())
        text += QStringLiteral("  \u00b7  ") + model_->entryAt(cur.row()).display;
    if (marked > 0)
        text += tr(", %n selected", nullptr, marked);
    if (hidden > 0 && !model_->showHidden())
        text += tr(", %n hidden", nullptr, hidden);
    if (model_->recursiveSizes())
        text += tr(", sizes");
    Q_EMIT statusChanged(text, spaceText_);
}

QByteArray Tab::currentName() const
{
    const QModelIndex idx = view_->currentIndex();
    return idx.isValid() ? model_->entryAt(idx.row()).name : QByteArray();
}
