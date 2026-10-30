#include "core/fileops.h"

#include "core/gobject.h"

namespace core {

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
    std::function<void(const QString&, const QByteArray&)> done;

    static void onDone(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<RenameRequest> req(static_cast<RenameRequest*>(data));
        GError* error = nullptr;
        GPtr<GFile> renamed(g_file_set_display_name_finish(G_FILE(source), result, &error));
        GErrorPtr err(error);
        QByteArray name;
        if (renamed) {
            GStr base(g_file_get_basename(renamed.get()));
            name = QByteArray(base.get());
        }
        req->done(errorMessage(err.get()), name);
    }
};
} // namespace

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
                 std::function<void(const QString& error, const QByteArray& actualName)> done)
{
    if (!file.isValid()) {
        done(QStringLiteral("invalid location"), {});
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
