#include "core/dirsize.h"

#include "core/gobject.h"

#include <QElapsedTimer>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace core {

namespace {

const char* const kRootAttributes =
    "standard::type,unix::device,unix::inode,time::modified,time::modified-usec";
// With the name: the local backend sends it unasked, a daemon backend
// (admin://, sftp) does not, and the enumerator cannot hand out
// children without it.
const char* const kWalkAttributes =
    "standard::name,standard::type,standard::size,unix::device,unix::inode,unix::nlink";
const char* const kCountAttributes = "standard::name,standard::type";

constexpr qint64 kPartialIntervalMs = 100;
constexpr size_t kCacheLimit = 8192;

struct CacheKey {
    quint64 device = 0;
    quint64 inode = 0;
    quint64 modified = 0;  // seconds and microseconds folded together
    bool recursive = false;

    bool operator==(const CacheKey&) const = default;
};

struct CacheHash {
    size_t operator()(const CacheKey& k) const noexcept
    {
        size_t h = std::hash<quint64>{}(k.device);
        h ^= std::hash<quint64>{}(k.inode) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<quint64>{}(k.modified) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h ^ (k.recursive ? 1 : 0);
    }
};

} // namespace

struct DirSizePool::Cache : std::unordered_map<CacheKey, DirSize, CacheHash> {};

struct DirSizePool::Task {
    DirSizer* client = nullptr;  // touched on the main thread only
    quint64 generation = 0;
    QByteArray name;
    Location dir;
    bool recursive = false;
    GPtr<GCancellable> cancellable;

    bool cancelled() const { return g_cancellable_is_cancelled(cancellable.get()); }
};

DirSizePool::DirSizePool(QObject* parent)
    : QObject(parent)
    , cache_(std::make_unique<Cache>())
    , alive_(std::make_shared<bool>(true))
{
    qRegisterMetaType<DirSize>();
}

DirSizePool::~DirSizePool()
{
    *alive_ = false;  // deliveries still queued on the main loop do nothing
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        for (const auto& t : queue_)
            g_cancellable_cancel(t->cancellable.get());
        queue_.clear();
    }
    cv_.notify_all();
    for (auto& w : workers_)
        w.join();
}

void DirSizePool::startWorkers()
{
    if (!workers_.empty())
        return;
    const unsigned n = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
    for (unsigned i = 0; i < n; ++i)
        workers_.emplace_back([this] { workerLoop(); });
}

void DirSizePool::submit(std::shared_ptr<Task> task)
{
    startWorkers();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void DirSizePool::purgeCancelled()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(queue_, [](const std::shared_ptr<Task>& t) { return t->cancelled(); });
}

void DirSizePool::workerLoop()
{
    for (;;) {
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return quit_ || !queue_.empty(); });
            if (quit_)
                return;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        if (task->cancelled())
            continue;
        const DirSize size = compute(task);
        if (!task->cancelled())
            post(task, size);
    }
}

DirSize DirSizePool::compute(const std::shared_ptr<Task>& taskPtr)
{
    Task& task = *taskPtr;
    DirSize r;
    r.recursive = task.recursive;
    r.complete = true;  // cleared below while a walk is still running
    const Location root = task.dir.child(task.name);
    GCancellable* cancel = task.cancellable.get();
    if (!root.isValid())
        return r;

    GError* error = nullptr;
    GPtr<GFileInfo> rootInfo(g_file_query_info(root.gfile(), kRootAttributes,
                                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &error));
    GErrorPtr err(error);
    if (!rootInfo)
        return r;

    // Local folders have a device and inode: the key for the cache. A
    // folder's mtime changes when its direct children change, which is
    // exact for a count and a good enough signal for a walk.
    const bool haveKey = g_file_info_has_attribute(rootInfo.get(), G_FILE_ATTRIBUTE_UNIX_DEVICE)
        && g_file_info_has_attribute(rootInfo.get(), G_FILE_ATTRIBUTE_UNIX_INODE)
        && g_file_info_has_attribute(rootInfo.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED);
    CacheKey key;
    if (haveKey) {
        key.device = g_file_info_get_attribute_uint32(rootInfo.get(), G_FILE_ATTRIBUTE_UNIX_DEVICE);
        key.inode = g_file_info_get_attribute_uint64(rootInfo.get(), G_FILE_ATTRIBUTE_UNIX_INODE);
        key.modified = g_file_info_get_attribute_uint64(rootInfo.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED) * 1000000
            + g_file_info_get_attribute_uint32(rootInfo.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
        key.recursive = task.recursive;
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = cache_->find(key); it != cache_->end())
            return it->second;
    }

    const char* attributes = task.recursive ? kWalkAttributes : kCountAttributes;
    std::vector<GPtr<GFile>> pending;
    pending.emplace_back(G_FILE(g_object_ref(root.gfile())));
    std::unordered_set<quint64> linkedSeen;  // (device, inode) of files with nlink > 1
    QElapsedTimer sincePartial;
    sincePartial.start();
    bool atRoot = true;

    while (!pending.empty() && !task.cancelled()) {
        GPtr<GFile> dir = std::move(pending.back());
        pending.pop_back();
        GPtr<GFileEnumerator> en(g_file_enumerate_children(
            dir.get(), attributes, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &error));
        err.reset(error);
        error = nullptr;
        if (!en) {
            if (atRoot)
                return r;  // ok stays false
            ++r.unreadable;
            continue;
        }
        r.ok = true;
        for (;;) {
            GFileInfo* info = nullptr;
            GFile* child = nullptr;
            if (!g_file_enumerator_iterate(en.get(), &info, &child, cancel, &error)) {
                err.reset(error);
                error = nullptr;
                ++r.unreadable;
                break;
            }
            if (!info)
                break;
            if (atRoot)
                ++r.direct;
            if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
                ++r.dirs;
                const bool sameDevice = !haveKey
                    || !g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_DEVICE)
                    || g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_DEVICE) == key.device;
                if (task.recursive && sameDevice)
                    pending.emplace_back(G_FILE(g_object_ref(child)));
            } else {
                ++r.files;
                if (task.recursive) {
                    bool counted = false;
                    if (g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_NLINK) > 1) {
                        const quint64 id = (quint64(g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_DEVICE)) << 48)
                            ^ g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_UNIX_INODE);
                        counted = !linkedSeen.insert(id).second;
                    }
                    if (!counted)
                        r.bytes += static_cast<quint64>(g_file_info_get_size(info));
                }
            }
            if (task.recursive && sincePartial.elapsed() >= kPartialIntervalMs) {
                sincePartial.restart();
                if (task.cancelled())
                    break;
                r.complete = false;
                post(taskPtr, r);
                r.complete = true;
            }
        }
        atRoot = false;
    }
    if (task.cancelled())
        return r;
    if (haveKey && r.ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_->size() >= kCacheLimit)
            cache_->clear();
        (*cache_)[key] = r;
    }
    return r;
}

// A result crossing to the main thread. It travels as a GLib idle source
// on the default main context, the road every GIO async completion takes
// into this app, rather than as a Qt posted event: the main loop is
// GLib's, and a source attached from a worker always wakes it.
struct DirSizePool::Delivery {
    DirSizePool* pool;
    std::shared_ptr<bool> alive;
    std::shared_ptr<Task> task;
    DirSize size;

    static gboolean dispatch(gpointer data)
    {
        auto* d = static_cast<Delivery*>(data);
        if (*d->alive)
            d->pool->deliver(d->task, d->size);
        return G_SOURCE_REMOVE;
    }
    static void destroy(gpointer data) { delete static_cast<Delivery*>(data); }
};

void DirSizePool::post(const std::shared_ptr<Task>& task, const DirSize& size)
{
    GSource* source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(source, &Delivery::dispatch, new Delivery{this, alive_, task, size},
                          &Delivery::destroy);
    g_source_attach(source, nullptr);
    g_source_unref(source);
}

void DirSizePool::deliver(const std::shared_ptr<Task>& task, const DirSize& size)
{
    if (task->cancelled() || !task->client)
        return;
    DirSizer* client = task->client;
    if (size.complete)
        client->finished(task.get());
    Q_EMIT client->result(task->generation, task->name, size);
}

DirSizer::DirSizer(DirSizePool* pool, QObject* parent)
    : QObject(parent)
    , pool_(pool)
{
}

DirSizer::~DirSizer()
{
    cancelAll();
}

void DirSizer::request(const Location& dir, const QByteArray& name, quint64 generation, bool recursive)
{
    if (!dir.isValid())
        return;
    auto task = std::make_shared<DirSizePool::Task>();
    task->client = this;
    task->generation = generation;
    task->name = name;
    task->dir = dir;
    task->recursive = recursive;
    task->cancellable.reset(g_cancellable_new());
    if (!pool_)
        return;
    tasks_.insert(task.get(), task);
    pool_->submit(std::move(task));
}

void DirSizer::cancelAll()
{
    if (tasks_.isEmpty())
        return;
    for (const auto& task : tasks_) {
        g_cancellable_cancel(task->cancellable.get());
        task->client = nullptr;
    }
    tasks_.clear();
    if (pool_)
        pool_->purgeCancelled();
}

void DirSizer::finished(DirSizePool::Task* task)
{
    tasks_.remove(task);
}

} // namespace core
