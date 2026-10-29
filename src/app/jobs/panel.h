// The expanded view of one job: from / to, current item bar, overall bar,
// elapsed and remaining, throughput graph, and the four controls.
#pragma once

#include "core/jobs/job.h"

#include <QWidget>
#include <vector>

class QLabel;
class QProgressBar;
class QToolButton;

struct JobStats {
    qint64 elapsedMs = 0;
    qint64 remainingMs = -1;
    double rate = 0;  // per second, smoothed
    bool bytesUnit = true;  // bytes per second, else items per second
    const std::vector<double>* history = nullptr;
};

class ThroughputGraph : public QWidget {
    Q_OBJECT
public:
    explicit ThroughputGraph(QWidget* parent = nullptr);
    void setHistory(const std::vector<double>* history, bool bytesUnit);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const std::vector<double>* history_ = nullptr;
    bool bytesUnit_ = true;
};

class JobPanel : public QWidget {
    Q_OBJECT
public:
    explicit JobPanel(core::Job* job, QWidget* parent = nullptr);

    void update(const core::JobProgress& p, const JobStats& stats);

private:
    core::Job* job_;
    QLabel* from_;
    QLabel* to_;
    QLabel* currentName_;
    QProgressBar* current_;
    QProgressBar* overall_;
    QLabel* overallText_;
    QLabel* times_;
    ThroughputGraph* graph_;
    QToolButton* pause_ = nullptr;
};
