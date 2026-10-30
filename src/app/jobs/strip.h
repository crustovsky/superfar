// The bar area under the panes: one collapsed row per running job, each
// expandable into its panel; conflict and error questions under their
// job; prompts and confirmations for what is about to start; the log of
// a job that ended with skips or failures. Hidden when empty.
#pragma once

#include "core/jobs/job.h"
#include "jobs/panel.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>
#include <optional>
#include <vector>

class JobPanel;
class AuthBar;
class ChoiceBar;
class GoToBar;
class PromptBar;
class QLabel;
class QProgressBar;
class QToolButton;
class QVBoxLayout;

class JobRow : public QWidget {
    Q_OBJECT
public:
    JobRow(core::Job* job, QWidget* parent = nullptr);

    core::Job* job() const { return job_; }
    void addBar(QWidget* bar);

private:
    void onProgress(const core::JobProgress& p);
    void sample();
    void refresh();

    core::Job* job_;
    core::JobProgress last_;
    QElapsedTimer runClock_;
    bool running_ = false;
    quint64 sampleBytes_ = 0;
    qint64 sampleMs_ = 0;
    double rateSmooth_ = 0;
    std::vector<double> history_;

    QVBoxLayout* layout_;
    QLabel* name_;
    QProgressBar* bar_;
    QLabel* info_;
    QToolButton* pause_ = nullptr;
    QToolButton* details_ = nullptr;
    JobPanel* panel_;
};

class JobStrip : public QWidget {
    Q_OBJECT
public:
    explicit JobStrip(QWidget* parent = nullptr);

    // Takes ownership of the job and starts it.
    void addJob(core::Job* job);
    // A prompt; the caller connects accepted() / rejected(), the strip
    // removes the bar afterwards and gives focus back.
    PromptBar* prompt(const QString& message, const QString& okLabel, std::optional<QString> input);
    ChoiceBar* choose(const QString& message, const QStringList& items);
    AuthBar* auth(const QString& message, const QString& user, const QString& domain, bool needUser,
                  bool needDomain, bool needPassword, bool anonymousOk);
    GoToBar* goTo();

Q_SIGNALS:
    void jobFinished(core::Job* job);

private:
    void showBar(QWidget* bar, QWidget* under = nullptr);
    void removeBar(QWidget* bar);
    void updateVisibility();

    QVBoxLayout* layout_;
    QPointer<QWidget> returnFocus_;
    int rows_ = 0;
    int bars_ = 0;
};
