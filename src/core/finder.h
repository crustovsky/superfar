// Find in tree: walks a folder on a worker thread and reports entries
// whose name matches the pattern as a case-insensitive subsequence. Each
// result's name is its path relative to the root, so a tab can treat the
// results like a folder listing (root.child(name) is the item). Never
// follows symlinks, never leaves the root's device.
#pragma once

#include "core/entry.h"
#include "core/location.h"

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace core {

class Finder : public QObject {
    Q_OBJECT
public:
    explicit Finder(QObject* parent = nullptr);
    ~Finder() override;

    void start(const Location& root, const QString& pattern, quint64 generation);
    void cancel();
    bool running() const { return run_ != nullptr; }

    // True when needle is a subsequence of hay, both case-folded.
    static bool nameMatches(const QString& hay, const QString& needle);

Q_SIGNALS:
    void matches(quint64 generation, std::vector<core::Entry> entries);
    void finished(quint64 generation, quint64 scanned);

private:
    struct Run;
    std::shared_ptr<Run> run_;
};

} // namespace core
