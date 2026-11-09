#include "core/apps.h"

#include "core/gobject.h"

#include <QFile>
#include <QList>

#include <gio/gdesktopappinfo.h>

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

// The terminal's command line up to where the program to run goes;
// empty when there is none.
QList<QByteArray> terminalArgv()
{
    if (GStr found(g_find_program_in_path("xdg-terminal-exec")); found)
        return {QByteArray(found.get())};
    if (const QByteArray term = qgetenv("TERMINAL"); !term.isEmpty())
        return {term, "-e"};
    for (const char* t : {"kitty", "foot", "alacritty", "wezterm", "ghostty", "konsole", "xterm"})
        if (GStr found(g_find_program_in_path(t)); found)
            return {QByteArray(found.get()), "-e"};
    return {};
}

bool shellInTerminal(const QByteArray& cwd, const QByteArray& command, QString* error)
{
    QList<QByteArray> argv = terminalArgv();
    if (argv.isEmpty()) {
        if (error)
            *error = QStringLiteral("No terminal found; set $TERMINAL");
        return false;
    }
    argv << "sh" << "-c" << command;
    std::vector<char*> cargv;
    for (QByteArray& a : argv)
        cargv.push_back(a.data());
    cargv.push_back(nullptr);
    GError* gerror = nullptr;
    const bool ok = g_spawn_async(cwd.isEmpty() ? nullptr : cwd.constData(), cargv.data(), nullptr,
                                  G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &gerror);
    GErrorPtr err(gerror);
    if (!ok && error)
        *error = errorMessage(err.get());
    return ok;
}

bool launchInTerminal(GAppInfo* app, const std::vector<Location>& files, QString* error)
{
    const char* line = g_app_info_get_commandline(app);
    if (!line || !*line) {
        if (error)
            *error = QStringLiteral("%1 has no command").arg(QString::fromUtf8(g_app_info_get_name(app)));
        return false;
    }
    const QByteArray exec(line);
    const QByteArray cwd = files.empty() ? QByteArray() : files.front().parent().path();
    // An app that takes one file gets a terminal per file, as GLib
    // starts one instance per file.
    const bool many = exec.contains("%F") || exec.contains("%U");
    if (!many && (exec.contains("%f") || exec.contains("%u")) && files.size() > 1) {
        for (const Location& file : files)
            if (!shellInTerminal(cwd, expandExec(exec, {file}), error))
                return false;
        return true;
    }
    return shellInTerminal(cwd, expandExec(exec, files), error);
}

bool launch(GAppInfo* app, const std::vector<Location>& files, QString* error)
{
    if (G_IS_DESKTOP_APP_INFO(app) && g_desktop_app_info_get_boolean(G_DESKTOP_APP_INFO(app), "Terminal"))
        return launchInTerminal(app, files, error);
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
        if (type == QLatin1String("inode/symlink")) {
            done(QStringLiteral("Broken link: its target does not exist"));
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

void appsFor(const Location& file,
             std::function<void(const QString& contentType, std::vector<AppChoice> apps)> done)
{
    queryContentType(file, [done = std::move(done)](const QString& type, const QString&) {
        std::vector<AppChoice> out;
        if (type.isEmpty()) {
            done(type, out);
            return;
        }
        auto add = [&](GAppInfo* app, bool isDefault) {
            const char* id = g_app_info_get_id(app);
            if (!id)
                return;
            const QString qid = QString::fromUtf8(id);
            for (const AppChoice& c : out)
                if (c.id == qid)
                    return;
            out.push_back({qid, QString::fromUtf8(g_app_info_get_display_name(app)), iconNamesOf(app), isDefault});
        };
        auto addAll = [&](GList* list) {
            for (GList* l = list; l; l = l->next)
                add(G_APP_INFO(l->data), false);
            g_list_free_full(list, g_object_unref);
        };
        const QByteArray utf8 = type.toUtf8();
        if (GPtr<GAppInfo> app(g_app_info_get_default_for_type(utf8.constData(), FALSE)); app)
            add(app.get(), true);
        addAll(g_app_info_get_recommended_for_type(utf8.constData()));
        addAll(g_app_info_get_all_for_type(utf8.constData()));
        done(type, out);
    });
}

bool launchWith(const QString& appId, const std::vector<Location>& files, QString* error, const LaunchNote& note)
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
    if (!launch(app.get(), files, error))
        return false;
    if (!note.contentType.isEmpty()) {
        // The app is running; a mimeapps.list that cannot be written is
        // worth a word, not a failure.
        const QByteArray type = note.contentType.toUtf8();
        GError* gerror = nullptr;
        const bool noted = note.makeDefault ? g_app_info_set_as_default_for_type(app.get(), type.constData(), &gerror)
                                            : g_app_info_set_as_last_used_for_type(app.get(), type.constData(), &gerror);
        GErrorPtr err(gerror);
        if (!noted && note.makeDefault && error)
            *error = QStringLiteral("Opened, but the default was not changed: %1").arg(errorMessage(err.get()));
    }
    return true;
}

bool launchCommand(const QString& command, const std::vector<Location>& files, bool inTerminal, QString* error)
{
    const QByteArray typed = command.trimmed().toLocal8Bit();
    // sh would start and fail unseen; say it here. A leading VAR=value
    // is the shell's business.
    const QByteArray program = typed.split(' ').first();
    if (!program.contains('=')) {
        if (GStr found(g_find_program_in_path(program.constData())); !found) {
            if (error)
                *error = QStringLiteral("%1 is not on PATH").arg(QString::fromLocal8Bit(program));
            return false;
        }
    }
    QByteArray line = typed;
    for (const Location& file : files) {
        const QByteArray path = file.path();
        line += ' ' + shellQuote(path.isEmpty() ? file.uri().toUtf8() : path);
    }
    const QByteArray cwd = files.empty() ? QByteArray() : files.front().parent().path();
    if (inTerminal)
        return shellInTerminal(cwd, line, error);
    QByteArray sh("sh"), dashC("-c");
    char* argv[] = {sh.data(), dashC.data(), line.data(), nullptr};
    GError* gerror = nullptr;
    const bool ok = g_spawn_async(cwd.isEmpty() ? nullptr : cwd.constData(), argv, nullptr,
                                  GSpawnFlags(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                                  nullptr, nullptr, nullptr, &gerror);
    GErrorPtr err(gerror);
    if (!ok && error)
        *error = errorMessage(err.get());
    return ok;
}

bool runInTerminal(const Location& dir, const QString& shellCommand, bool holdOpen, QString* error)
{
    QString command = shellCommand;
    if (holdOpen)
        command += QStringLiteral("; __s=$?; printf '\\n[exit %s] Enter to close' \"$__s\"; read __r");
    return shellInTerminal(dir.path(), command.toUtf8(), error);
}

QByteArray expandExec(const QByteArray& exec, const std::vector<Location>& files)
{
    auto quoted = [](const Location& file, bool uri) {
        const QByteArray path = file.path();
        return shellQuote(uri || path.isEmpty() ? file.uri().toUtf8() : path);
    };
    auto all = [&](bool uri) {
        QByteArray out;
        for (const Location& file : files)
            out += (out.isEmpty() ? "" : " ") + quoted(file, uri);
        return out;
    };
    QByteArray out;
    bool placed = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        if (exec[i] != '%' || i + 1 == exec.size()) {
            out += exec[i];
            continue;
        }
        const char code = exec[++i];
        switch (code) {
        case '%': out += '%'; break;
        case 'f':
        case 'u':
            if (!files.empty())
                out += quoted(files.front(), code == 'u');
            placed = true;
            break;
        case 'F':
        case 'U':
            out += all(code == 'U');
            placed = true;
            break;
        default: break;  // %i, %c, %k and the deprecated ones: nothing a terminal app needs
        }
    }
    if (!placed && !files.empty())
        out += ' ' + all(false);
    return out.trimmed();
}

QByteArray shellQuote(const QByteArray& arg)
{
    GStr quoted(g_shell_quote(arg.constData()));
    return QByteArray(quoted.get());
}

QString editorCommand(QString* error)
{
    // $VISUAL, then $EDITOR, then the usual suspects. A launcher started
    // by the compositor rarely carries the shell's exports, so the
    // fallback matters, and a terminal that opens on "vi: not found"
    // closes before anyone reads it.
    for (const char* var : {"VISUAL", "EDITOR"}) {
        const QByteArray v = qgetenv(var);
        if (v.isEmpty())
            continue;
        const QByteArray program = v.trimmed().split(' ').first();
        if (GStr found(g_find_program_in_path(program.constData())); found)
            return QString::fromLocal8Bit(v);
        if (error)
            *error = QStringLiteral("$%1 names %2, which is not on PATH").arg(QLatin1String(var), QString::fromLocal8Bit(program));
        return {};
    }
    for (const char* e : {"nvim", "vim", "nano", "vi"})
        if (GStr found(g_find_program_in_path(e)); found)
            return QString::fromLatin1(e);
    if (error)
        *error = QStringLiteral("No editor found: set $VISUAL or $EDITOR in the session environment");
    return {};
}

} // namespace core
