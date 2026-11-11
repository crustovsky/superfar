// A filesystem job: one worker thread doing synchronous GIO calls, a
// QObject on the main thread reporting progress and asking questions.
// Pause blocks the worker, Skip cancels the current item's cancellable,
// Abort cancels it and ends the run. Conflicts and errors block the
// worker until the main thread answers, unless the job is unattended,
// in which case the default policy applies and the log records it.
#pragma once

#include "core/location.h"
#include "core/undo.h"

#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

typedef struct _GCancellable GCancellable;

namespace core {

enum class JobKind { Copy, Move, Trash, Delete, Extract, Pack, Attributes, Undo, Redo };

struct JobProgress {
    enum class Phase { Scanning, Running, Done };
    Phase phase = Phase::Scanning;
    quint64 itemsDone = 0;
    quint64 itemsTotal = 0;
    quint64 bytesDone = 0;
    quint64 bytesTotal = 0;
    quint64 currentDone = 0;
    quint64 currentTotal = 0;
    QString currentName;
    QString from;  // display paths of the current item
    QString to;
};

enum class ConflictAction { Ask, Skip, Overwrite, OverwriteIfNewer, Rename };
enum class ErrorAction { Ask, Skip, Retry, Abort, DeletePermanently };

struct Conflict {
    QString name;
    QString sourcePath;
    QString targetPath;
    quint64 sourceSize = 0;
    quint64 targetSize = 0;
    qint64 sourceModified = 0;
    qint64 targetModified = 0;
    bool sourceIsDir = false;
    bool targetIsDir = false;
    // The target has no trash to keep what an overwrite replaces, so
    // that overwrite could not be undone.
    bool cannotUndo = false;
};

struct Failure {
    QString name;
    QString path;
    QString message;
    bool offerDelete = false;  // trash refused; permanent delete is an option
};

struct LogEntry {
    // Overwritten: replaced for good, where the target has no trash.
    enum class Kind { Skipped, Failed, Overwritten };
    Kind kind = Kind::Skipped;
    QString path;
    QString message;
};

class Job : public QObject {
    Q_OBJECT
public:
    ~Job() override;

    JobKind kind() const { return kind_; }
    QString title() const;

    void start();
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    void skip();
    void abort();
    void setUnattended(bool on) { unattended_ = on; }
    bool unattended() const { return unattended_; }
    // A policy chosen before the start, so no conflict is asked about.
    void setConflictPolicy(ConflictAction action) { conflictPolicy_ = action; }
    void answerConflict(ConflictAction action, bool applyToAll);
    void answerError(ErrorAction action, bool applyToAll);

    bool aborted() const { return aborted_; }
    bool done() const { return done_; }
    // Valid once finished() has been emitted.
    const std::vector<LogEntry>& log() const { return log_; }
    // What the job did, for the undo stack: valid once finished() has
    // been emitted, and the taker's to move from.
    std::vector<UndoItem>& undoItems() { return undo_; }
    // What the job is about, in the user's words ("12 items", a name).
    void setSubject(const QString& subject) { subject_ = subject; }
    const QString& subject() const { return subject_; }

Q_SIGNALS:
    void progress(const core::JobProgress& p);
    void conflict(const core::Conflict& c);
    void failure(const core::Failure& f);
    void finished();

protected:
    Job(JobKind kind, QObject* parent);
    virtual void run() = 0;
    // Subclass destructors call this first, so the worker is gone before
    // their members are.
    void stop();

    // Worker-side helpers.
    bool waitIfPaused();  // false once aborted
    bool isAborted() const { return aborted_; }
    // A fresh cancellable for the next item. The previous one is released,
    // so never hold the pointer across a nested item.
    GCancellable* beginItem();
    // asked tells an answer given to this very question from a policy.
    ConflictAction askConflict(const Conflict& c, bool* asked = nullptr);
    ErrorAction askError(const Failure& f);
    void report(const JobProgress& p, bool force = false);
    void logSkip(const QString& path, const QString& message = QString());
    void record(UndoItem item);
    // An overwrite is about to replace target: it goes to the trash
    // first, which is what makes the overwrite undoable. False when
    // there is no trash for it; the caller then overwrites for good,
    // and the log says so unless the user was warned when asked.
    bool keepReplaced(const Location& target, GCancellable* cancellable, bool warned);
    static bool canTrash(const Location& target);
    void logFail(const QString& path, const QString& message);

private:
    void cancelCurrent();

    JobKind kind_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> paused_ = false;
    std::atomic<bool> aborted_ = false;
    std::atomic<bool> unattended_ = false;
    std::atomic<bool> done_ = false;
    bool answered_ = false;
    ConflictAction conflictAnswer_ = ConflictAction::Skip;
    ConflictAction conflictPolicy_ = ConflictAction::Ask;
    ErrorAction errorAnswer_ = ErrorAction::Skip;
    ErrorAction errorPolicy_ = ErrorAction::Ask;
    GCancellable* current_ = nullptr;
    std::vector<LogEntry> log_;
    std::vector<UndoItem> undo_;
    QString subject_;
    qint64 lastReport_ = 0;
};

} // namespace core

Q_DECLARE_METATYPE(core::JobProgress)
Q_DECLARE_METATYPE(core::Conflict)
Q_DECLARE_METATYPE(core::Failure)
