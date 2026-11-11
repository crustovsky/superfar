// The bar area under the panes: one collapsed row per running job, each
// expandable into its panel; conflict and error questions under their
// job; prompts and confirmations for what is about to start; the log of
// a job that ended with skips or failures. Hidden when empty.
#pragma once

#include "core/apps.h"
#include "core/jobs/job.h"
#include "core/jobs/queue.h"
#include "jobs/panel.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>
#include <optional>
#include <vector>

class JobPanel;
class AuthBar;
class CheckBar;
class ChoiceBar;
class OpenWithBar;
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
    // A conflict or error question is open under the row: the job is
    // blocked, so the clock stops and the panel does not open on its own.
    void setWaiting(bool on) { waiting_ = on; }

Q_SIGNALS:
    void startNowRequested();

private:
    void onProgress(const core::JobProgress& p);
    void sample();
    void refresh();
    // A job waiting in the queue has no progress to show: its row says
    // what it will do and offers Start now, Unattended and Abort.
    void setQueued(bool queued);

    core::Job* job_;
    core::JobProgress last_;
    QElapsedTimer runClock_;
    bool running_ = false;
    bool waiting_ = false;
    bool autoOpened_ = false;
    qint64 activeMs_ = 0;  // running time less pauses and questions
    quint64 sampleBytes_ = 0;
    qint64 sampleMs_ = 0;
    double rateSmooth_ = 0;
    std::vector<double> history_;

    QVBoxLayout* layout_;
    QLabel* name_;
    QProgressBar* bar_;
    QLabel* info_;
    QToolButton* start_ = nullptr;
    QToolButton* unattended_ = nullptr;
    QToolButton* pause_ = nullptr;
    QToolButton* details_ = nullptr;
    JobPanel* panel_;
};

class JobStrip : public QWidget {
    Q_OBJECT
public:
    explicit JobStrip(QWidget* parent = nullptr);

    // Takes ownership of the job and starts it, or with queued puts it
    // behind the jobs already there.
    void addJob(core::Job* job, bool queued = false);
    const core::JobQueue* queue() const { return queue_; }
    // A prompt; the caller connects accepted() / rejected(), the strip
    // removes the bar afterwards and gives focus back. A prompt for a
    // job offers Queue beside its button while other jobs are there;
    // the bar's queued() then says which one was chosen.
    PromptBar* prompt(const QString& message, const QString& okLabel, std::optional<QString> input,
                      bool forJob = false);
    ChoiceBar* choose(const QString& message, const QStringList& items);
    CheckBar* checks(const QString& message);  // the caller fills it
    AuthBar* auth(const QString& message, const QString& user, const QString& domain, bool needUser,
                  bool needDomain, bool needPassword, bool anonymousOk);
    GoToBar* goTo();
    OpenWithBar* openWith(const QString& title, std::vector<core::AppChoice> apps);

Q_SIGNALS:
    void jobFinished(core::Job* job);

private:
    void showBar(QWidget* bar, QWidget* under = nullptr);
    void removeBar(QWidget* bar);
    void updateVisibility();

    QVBoxLayout* layout_;
    core::JobQueue* queue_;
    QPointer<QWidget> returnFocus_;
    int rows_ = 0;
    int bars_ = 0;
};
