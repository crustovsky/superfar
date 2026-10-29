#include "jobs/strip.h"

#include "jobs/bars.h"
#include "jobs/format.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int kSampleMs = 500;
constexpr size_t kHistory = 120;
} // namespace

JobRow::JobRow(core::Job* job, QWidget* parent)
    : QWidget(parent)
    , job_(job)
    , layout_(new QVBoxLayout(this))
    , name_(new QLabel(this))
    , bar_(new QProgressBar(this))
    , info_(new QLabel(this))
    , panel_(new JobPanel(job, this))
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    auto* line = new QWidget(this);
    auto* row = new QHBoxLayout(line);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);
    auto* title = new QLabel(job->title(), this);
    title->setMinimumWidth(title->fontMetrics().horizontalAdvance(QStringLiteral("Delete")) + 4);
    row->addWidget(title);
    name_->setTextFormat(Qt::PlainText);
    name_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row->addWidget(name_, 2);
    bar_->setTextVisible(true);
    bar_->setMinimumWidth(160);
    row->addWidget(bar_, 1);
    row->addWidget(info_);

    auto add = [&](const QString& text) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        row->addWidget(b);
        return b;
    };
    pause_ = add(tr("Pause"));
    pause_->setCheckable(true);
    auto* abort = add(tr("Abort"));
    details_ = add(tr("Details"));
    details_->setCheckable(true);

    layout_->addWidget(line);
    layout_->addWidget(panel_);
    panel_->hide();

    connect(pause_, &QToolButton::toggled, job_, &core::Job::setPaused);
    connect(abort, &QToolButton::clicked, job_, &core::Job::abort);
    connect(details_, &QToolButton::toggled, this, [this](bool on) {
        panel_->setVisible(on);
        // Only an open panel takes extra height from the bar area.
        setSizePolicy(QSizePolicy::Preferred, on ? QSizePolicy::Expanding : QSizePolicy::Fixed);
    });
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(job_, &core::Job::progress, this, &JobRow::onProgress);

    auto* timer = new QTimer(this);
    timer->setInterval(kSampleMs);
    connect(timer, &QTimer::timeout, this, &JobRow::sample);
    timer->start();
    name_->setText(tr("Scanning"));
}

void JobRow::addBar(QWidget* bar)
{
    layout_->addWidget(bar);
}

void JobRow::onProgress(const core::JobProgress& p)
{
    if (p.phase == core::JobProgress::Phase::Running && !running_) {
        running_ = true;
        runClock_.start();
        sampleBytes_ = p.bytesDone;
        sampleMs_ = 0;
        // A job still going after a couple of seconds deserves its panel.
        QTimer::singleShot(2000, this, [this] {
            if (running_ && last_.phase == core::JobProgress::Phase::Running)
                details_->setChecked(true);
        });
    }
    last_ = p;
    refresh();
}

void JobRow::sample()
{
    if (!running_ || job_->paused())
        return;
    const qint64 now = runClock_.elapsed();
    const qint64 dt = now - sampleMs_;
    if (dt <= 0)
        return;
    // Bytes per second when the job moves bytes, else items per second.
    const bool bytes = last_.bytesTotal > 0;
    const quint64 done = bytes ? last_.bytesDone : last_.itemsDone;
    const double rate = static_cast<double>(done - sampleBytes_) * 1000.0 / dt;
    sampleBytes_ = done;
    sampleMs_ = now;
    rateSmooth_ = rateSmooth_ == 0 ? rate : rateSmooth_ * 0.7 + rate * 0.3;
    history_.push_back(rate);
    if (history_.size() > kHistory)
        history_.erase(history_.begin());
    refresh();
}

void JobRow::refresh()
{
    const core::JobProgress& p = last_;
    JobStats stats;
    stats.elapsedMs = running_ ? runClock_.elapsed() : 0;
    stats.rate = rateSmooth_;
    stats.bytesUnit = p.bytesTotal > 0;
    stats.history = &history_;
    if (running_ && stats.elapsedMs > 1000) {
        if (p.bytesTotal > 0 && p.bytesDone > 0)
            stats.remainingMs = static_cast<qint64>(
                static_cast<double>(p.bytesTotal - p.bytesDone) * stats.elapsedMs / p.bytesDone);
        else if (p.itemsTotal > 0 && p.itemsDone > 0)
            stats.remainingMs = static_cast<qint64>(
                static_cast<double>(p.itemsTotal - p.itemsDone) * stats.elapsedMs / p.itemsDone);
    }

    if (p.phase == core::JobProgress::Phase::Scanning) {
        name_->setText(tr("Scanning: %1 items, %2").arg(p.itemsTotal).arg(jobfmt::bytes(p.bytesTotal)));
        bar_->setRange(0, 0);
        info_->clear();
    } else {
        name_->setText(p.currentName);
        if (p.bytesTotal > 0) {
            bar_->setRange(0, 1000);
            bar_->setValue(static_cast<int>(p.bytesDone * 1000 / p.bytesTotal));
            info_->setText(QStringLiteral("%1  %2 left").arg(jobfmt::rate(stats.rate, true), jobfmt::duration(stats.remainingMs)));
        } else {
            bar_->setRange(0, static_cast<int>(std::max<quint64>(1, p.itemsTotal)));
            bar_->setValue(static_cast<int>(p.itemsDone));
            info_->setText(tr("%1 / %2  %3 left").arg(p.itemsDone).arg(p.itemsTotal).arg(jobfmt::duration(stats.remainingMs)));
        }
    }
    pause_->setChecked(job_->paused());
    if (panel_->isVisible())
        panel_->update(p, stats);
}

JobStrip::JobStrip(QWidget* parent)
    : QWidget(parent)
    , layout_(new QVBoxLayout(this))
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(1);
    hide();
}

void JobStrip::addJob(core::Job* job)
{
    job->setParent(this);
    auto* row = new JobRow(job, this);
    layout_->addWidget(row);
    ++rows_;
    updateVisibility();

    connect(job, &core::Job::conflict, this, [this, job, row](const core::Conflict& c) {
        auto* bar = new ConflictBar(c, this);
        connect(bar, &ConflictBar::answered, this, [this, job, bar](core::ConflictAction a, bool all) {
            job->answerConflict(a, all);
            removeBar(bar);
        });
        showBar(bar, row);
    });
    connect(job, &core::Job::failure, this, [this, job, row](const core::Failure& f) {
        auto* bar = new ErrorBar(job->title().toLower(), f, this);
        connect(bar, &ErrorBar::answered, this, [this, job, bar](core::ErrorAction a, bool all) {
            job->answerError(a, all);
            removeBar(bar);
        });
        showBar(bar, row);
    });
    connect(job, &core::Job::finished, this, [this, job, row] {
        layout_->removeWidget(row);
        row->hide();
        row->deleteLater();
        --rows_;
        if (!job->log().empty()) {
            int skipped = 0, failed = 0;
            for (const auto& e : job->log())
                (e.kind == core::LogEntry::Kind::Skipped ? skipped : failed)++;
            const QString title = tr("%1 %2: %3 skipped, %4 failed")
                                      .arg(job->title(), job->aborted() ? tr("aborted") : tr("finished"))
                                      .arg(skipped).arg(failed);
            auto* bar = new LogBar(title, job->log(), this);
            connect(bar, &LogBar::closed, this, [this, bar] { removeBar(bar); });
            showBar(bar);
        }
        updateVisibility();
        Q_EMIT jobFinished(job);
        job->deleteLater();
    });
    job->start();
}

PromptBar* JobStrip::prompt(const QString& message, const QString& okLabel, std::optional<QString> input)
{
    auto* bar = new PromptBar(message, okLabel, std::move(input), this);
    connect(bar, &PromptBar::accepted, this, [this, bar] { removeBar(bar); });
    connect(bar, &PromptBar::rejected, this, [this, bar] { removeBar(bar); });
    showBar(bar);
    return bar;
}

void JobStrip::showBar(QWidget* bar, QWidget* under)
{
    if (bars_ == 0)
        returnFocus_ = QApplication::focusWidget();
    ++bars_;
    if (auto* row = qobject_cast<JobRow*>(under))
        row->addBar(bar);
    else
        layout_->addWidget(bar);
    updateVisibility();
    bar->setFocus();
}

void JobStrip::removeBar(QWidget* bar)
{
    bar->hide();
    bar->deleteLater();
    --bars_;
    if (bars_ == 0 && returnFocus_)
        returnFocus_->setFocus();
    updateVisibility();
}

void JobStrip::updateVisibility()
{
    setVisible(rows_ + bars_ > 0);
}
