#include "core/compare.h"

#include "core/gobject.h"
#include "core/mainthread.h"

#include <QElapsedTimer>
#include <algorithm>
#include <map>
#include <thread>

namespace core {

namespace {

const char* const kAttributes =
    "standard::name,standard::display-name,standard::type,standard::size,time::modified";

struct Side {
    bool exists = false;
    bool isDir = false;
    quint64 size = 0;
    qint64 modified = 0;
    QString display;
};

QByteArray sha256(GFile* file, GCancellable* cancel)
{
    GError* error = nullptr;
    GPtr<GFileInputStream> in(g_file_read(file, cancel, &error));
    if (!in) {
        if (error)
            g_error_free(error);
        return {};
    }
    GChecksum* sum = g_checksum_new(G_CHECKSUM_SHA256);
    std::vector<guchar> buf(256 * 1024);
    for (;;) {
        const gssize n = g_input_stream_read(G_INPUT_STREAM(in.get()), buf.data(), buf.size(), cancel, nullptr);
        if (n <= 0)
            break;
        g_checksum_update(sum, buf.data(), n);
    }
    const QByteArray out(g_checksum_get_string(sum));
    g_checksum_free(sum);
    return out;
}

} // namespace

struct Comparer::Run {
    Comparer* owner;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    Location left, right;
    CompareOptions options;
    quint64 generation;
    GPtr<GCancellable> cancellable{g_cancellable_new()};
    std::thread thread;
    quint64 scanned = 0;
    QElapsedTimer sinceProgress;

    bool cancelled() const { return g_cancellable_is_cancelled(cancellable.get()); }

    void post(std::function<void(Comparer*)> fn)
    {
        auto alive = this->alive;
        Comparer** ownerRef = &owner;
        postToMain([alive, ownerRef, fn = std::move(fn)] {
            if (*alive && *ownerRef)
                fn(*ownerRef);
        });
    }

    // One folder level: the children of a relative path on one side.
    std::map<QByteArray, Side> list(GFile* dir)
    {
        std::map<QByteArray, Side> out;
        GPtr<GFileEnumerator> en(g_file_enumerate_children(dir, kAttributes, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                           cancellable.get(), nullptr));
        if (!en)
            return out;
        for (;;) {
            GFileInfo* info = nullptr;
            if (!g_file_enumerator_iterate(en.get(), &info, nullptr, cancellable.get(), nullptr) || !info)
                break;
            Side s;
            s.exists = true;
            s.isDir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
            s.size = static_cast<quint64>(g_file_info_get_size(info));
            s.modified = static_cast<qint64>(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED));
            const char* dn = g_file_info_get_display_name(info);
            s.display = dn ? QString::fromUtf8(dn) : QString::fromUtf8(g_file_info_get_name(info));
            out.emplace(QByteArray(g_file_info_get_name(info)), s);
            ++scanned;
        }
        if (sinceProgress.elapsed() >= 200) {
            sinceProgress.restart();
            const quint64 gen = generation, n = scanned;
            post([gen, n](Comparer* c) { Q_EMIT c->progress(gen, n); });
        }
        return out;
    }

    void walk(const QByteArray& rel, const QString& relDisplay, std::vector<CompareEntry>& out)
    {
        if (cancelled())
            return;
        GPtr<GFile> l(rel.isEmpty() ? G_FILE(g_object_ref(left.gfile())) : g_file_get_child(left.gfile(), rel.constData()));
        GPtr<GFile> r(rel.isEmpty() ? G_FILE(g_object_ref(right.gfile())) : g_file_get_child(right.gfile(), rel.constData()));
        const auto ls = list(l.get());
        const auto rs = list(r.get());
        std::map<QByteArray, std::pair<Side, Side>> both;
        for (const auto& [name, s] : ls)
            both[name].first = s;
        for (const auto& [name, s] : rs)
            both[name].second = s;
        for (const auto& [name, sides] : both) {
            if (cancelled())
                return;
            const Side& a = sides.first;
            const Side& b = sides.second;
            const QByteArray childRel = rel.isEmpty() ? name : rel + '/' + name;
            const QString childDisplay = (relDisplay.isEmpty() ? QString() : relDisplay + QLatin1Char('/'))
                + (a.exists ? a.display : b.display);
            if (a.exists && b.exists && a.isDir && b.isDir) {
                walk(childRel, childDisplay, out);
                continue;
            }
            CompareEntry e;
            e.relPath = childRel;
            e.display = childDisplay;
            e.isDir = a.exists ? a.isDir : b.isDir;
            e.leftExists = a.exists;
            e.rightExists = b.exists;
            e.leftSize = a.size;
            e.rightSize = b.size;
            e.leftModified = a.modified;
            e.rightModified = b.modified;
            if (!b.exists)
                e.difference = Difference::LeftOnly;
            else if (!a.exists)
                e.difference = Difference::RightOnly;
            else if (a.isDir != b.isDir)
                e.difference = Difference::ContentDiffers;  // a file here, a folder there
            else {
                e.difference = Difference::Same;
                if (options.size && a.size != b.size)
                    e.difference = Difference::SizeDiffers;
                else if (options.mtime && std::llabs(a.modified - b.modified) > options.mtimeSlackSeconds)
                    e.difference = a.modified > b.modified ? Difference::LeftNewer : Difference::RightNewer;
                else if (options.hash && !a.isDir) {
                    GPtr<GFile> lf(g_file_get_child(left.gfile(), childRel.constData()));
                    GPtr<GFile> rf(g_file_get_child(right.gfile(), childRel.constData()));
                    if (sha256(lf.get(), cancellable.get()) != sha256(rf.get(), cancellable.get()))
                        e.difference = Difference::ContentDiffers;
                }
            }
            out.push_back(std::move(e));
        }
    }

    void run()
    {
        sinceProgress.start();
        std::vector<CompareEntry> out;
        walk(QByteArray(), QString(), out);
        if (cancelled())
            return;
        const quint64 gen = generation;
        post([gen, out = std::move(out)](Comparer* c) mutable { Q_EMIT c->finished(gen, std::move(out)); });
    }
};

Comparer::Comparer(QObject* parent)
    : QObject(parent)
{
}

Comparer::~Comparer()
{
    cancel();
}

void Comparer::start(const Location& left, const Location& right, const CompareOptions& options, quint64 generation)
{
    cancel();
    if (!left.isValid() || !right.isValid()) {
        Q_EMIT failed(generation, QStringLiteral("both sides need a folder"));
        return;
    }
    run_ = std::make_shared<Run>();
    run_->owner = this;
    run_->left = left;
    run_->right = right;
    run_->options = options;
    run_->generation = generation;
    std::shared_ptr<Run> run = run_;
    run_->thread = std::thread([run] { run->run(); });
}

void Comparer::cancel()
{
    if (!run_)
        return;
    g_cancellable_cancel(run_->cancellable.get());
    *run_->alive = false;
    run_->owner = nullptr;
    if (run_->thread.joinable())
        run_->thread.join();
    run_.reset();
}

} // namespace core
