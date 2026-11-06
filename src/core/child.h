// A child process run to its end for its output: spawned under
// LC_ALL=C.UTF-8 with stdout and stderr piped, read whole, and killed
// when cancelled. Cancelling a GIO read does not stop the child, which
// would then work to the end for nobody (rule 16), so the handle is
// kept and force-exited. One run at a time: a new run cancels the one
// before. The lister, the page renderer and the video probe each hold
// one per kind of call.
#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

typedef struct _GCancellable GCancellable;
typedef struct _GSubprocess GSubprocess;

namespace core {

class Child {
public:
    struct Result {
        bool spawned = false;  // false: the program could not be started
        bool missing = false;  // ... because it is not installed
        bool ok = false;       // it ran and exited with success
        QByteArray out;
        QByteArray err;
        QString error;         // the spawn or read error, when there was one

        // Why a run failed, for a message: what the tool wrote to
        // stderr (its last line only when asked: some tools print what
        // they could first), else the error, else "<tool> failed".
        QString why(const char* tool, bool lastLineOnly = false) const;
    };

    Child() = default;
    ~Child() { cancel(); }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    // argv is null terminated. done runs on the main thread, once,
    // unless cancel() or another run() comes first; when the program
    // cannot be started it runs before run() returns. It may start the
    // next run or destroy the Child.
    void run(const char* const* argv, std::function<void(const Result&)> done);
    void cancel();
    bool running() const { return process_ != nullptr; }

private:
    struct Call;

    GCancellable* cancellable_ = nullptr;
    GSubprocess* process_ = nullptr;
};

} // namespace core
