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

namespace {

const char* const kDetailAttributes =
    "standard::type,standard::size,standard::content-type,standard::is-symlink,"
    "standard::symlink-target,time::modified,time::access,time::changed,"
    "owner::user,owner::group,unix::mode";

struct DetailsRequest {
    std::function<void(const Details&)> done;

    static void onInfo(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<DetailsRequest> req(static_cast<DetailsRequest*>(data));
        GError* error = nullptr;
        GPtr<GFileInfo> info(g_file_query_info_finish(G_FILE(source), result, &error));
        GErrorPtr err(error);
        Details d;
        if (info) {
            GFileInfo* i = info.get();
            d.ok = true;
            d.isDir = g_file_info_get_file_type(i) == G_FILE_TYPE_DIRECTORY;
            d.symlink = infoIsSymlink(i);
            if (d.symlink && g_file_info_has_attribute(i, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET))
                d.symlinkTarget = QString::fromUtf8(g_file_info_get_symlink_target(i));
            if (const char* t = g_file_info_has_attribute(i, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE)
                    ? g_file_info_get_content_type(i) : nullptr) {
                d.contentType = QString::fromUtf8(t);
                GStr desc(g_content_type_get_description(t));
                d.typeDescription = QString::fromUtf8(desc.get());
            }
            d.size = static_cast<quint64>(g_file_info_get_size(i));
            d.modified = static_cast<qint64>(g_file_info_get_attribute_uint64(i, G_FILE_ATTRIBUTE_TIME_MODIFIED));
            d.accessed = static_cast<qint64>(g_file_info_get_attribute_uint64(i, G_FILE_ATTRIBUTE_TIME_ACCESS));
            d.changed = static_cast<qint64>(g_file_info_get_attribute_uint64(i, G_FILE_ATTRIBUTE_TIME_CHANGED));
            d.mode = g_file_info_get_attribute_uint32(i, G_FILE_ATTRIBUTE_UNIX_MODE);
            d.owner = QString::fromUtf8(g_file_info_get_attribute_string(i, G_FILE_ATTRIBUTE_OWNER_USER));
            d.group = QString::fromUtf8(g_file_info_get_attribute_string(i, G_FILE_ATTRIBUTE_OWNER_GROUP));
        }
        req->done(d);
    }
};

} // namespace

bool isTextType(const QString& contentType)
{
    if (contentType.startsWith(QLatin1String("text/")))
        return true;
    const QString sub = contentType.section(QLatin1Char('/'), 1);
    return sub == QLatin1String("xml") || sub == QLatin1String("json") || sub.endsWith(QLatin1String("+xml"))
        || sub.endsWith(QLatin1String("+json")) || sub.contains(QLatin1String("script"))
        || sub == QLatin1String("x-empty");
}

void queryDetails(const Location& file, std::function<void(const Details&)> done)
{
    if (!file.isValid()) {
        done(Details{});
        return;
    }
    auto* req = new DetailsRequest{std::move(done)};
    g_file_query_info_async(file.gfile(), kDetailAttributes, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                            G_PRIORITY_DEFAULT, nullptr, &DetailsRequest::onInfo, req);
}

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
