#include "pane/tab.h"

#include "bench.h"
#include "pane/listmodel.h"
#include "pane/listview.h"

#include <QLocale>
#include <QVBoxLayout>

Tab::Tab(QWidget* parent)
    : QWidget(parent)
    , listing_(new core::Listing(this))
    , space_(new core::SpaceQuery(this))
    , model_(new ListModel(this))
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
    connect(space_, &core::SpaceQuery::result, this, &Tab::onSpace);
    connect(model_, &ListModel::marksChanged, this, &Tab::updateStatus);
    connect(view_, &QAbstractItemView::activated, this, &Tab::activate);
    connect(view_, &ListView::commandRequested, this, &Tab::commandRequested);
}

void Tab::navigate(const core::Location& dir)
{
    pendingMarks_.clear();
    load(dir);
}

void Tab::reload()
{
    pendingMarks_ = model_->markedNames();
    cursorName_ = currentName();
    load(location_);
}

void Tab::load(const core::Location& dir)
{
    if (!dir.isValid())
        return;
    ++generation_;
    location_ = dir;
    spaceText_.clear();
    model_->reset(dir);
    listing_->start(dir, generation_);
    Q_EMIT locationChanged(location_);
    Q_EMIT statusChanged(tr("Loading"), QString());
}

void Tab::goUp()
{
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
        if (cur.isValid())
            model_->toggleMarked(cur.row());
        return true;
    case Command::MarkAll:
        model_->markAll(true);
        return true;
    case Command::InvertMarks:
        model_->invertMarks();
        return true;
    case Command::GoUp:
        goUp();
        return true;
    case Command::Reload:
        reload();
        return true;
    case Command::ToggleHidden:
        setShowHidden(!showHidden());
        return true;
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
    if (benchReporter_)
        bench::mark("loaded");
    Q_EMIT loaded();
}

void Tab::onFailed(quint64 generation, const QString& message)
{
    if (generation != generation_)
        return;
    Q_EMIT statusChanged(message, QString());
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
    if (e.isDir()) {
        cursorName_.clear();
        navigate(location_.child(e.name));
    }
    // Files open with their default app in milestone 5.
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
    view_->scrollTo(idx, restoring ? QAbstractItemView::PositionAtCenter
                                   : QAbstractItemView::PositionAtTop);
    cursorName_.clear();
}

void Tab::updateStatus()
{
    const int shown = model_->rowCount();
    const int hidden = model_->hiddenCount();
    const int marked = model_->markedCount();
    QString text = tr("%n items", nullptr, shown);
    if (marked > 0)
        text += tr(", %n selected", nullptr, marked);
    if (hidden > 0 && !model_->showHidden())
        text += tr(", %n hidden", nullptr, hidden);
    Q_EMIT statusChanged(text, spaceText_);
}

QByteArray Tab::currentName() const
{
    const QModelIndex idx = view_->currentIndex();
    return idx.isValid() ? model_->entryAt(idx.row()).name : QByteArray();
}
