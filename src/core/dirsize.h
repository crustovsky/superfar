// Folder item counts and recursive sizes (rule 9). One DirSizePool per
// window owns a few worker threads and a cache of finished results keyed
// by the folder's device, inode and mtime. A DirSizer is one client of
// the pool, typically one per list model: it asks like MetadataQuery does
// and gets results back on the main thread with the generation it passed,
// so a reply for a folder that is no longer shown is dropped.
//
// A walk never follows symlinks, never crosses onto another device and
// counts a hard-linked file once. Partial totals arrive every 100 ms
// while the walk runs, then one complete result.
#pragma once

#include "core/location.h"

#include <QHash>
#include <QObject>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace core {

struct DirSize {
    quint64 bytes = 0;       // sum of file sizes below the folder (walk only)
    quint64 files = 0;       // non-directory entries seen
    quint64 dirs = 0;        // directories seen, the folder itself excluded
    quint64 direct = 0;      // entries directly inside the folder
    quint64 unreadable = 0;  // folders that could not be listed
    bool recursive = false;  // a walk down the tree, or a direct count only
    bool complete = false;   // false for a partial total while the walk runs
    bool ok = false;         // the folder itself could be read

    quint64 total() const { return files + dirs; }
};

class DirSizer;

class DirSizePool : public QObject {
    Q_OBJECT
public:
    explicit DirSizePool(QObject* parent = nullptr);
    ~DirSizePool() override;

private:
    friend class DirSizer;
    struct Task;
    struct Cache;
    struct Delivery;

    void submit(std::shared_ptr<Task> task);
    void purgeCancelled();
    void startWorkers();
    void workerLoop();
    DirSize compute(const std::shared_ptr<Task>& task);
    void post(const std::shared_ptr<Task>& task, const DirSize& size);
    void deliver(const std::shared_ptr<Task>& task, const DirSize& size);  // main thread

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Task>> queue_;
    std::unique_ptr<Cache> cache_;  // guarded by mutex_
    std::shared_ptr<bool> alive_;   // main thread only
    bool quit_ = false;
};

class DirSizer : public QObject {
    Q_OBJECT
public:
    explicit DirSizer(DirSizePool* pool, QObject* parent = nullptr);
    ~DirSizer() override;

    // Counts the children of dir/name, or walks the whole tree below it.
    void request(const Location& dir, const QByteArray& name, quint64 generation, bool recursive);
    // Drops every request in flight; running walks stop at the next entry.
    void cancelAll();

Q_SIGNALS:
    void result(quint64 generation, const QByteArray& name, const core::DirSize& size);

private:
    friend class DirSizePool;
    void finished(DirSizePool::Task* task);

    DirSizePool* pool_;
    QHash<DirSizePool::Task*, std::shared_ptr<DirSizePool::Task>> tasks_;
};

} // namespace core

Q_DECLARE_METATYPE(core::DirSize)
