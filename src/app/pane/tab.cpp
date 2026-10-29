#include "pane/tab.h"

#include "bench.h"
#include "pane/listmodel.h"
#include "pane/listview.h"

#include <QVBoxLayout>

Tab::Tab(QWidget* parent)
    : QWidget(parent)
    , listing_(new core::Listing(this))
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

    connect(listing_, &core::Listing::batch, this, &Tab::onBatch);
    connect(listing_, &core::Listing::finished, this, &Tab::onFinished);
    connect(listing_, &core::Listing::failed, this, &Tab::onFailed);
    connect(view_, &QAbstractItemView::activated, this, &Tab::activate);
    connect(view_, &ListView::goUpRequested, this, &Tab::goUp);
}

void Tab::navigate(const core::Location& dir)
{
    if (!dir.isValid())
        return;
    ++generation_;
    location_ = dir;
    model_->reset(dir);
    listing_->start(dir, generation_);
    Q_EMIT locationChanged(location_);
    Q_EMIT statusChanged(tr("Loading"));
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
    placeCursor();
    updateStatus();
    bench::mark("loaded");
    Q_EMIT loaded();
}

void Tab::onFailed(quint64 generation, const QString& message)
{
    if (generation != generation_)
        return;
    Q_EMIT statusChanged(message);
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
    QString text = tr("%n items", nullptr, shown);
    if (hidden > 0 && !model_->showHidden())
        text += tr(", %n hidden", nullptr, hidden);
    Q_EMIT statusChanged(text);
}

QByteArray Tab::currentName() const
{
    const QModelIndex idx = view_->currentIndex();
    return idx.isValid() ? model_->entryAt(idx.row()).name : QByteArray();
}
