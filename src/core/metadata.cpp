#include "core/metadata.h"

#include "core/gobject.h"

#include <QPointer>

namespace core {

namespace {

const char* const kAttributes = "standard::size,time::modified,standard::fast-content-type";

} // namespace

struct MetadataQuery::Request {
    QPointer<MetadataQuery> owner;
    quint64 generation = 0;
    QByteArray name;

    static void onInfo(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<Request> req(static_cast<Request*>(data));
        GError* error = nullptr;
        GPtr<GFileInfo> info(g_file_query_info_finish(G_FILE(source), result, &error));
        GErrorPtr err(error);
        if (!req->owner || isCancelled(err.get()))
            return;

        Metadata m;
        if (info) {
            m.ok = true;
            m.size = static_cast<quint64>(g_file_info_get_size(info.get()));
            m.modified = static_cast<qint64>(
                g_file_info_get_attribute_uint64(info.get(), G_FILE_ATTRIBUTE_TIME_MODIFIED));
            const char* type = g_file_info_get_attribute_string(
                info.get(), G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
            if (type) {
                m.contentType = QString::fromUtf8(type);
                GPtr<GIcon> icon(g_content_type_get_icon(type));
                if (icon && G_IS_THEMED_ICON(icon.get())) {
                    const gchar* const* names = g_themed_icon_get_names(G_THEMED_ICON(icon.get()));
                    for (; names && *names; ++names)
                        m.iconNames.append(QString::fromUtf8(*names));
                }
            }
        }
        Q_EMIT req->owner->result(req->generation, req->name, m);
    }
};

MetadataQuery::MetadataQuery(QObject* parent)
    : QObject(parent)
    , cancellable_(g_cancellable_new())
{
}

MetadataQuery::~MetadataQuery()
{
    g_cancellable_cancel(cancellable_);
    g_object_unref(cancellable_);
}

void MetadataQuery::request(const Location& dir, const QByteArray& name, quint64 generation)
{
    const Location child = dir.child(name);
    if (!child.isValid())
        return;
    auto* req = new Request{this, generation, name};
    g_file_query_info_async(child.gfile(), kAttributes, G_FILE_QUERY_INFO_NONE, G_PRIORITY_LOW,
                            cancellable_, &Request::onInfo, req);
}

void MetadataQuery::cancelAll()
{
    g_cancellable_cancel(cancellable_);
    g_object_unref(cancellable_);
    cancellable_ = g_cancellable_new();
}

} // namespace core
