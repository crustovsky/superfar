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
} // namespace

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
