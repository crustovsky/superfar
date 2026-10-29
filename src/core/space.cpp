#include "core/space.h"

#include "core/gobject.h"

#include <QPointer>

namespace core {

struct SpaceQuery::Request {
    QPointer<SpaceQuery> owner;
    quint64 generation = 0;

    static void onInfo(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Request> req(static_cast<Request*>(data));
        GError* error = nullptr;
        GPtr<GFileInfo> info(g_file_query_filesystem_info_finish(G_FILE(source), result, &error));
        GErrorPtr err(error);
        if (!req->owner)
            return;
        const bool ok = info && g_file_info_has_attribute(info.get(), G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
        const quint64 free = ok ? g_file_info_get_attribute_uint64(info.get(), G_FILE_ATTRIBUTE_FILESYSTEM_FREE) : 0;
        Q_EMIT req->owner->result(req->generation, ok, free);
    }
};

SpaceQuery::SpaceQuery(QObject* parent)
    : QObject(parent)
{
}

void SpaceQuery::request(const Location& dir, quint64 generation)
{
    if (!dir.isValid())
        return;
    auto* req = new Request{this, generation};
    g_file_query_filesystem_info_async(dir.gfile(), G_FILE_ATTRIBUTE_FILESYSTEM_FREE, G_PRIORITY_LOW,
                                       nullptr, &Request::onInfo, req);
}

} // namespace core
