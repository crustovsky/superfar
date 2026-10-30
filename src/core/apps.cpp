#include "core/apps.h"

#include "core/gobject.h"

#include <QFile>

namespace core {

namespace {

const char* const kContentType = "standard::content-type";

QStringList iconNamesOf(GAppInfo* app)
{
    QStringList names;
    GIcon* icon = g_app_info_get_icon(app);  // borrowed
    if (icon && G_IS_THEMED_ICON(icon)) {
        const gchar* const* n = g_themed_icon_get_names(G_THEMED_ICON(icon));
        for (; n && *n; ++n)
            names.append(QString::fromUtf8(*n));
    }
    return names;
}

GList* fileList(const std::vector<Location>& files)
{
    GList* list = nullptr;
    for (const Location& f : files)
        list = g_list_append(list, f.gfile());
    return list;
}

struct TypeRequest {
    std::function<void(const QString& contentType, const QString& error)> done;

    static void onInfo(GObject* source, GAsyncResult* result, gpointer data)
    {
        std::unique_ptr<TypeRequest> req(static_cast<TypeRequest*>(data));
        GError* error = nullptr;
        GPtr<GFileInfo> info(g_file_query_info_finish(G_FILE(source), result, &error));
        GErrorPtr err(error);
        if (!info) {
            req->done(QString(), errorMessage(err.get()));
            return;
        }
        const char* type = g_file_info_get_content_type(info.get());
        req->done(type ? QString::fromUtf8(type) : QString(), QString());
    }
};

void queryContentType(const Location& file,
                      std::function<void(const QString& contentType, const QString& error)> done)
{
    auto* req = new TypeRequest{std::move(done)};
    g_file_query_info_async(file.gfile(), kContentType, G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
                            nullptr, &TypeRequest::onInfo, req);
}

bool launch(GAppInfo* app, const std::vector<Location>& files, QString* error)
{
    GList* list = fileList(files);
    GError* gerror = nullptr;
    const bool ok = g_app_info_launch(app, list, nullptr, &gerror);
    g_list_free(list);
    GErrorPtr err(gerror);
    if (!ok && error)
        *error = errorMessage(err.get());
    return ok;
}

} // namespace

void openDefault(const Location& file, std::function<void(const QString& error)> done)
{
    queryContentType(file, [file, done = std::move(done)](const QString& type, const QString& error) {
        if (!error.isEmpty()) {
            done(error);
            return;
        }
        GPtr<GAppInfo> app(g_app_info_get_default_for_type(type.toUtf8().constData(), !file.isNative()));
        if (!app) {
            done(QStringLiteral("No application for %1").arg(type));
            return;
        }
        QString launchError;
        launch(app.get(), {file}, &launchError);
        done(launchError);
    });
}

void appsFor(const Location& file, std::function<void(std::vector<AppChoice>)> done)
{
    queryContentType(file, [done = std::move(done)](const QString& type, const QString&) {
        std::vector<AppChoice> out;
        if (type.isEmpty()) {
            done(out);
            return;
        }
        auto add = [&](GList* list) {
            for (GList* l = list; l; l = l->next) {
                auto* app = G_APP_INFO(l->data);
                const char* id = g_app_info_get_id(app);
                if (!id)
                    continue;
                const QString qid = QString::fromUtf8(id);
                bool seen = false;
                for (const AppChoice& c : out)
                    seen = seen || c.id == qid;
                if (!seen)
                    out.push_back({qid, QString::fromUtf8(g_app_info_get_display_name(app)), iconNamesOf(app)});
            }
            g_list_free_full(list, g_object_unref);
        };
        add(g_app_info_get_recommended_for_type(type.toUtf8().constData()));
        add(g_app_info_get_all_for_type(type.toUtf8().constData()));
        done(out);
    });
}

bool launchWith(const QString& appId, const std::vector<Location>& files, QString* error)
{
    GList* all = g_app_info_get_all();
    GAppInfo* found = nullptr;
    for (GList* l = all; l; l = l->next) {
        const char* id = g_app_info_get_id(G_APP_INFO(l->data));
        if (id && appId == QString::fromUtf8(id)) {
            found = G_APP_INFO(g_object_ref(l->data));
            break;
        }
    }
    g_list_free_full(all, g_object_unref);
    GPtr<GAppInfo> app(found);
    if (!app) {
        if (error)
            *error = QStringLiteral("Application %1 not found").arg(appId);
        return false;
    }
    return launch(app.get(), files, error);
}

bool runInTerminal(const Location& dir, const QString& shellCommand, bool holdOpen, QString* error)
{
    QString command = shellCommand;
    if (holdOpen)
        command += QStringLiteral("; __s=$?; printf '\\n[exit %s] Enter to close' \"$__s\"; read __r");

    QList<QByteArray> argv;
    if (GStr found(g_find_program_in_path("xdg-terminal-exec")); found) {
        argv << QByteArray(found.get());
    } else if (const QByteArray term = qgetenv("TERMINAL"); !term.isEmpty()) {
        argv << term << "-e";
    } else {
        for (const char* t : {"kitty", "foot", "alacritty", "wezterm", "ghostty", "konsole", "xterm"}) {
            if (GStr found(g_find_program_in_path(t)); found) {
                argv << QByteArray(found.get()) << "-e";
                break;
            }
        }
    }
    if (argv.isEmpty()) {
        if (error)
            *error = QStringLiteral("No terminal found; set $TERMINAL");
        return false;
    }
    argv << "sh" << "-c" << command.toUtf8();

    std::vector<char*> cargv;
    for (QByteArray& a : argv)
        cargv.push_back(a.data());
    cargv.push_back(nullptr);
    const QByteArray cwd = dir.path();
    GError* gerror = nullptr;
    const bool ok = g_spawn_async(cwd.isEmpty() ? nullptr : cwd.constData(), cargv.data(), nullptr,
                                  G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &gerror);
    GErrorPtr err(gerror);
    if (!ok && error)
        *error = errorMessage(err.get());
    return ok;
}

QByteArray shellQuote(const QByteArray& arg)
{
    GStr quoted(g_shell_quote(arg.constData()));
    return QByteArray(quoted.get());
}

QString editorCommand()
{
    for (const char* var : {"VISUAL", "EDITOR"})
        if (const QByteArray v = qgetenv(var); !v.isEmpty())
            return QString::fromLocal8Bit(v);
    return QStringLiteral("vi");
}

} // namespace core
