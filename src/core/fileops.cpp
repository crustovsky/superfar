#include "core/fileops.h"

#include "core/gobject.h"

namespace core {

Location uniqueSibling(const Location& file)
{
    const QByteArray name = file.basename();
    const Location parent = file.parent();
    int dot = name.lastIndexOf('.');
    if (dot <= 0)
        dot = name.size();
    const QByteArray stem = name.left(dot);
    const QByteArray ext = name.mid(dot);
    for (int n = 2; n < 10000; ++n) {
        const Location candidate = parent.child(stem + " (" + QByteArray::number(n) + ")" + ext);
        if (!g_file_query_exists(candidate.gfile(), nullptr))
            return candidate;
    }
    return file;
}

namespace {
struct MkdirRequest {
    std::function<void(const QString&)> done;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<MkdirRequest> req(static_cast<MkdirRequest*>(data));
        GError* error = nullptr;
        g_file_make_directory_finish(G_FILE(source), result, &error);
        GErrorPtr err(error);
        req->done(errorMessage(err.get()));
    }
};
struct CreateRequest {
    std::function<void(const QString&)> done;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<CreateRequest> req(static_cast<CreateRequest*>(data));
        GError* error = nullptr;
        GPtr<GFileOutputStream> out(g_file_create_finish(G_FILE(source), result, &error));
        GErrorPtr err(error);
        if (out)
            g_output_stream_close(G_OUTPUT_STREAM(out.get()), nullptr, nullptr);
        req->done(errorMessage(err.get()));
    }
};

struct RenameRequest {
    std::function<void(const QString&, const QByteArray&, const QString&)> done;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<RenameRequest> req(static_cast<RenameRequest*>(data));
        GError* error = nullptr;
        GPtr<GFile> renamed(g_file_set_display_name_finish(G_FILE(source), result, &error));
        GErrorPtr err(error);
        QByteArray name;
        QString display;
        if (renamed) {
            GStr base(g_file_get_basename(renamed.get()));
            name = QByteArray(base.get());
            GStr shown(g_filename_display_name(base.get()));
            display = QString::fromUtf8(shown.get());
        }
        req->done(errorMessage(err.get()), name, display);
    }
};
struct ModeRequest {
    std::function<void(const QString&)> done;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<ModeRequest> req(static_cast<ModeRequest*>(data));
        GError* error = nullptr;
        g_file_set_attributes_finish(G_FILE(source), result, nullptr, &error);
        GErrorPtr err(error);
        req->done(errorMessage(err.get()));
    }
};
} // namespace

void setMode(const Location& file, quint32 mode, std::function<void(const QString& error)> done)
{
    if (!file.isValid()) {
        done(QStringLiteral("invalid location"));
        return;
    }
    GPtr<GFileInfo> info(g_file_info_new());
    g_file_info_set_attribute_uint32(info.get(), G_FILE_ATTRIBUTE_UNIX_MODE, mode);
    auto* req = new ModeRequest{std::move(done)};
    g_file_set_attributes_async(file.gfile(), info.get(), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, G_PRIORITY_DEFAULT,
                                nullptr, &ModeRequest::onDone, req);
}

void createFile(const Location& file, std::function<void(const QString& error)> done)
{
    if (!file.isValid()) {
        done(QStringLiteral("invalid location"));
        return;
    }
    auto* req = new CreateRequest{std::move(done)};
    g_file_create_async(file.gfile(), G_FILE_CREATE_NONE, G_PRIORITY_DEFAULT, nullptr, &CreateRequest::onDone, req);
}

void renameEntry(const Location& file, const QString& newName,
                 std::function<void(const QString& error, const QByteArray& actualName,
                                    const QString& display)> done)
{
    if (!file.isValid()) {
        done(QStringLiteral("invalid location"), {}, {});
        return;
    }
    auto* req = new RenameRequest{std::move(done)};
    g_file_set_display_name_async(file.gfile(), newName.toUtf8().constData(), G_PRIORITY_DEFAULT, nullptr,
                                  &RenameRequest::onDone, req);
}

void makeDirectory(const Location& dir, std::function<void(const QString& error)> done)
{
    if (!dir.isValid()) {
        done(QStringLiteral("invalid location"));
        return;
    }
    auto* req = new MkdirRequest{std::move(done)};
    g_file_make_directory_async(dir.gfile(), G_PRIORITY_DEFAULT, nullptr, &MkdirRequest::onDone, req);
}

} // namespace core
