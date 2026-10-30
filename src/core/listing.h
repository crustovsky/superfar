// Asynchronous directory listing over GIO. Entries arrive in batches on the
// main thread; the first batch is small so the first paint is early.
// Every signal carries the generation the caller passed to start(), so a
// reply from a superseded load can be dropped by the receiver.
#pragma once

#include "core/entry.h"
#include "core/location.h"

#include <QObject>
#include <vector>

namespace core {

enum class ListFailure { Other, NotMounted, NotDirectory };

class Listing : public QObject {
    Q_OBJECT
public:
    explicit Listing(QObject* parent = nullptr);
    ~Listing() override;

    // Cancels any running load and starts a new one.
    void start(const Location& dir, quint64 generation);
    void cancel();
    bool running() const { return job_ != nullptr; }

Q_SIGNALS:
    void batch(quint64 generation, std::vector<core::Entry> entries);
    void finished(quint64 generation);
    void failed(quint64 generation, const QString& message, core::ListFailure why);

private:
    struct Job;
    Job* job_ = nullptr;
};

} // namespace core
