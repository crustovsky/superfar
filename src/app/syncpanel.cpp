#include "syncpanel.h"

#include "core/jobs/remove.h"
#include "core/jobs/transfer.h"
#include "jobs/format.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>

namespace {
QString when(qint64 secs)
{
    return secs > 0 ? QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
}
QString sideText(bool exists, bool isDir, quint64 size, qint64 modified)
{
    if (!exists)
        return QStringLiteral("-");
    return (isDir ? QStringLiteral("folder") : jobfmt::bytes(size)) + QStringLiteral("  ") + when(modified);
}
} // namespace

SyncPanel::SyncPanel(QWidget* parent)
    : QWidget(parent)
    , comparer_(new core::Comparer(this))
    , title_(new QLabel(this))
    , bySize_(new QCheckBox(tr("&Size"), this))
    , byTime_(new QCheckBox(tr("&Time"), this))
    , byHash_(new QCheckBox(tr("Content (SHA-256, slo&w)"), this))
    , onlyDifferent_(new QCheckBox(tr("Only di&fferences"), this))
    , table_(new QTreeWidget(this))
    , summary_(new QLabel(this))
    , toRight_(new QPushButton(tr("Copy to &right"), this))
    , toLeft_(new QPushButton(tr("Copy to &left"), this))
    , deleteRight_(new QPushButton(tr("Delete right &orphans"), this))
    , deleteLeft_(new QPushButton(tr("Delete left orp&hans"), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    title_->setWordWrap(true);
    layout->addWidget(title_);
    auto* options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Compare by"), this));
    options->addWidget(bySize_);
    options->addWidget(byTime_);
    options->addWidget(byHash_);
    options->addStretch(1);
    options->addWidget(onlyDifferent_);
    auto* compareBtn = new QPushButton(tr("&Compare"), this);
    options->addWidget(compareBtn);
    layout->addLayout(options);
    bySize_->setChecked(true);
    byTime_->setChecked(true);
    onlyDifferent_->setChecked(true);

    table_->setHeaderLabels({tr("Path"), tr("Left"), tr("Difference"), tr("Right")});
    table_->setRootIsDecorated(false);
    table_->setUniformRowHeights(true);
    table_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table_, 1);

    auto* actions = new QHBoxLayout;
    actions->addWidget(summary_, 1);
    auto* closeBtn = new QPushButton(tr("Clos&e"), this);
    for (QPushButton* b : {toRight_, toLeft_, deleteRight_, deleteLeft_, closeBtn})
        actions->addWidget(b);
    layout->addLayout(actions);

    connect(compareBtn, &QPushButton::clicked, this, &SyncPanel::compare);
    connect(onlyDifferent_, &QCheckBox::toggled, this, [this] { onFinished(generation_, entries_); });
    connect(toRight_, &QPushButton::clicked, this, [this] { copy(Direction::ToRight); });
    connect(toLeft_, &QPushButton::clicked, this, [this] { copy(Direction::ToLeft); });
    connect(deleteRight_, &QPushButton::clicked, this, [this] { removeOrphans(true); });
    connect(deleteLeft_, &QPushButton::clicked, this, [this] { removeOrphans(false); });
    connect(closeBtn, &QPushButton::clicked, this, &SyncPanel::closed);
    connect(comparer_, &core::Comparer::finished, this, &SyncPanel::onFinished);
    connect(comparer_, &core::Comparer::progress, this, [this](quint64 gen, quint64 n) {
        if (gen == generation_)
            summary_->setText(tr("Comparing, %n entries so far", nullptr, static_cast<int>(n)));
    });
    connect(comparer_, &core::Comparer::failed, this, [this](quint64, const QString& m) { summary_->setText(m); });
    setFocusProxy(table_);
}

void SyncPanel::show(const core::Location& left, const core::Location& right)
{
    left_ = left;
    right_ = right;
    title_->setText(tr("Left: %1\nRight: %2").arg(left.displayPath(), right.displayPath()));
    compare();
}

void SyncPanel::compare()
{
    ++generation_;
    entries_.clear();
    table_->clear();
    core::CompareOptions o;
    o.size = bySize_->isChecked();
    o.mtime = byTime_->isChecked();
    o.hash = byHash_->isChecked();
    summary_->setText(tr("Comparing"));
    comparer_->start(left_, right_, o, generation_);
}

QString SyncPanel::describe(core::Difference d)
{
    switch (d) {
    case core::Difference::Same: return tr("same");
    case core::Difference::LeftOnly: return tr("left only");
    case core::Difference::RightOnly: return tr("right only");
    case core::Difference::LeftNewer: return tr("left newer");
    case core::Difference::RightNewer: return tr("right newer");
    case core::Difference::SizeDiffers: return tr("size differs");
    case core::Difference::ContentDiffers: return tr("content differs");
    }
    return {};
}

void SyncPanel::onFinished(quint64 generation, std::vector<core::CompareEntry> entries)
{
    if (generation != generation_)
        return;
    entries_ = std::move(entries);
    table_->clear();
    int different = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const core::CompareEntry& e = entries_[i];
        const bool diff = e.difference != core::Difference::Same;
        different += diff ? 1 : 0;
        if (!diff && onlyDifferent_->isChecked())
            continue;
        auto* row = new QTreeWidgetItem(table_, {e.display, sideText(e.leftExists, e.isDir, e.leftSize, e.leftModified),
                                                 describe(e.difference),
                                                 sideText(e.rightExists, e.isDir, e.rightSize, e.rightModified)});
        row->setData(0, Qt::UserRole, static_cast<qulonglong>(i));
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(0, diff ? Qt::Checked : Qt::Unchecked);
    }
    summary_->setText(tr("%n entries compared, %1 differ", nullptr, static_cast<int>(entries_.size())).arg(different));
}

std::vector<const core::CompareEntry*> SyncPanel::checkedEntries() const
{
    std::vector<const core::CompareEntry*> out;
    for (int i = 0; i < table_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = table_->topLevelItem(i);
        if (row->checkState(0) == Qt::Checked)
            out.push_back(&entries_[row->data(0, Qt::UserRole).toULongLong()]);
    }
    return out;
}

void SyncPanel::copy(Direction direction)
{
    const core::Location& from = direction == Direction::ToRight ? left_ : right_;
    const core::Location& to = direction == Direction::ToRight ? right_ : left_;
    // Rows that exist on the source side, grouped by their parent folder on
    // the target side; one transfer job per group, overwriting.
    std::map<QByteArray, std::vector<core::Location>> groups;
    for (const core::CompareEntry* e : checkedEntries()) {
        const bool exists = direction == Direction::ToRight ? e->leftExists : e->rightExists;
        if (!exists)
            continue;
        const int slash = e->relPath.lastIndexOf('/');
        groups[slash < 0 ? QByteArray() : e->relPath.left(slash)].push_back(from.child(e->relPath));
    }
    std::vector<core::Job*> jobs;
    for (auto& [parentRel, sources] : groups) {
        const core::Location dest = parentRel.isEmpty() ? to : to.child(parentRel);
        auto* job = new core::TransferJob(std::move(sources), dest, false);
        job->setConflictPolicy(core::ConflictAction::Overwrite);
        jobs.push_back(job);
    }
    if (jobs.empty()) {
        Q_EMIT message(tr("Nothing checked on that side"));
        return;
    }
    Q_EMIT runJobs(std::move(jobs));
}

void SyncPanel::removeOrphans(bool onRight)
{
    std::vector<core::Location> victims;
    for (const core::CompareEntry* e : checkedEntries()) {
        if (onRight && e->difference == core::Difference::RightOnly)
            victims.push_back(right_.child(e->relPath));
        if (!onRight && e->difference == core::Difference::LeftOnly)
            victims.push_back(left_.child(e->relPath));
    }
    if (victims.empty()) {
        Q_EMIT message(tr("No checked orphans on that side"));
        return;
    }
    Q_EMIT runJobs({new core::RemoveJob(std::move(victims), false)});  // to the trash
}

void SyncPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT closed();
        return;
    }
    QWidget::keyPressEvent(event);
}
