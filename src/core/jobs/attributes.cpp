#include "core/jobs/attributes.h"

#include "core/fileops.h"
#include "core/gobject.h"

namespace core {

namespace {

const char* const kAttrs = "standard::name,standard::type,unix::mode,unix::device";
constexpr GFileQueryInfoFlags kNoFollow = G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;

// With links not followed a link to a folder is a link, not a folder.
bool isDir(GFileInfo* info)
{
    return g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
}

quint32 deviceOf(GFileInfo* info)
{
    return g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_DEVICE);
}

QString takeMessage(GError* error)
{
    GErrorPtr err(error);
    return err ? errorMessage(err.get()) : QStringLiteral("failed");
}

} // namespace

struct AttributesJob::Impl {
    AttributesJob& job;
    JobProgress p;

    // The items a run will touch, for the bar.
    void count(const Location& loc, GFileInfo* info)
    {
        ++p.itemsTotal;
        job.report(p);
        if (!job.change_.recursive || !isDir(info))
            return;
        GPtr<GFileEnumerator> en(g_file_enumerate_children(loc.gfile(), kAttrs, kNoFollow, nullptr, nullptr));
        while (en && !job.isAborted()) {
            GPtr<GFileInfo> child(g_file_enumerator_next_file(en.get(), nullptr, nullptr));
            if (!child)
                break;
            if (isDir(child.get()) && deviceOf(child.get()) != deviceOf(info))
                continue;
            count(loc.child(g_file_info_get_name(child.get())), child.get());
        }
    }

    // One item. Empty when done, else the system's words.
    QString set(const Location& loc, GFileInfo* info, bool below)
    {
        const AttributeChange& c = job.change_;
        const bool link = g_file_info_get_file_type(info) == G_FILE_TYPE_SYMBOLIC_LINK;
        quint32 old = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
        GError* error = nullptr;
        if (!c.owner.isEmpty()) {
            GPtr<GFileInfo> change(g_file_info_new());
            if (c.owner.uid)
                g_file_info_set_attribute_uint32(change.get(), G_FILE_ATTRIBUTE_UNIX_UID, *c.owner.uid);
            if (c.owner.gid)
                g_file_info_set_attribute_uint32(change.get(), G_FILE_ATTRIBUTE_UNIX_GID, *c.owner.gid);
            if (!g_file_set_attributes_from_info(loc.gfile(), change.get(), kNoFollow, nullptr, &error))
                return takeMessage(error);
            if (c.mode && !link) {
                // A change of owner drops setuid and setgid, on purpose:
                // the mode is merged with what is there now, not with
                // what was, so they are not put back behind its back.
                GPtr<GFileInfo> now(g_file_query_info(loc.gfile(), G_FILE_ATTRIBUTE_UNIX_MODE, kNoFollow, nullptr, &error));
                if (!now)
                    return takeMessage(error);
                old = g_file_info_get_attribute_uint32(now.get(), G_FILE_ATTRIBUTE_UNIX_MODE);
            }
        }
        if (!c.mode || link)
            return {};
        quint32 mask = c.modeMask;
        if (below && !isDir(info) && !(old & 0111))
            mask &= ~0111u;  // chmod's X: no execute for a file that has none
        const quint32 mode = mergedMode(old, *c.mode, mask);
        if (mode == (old & 07777))
            return {};
        if (!g_file_set_attribute_uint32(loc.gfile(), G_FILE_ATTRIBUTE_UNIX_MODE, mode, kNoFollow, nullptr, &error))
            return takeMessage(error);
        return {};
    }

    // Asks what to do about a refusal. False means abort; retry says
    // whether to try the same thing again.
    bool refused(const Location& loc, const QString& message, bool* retry)
    {
        Failure f;
        f.name = loc.displayName();
        f.path = loc.displayPath();
        f.message = message;
        *retry = false;
        switch (job.askError(f)) {
        case ErrorAction::Abort:
            job.abort();
            return false;
        case ErrorAction::Retry:
            *retry = true;
            return true;
        default:
            job.logFail(f.path, f.message);
            return true;
        }
    }

    // loc, then what is below it. False means abort.
    bool apply(const Location& loc, GFileInfo* info, bool below)
    {
        if (!job.waitIfPaused())
            return false;
        p.currentName = loc.displayName();
        p.from = loc.displayPath();
        job.report(p);
        const bool link = g_file_info_get_file_type(info) == G_FILE_TYPE_SYMBOLIC_LINK;
        if (link && !below && job.change_.owner.isEmpty())
            job.logSkip(loc.displayPath());  // named by the user, and nothing done to it
        for (bool retry = true; retry;) {
            const QString message = set(loc, info, below);
            if (message.isEmpty())
                break;
            if (!refused(loc, message, &retry))
                return false;
        }
        ++p.itemsDone;
        job.report(p);
        if (!job.change_.recursive || !isDir(info))
            return true;

        // The folder first, as chmod -R does: a mode that opens it lets
        // the walk in, one that closes it is reported here.
        for (bool retry = true; retry;) {
            GError* error = nullptr;
            GPtr<GFileEnumerator> en(g_file_enumerate_children(loc.gfile(), kAttrs, kNoFollow, nullptr, &error));
            if (!en) {
                if (!refused(loc, takeMessage(error), &retry))
                    return false;
                continue;
            }
            for (;;) {
                if (job.isAborted())
                    return false;
                GPtr<GFileInfo> child(g_file_enumerator_next_file(en.get(), nullptr, nullptr));
                if (!child)
                    break;
                const Location at = loc.child(g_file_info_get_name(child.get()));
                if (isDir(child.get()) && deviceOf(child.get()) != deviceOf(info)) {
                    job.logSkip(at.displayPath());  // another filesystem starts here
                    continue;
                }
                if (!apply(at, child.get(), true))
                    return false;
            }
            break;
        }
        return true;
    }
};

AttributesJob::AttributesJob(std::vector<Location> items, AttributeChange change, QObject* parent)
    : Job(JobKind::Attributes, parent)
    , items_(std::move(items))
    , change_(change)
{
}

void AttributesJob::run()
{
    Impl impl{*this, {}};
    JobProgress& p = impl.p;

    std::vector<GPtr<GFileInfo>> infos;
    for (const Location& item : items_) {
        GError* error = nullptr;
        infos.emplace_back(g_file_query_info(item.gfile(), kAttrs, kNoFollow, nullptr, &error));
        if (infos.back()) {
            impl.count(item, infos.back().get());
        } else {
            ++p.itemsTotal;
            logFail(item.displayPath(), takeMessage(error));
        }
    }
    p.phase = JobProgress::Phase::Running;
    report(p, true);

    for (size_t i = 0; i < items_.size() && !isAborted(); ++i) {
        if (!infos[i])
            ++p.itemsDone;  // gone before the job got to it; logged above
        else if (!impl.apply(items_[i], infos[i].get(), false))
            break;
    }
    p.phase = JobProgress::Phase::Done;
    if (!isAborted())
        p.itemsDone = p.itemsTotal;
    p.currentName.clear();
    report(p, true);
}

} // namespace core
