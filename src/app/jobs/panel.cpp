#include "jobs/panel.h"

#include "jobs/format.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QToolButton>
#include <algorithm>

ThroughputGraph::ThroughputGraph(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ThroughputGraph::setHistory(const std::vector<double>* history, bool bytesUnit)
{
    history_ = history;
    bytesUnit_ = bytesUnit;
    update();
}

void ThroughputGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    p.fillRect(r, palette().color(QPalette::Base));
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(r);
    if (!history_ || history_->size() < 2)
        return;

    const double peak = std::max(1.0, *std::max_element(history_->begin(), history_->end()));
    const int n = static_cast<int>(history_->size());
    const double dx = static_cast<double>(r.width() - 2) / std::max(1, n - 1);
    QPainterPath path;
    path.moveTo(r.left() + 1, r.bottom() - 1);
    for (int i = 0; i < n; ++i) {
        const double y = r.bottom() - 1 - (*history_)[i] / peak * (r.height() - 4);
        path.lineTo(r.left() + 1 + i * dx, y);
    }
    path.lineTo(r.left() + 1 + (n - 1) * dx, r.bottom() - 1);
    path.closeSubpath();

    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlpha(70);
    p.fillPath(path, fill);
    p.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
    p.drawPath(path);

    p.setPen(palette().color(QPalette::Text));
    p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft, jobfmt::rate(peak, bytesUnit_));
}

JobPanel::JobPanel(core::Job* job, QWidget* parent)
    : QWidget(parent)
    , job_(job)
    , from_(new QLabel(this))
    , to_(new QLabel(this))
    , currentName_(new QLabel(this))
    , current_(new QProgressBar(this))
    , overall_(new QProgressBar(this))
    , overallText_(new QLabel(this))
    , times_(new QLabel(this))
    , graph_(new ThroughputGraph(this))
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(6, 2, 6, 4);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);

    int row = 0;
    grid->addWidget(new QLabel(tr("From"), this), row, 0);
    grid->addWidget(from_, row++, 1);
    grid->addWidget(new QLabel(tr("To"), this), row, 0);
    grid->addWidget(to_, row++, 1);
    grid->addWidget(currentName_, row, 0);
    grid->addWidget(current_, row++, 1);
    grid->addWidget(overallText_, row, 0);
    grid->addWidget(overall_, row++, 1);
    grid->addWidget(times_, row, 0);
    grid->addWidget(graph_, row++, 1);

    auto* buttons = new QHBoxLayout;
    auto add = [&](const QString& text) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(b);
        return b;
    };
    auto* skip = add(tr("Skip"));
    pause_ = add(tr("Pause"));
    auto* unattended = add(tr("Unattended"));
    auto* abort = add(tr("Abort"));
    pause_->setCheckable(true);
    unattended->setCheckable(true);
    buttons->addStretch(1);
    grid->addLayout(buttons, row++, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    for (QLabel* l : {from_, to_, currentName_})
        l->setTextFormat(Qt::PlainText);
    from_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    to_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    current_->setTextVisible(false);
    overall_->setTextVisible(true);

    connect(skip, &QToolButton::clicked, job_, &core::Job::skip);
    connect(pause_, &QToolButton::toggled, job_, &core::Job::setPaused);
    connect(unattended, &QToolButton::toggled, job_, &core::Job::setUnattended);
    connect(abort, &QToolButton::clicked, job_, &core::Job::abort);
}

void JobPanel::update(const core::JobProgress& p, const JobStats& stats)
{
    from_->setText(p.from);
    to_->setText(p.to);
    currentName_->setText(p.currentName);
    if (p.currentTotal > 0) {
        current_->setRange(0, 1000);
        current_->setValue(static_cast<int>(p.currentDone * 1000 / p.currentTotal));
    } else {
        current_->setRange(0, 1);
        current_->setValue(p.currentDone > 0 ? 1 : 0);
    }
    if (p.bytesTotal > 0) {
        overall_->setRange(0, 1000);
        overall_->setValue(static_cast<int>(p.bytesDone * 1000 / p.bytesTotal));
        overallText_->setText(tr("%1 / %2 items\n%3 / %4")
                                  .arg(p.itemsDone).arg(p.itemsTotal)
                                  .arg(jobfmt::bytes(p.bytesDone), jobfmt::bytes(p.bytesTotal)));
    } else {
        overall_->setRange(0, static_cast<int>(std::max<quint64>(1, p.itemsTotal)));
        overall_->setValue(static_cast<int>(p.itemsDone));
        overallText_->setText(tr("%1 / %2 items").arg(p.itemsDone).arg(p.itemsTotal));
    }
    times_->setText(tr("Elapsed %1\nLeft %2\n%3")
                        .arg(jobfmt::duration(stats.elapsedMs), jobfmt::duration(stats.remainingMs),
                             jobfmt::rate(stats.rate, stats.bytesUnit)));
    graph_->setHistory(stats.history, stats.bytesUnit);
    pause_->setChecked(job_->paused());
}
