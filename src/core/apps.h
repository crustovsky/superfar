// Applications and terminals through GAppInfo: the default app for a
// file, the list to choose from, launching, and running a shell command
// in a terminal in some folder.
#pragma once

#include "core/location.h"

#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

namespace core {

struct AppChoice {
    QString id;    // desktop file id, what launchWith() takes
    QString name;
    QStringList iconNames;
    bool isDefault = false;  // what Enter on the file opens it with
};

// Finds the default handler for the file's content type and launches
// it. done() runs on the main thread with an empty message on success.
void openDefault(const Location& file, std::function<void(const QString& error)> done);

// Every app that can open the file: the default for its content type
// first, then the recommended ones (the one used last leads them), then
// the rest. Empty when nothing is registered for the type; the type is
// empty when it could not be found out.
void appsFor(const Location& file,
             std::function<void(const QString& contentType, std::vector<AppChoice> apps)> done);

// What launchWith() does besides launching, for a known content type:
// the app becomes the one used last for it (so it leads the recommended
// ones next time), or the default (the user's mimeapps.list).
struct LaunchNote {
    QString contentType;
    bool makeDefault = false;
};

// An app that runs in a terminal (Terminal=true) is started in the
// terminal runInTerminal() finds, in the folder of the first file: GLib
// would pick from a list of its own that knows neither $TERMINAL nor
// most Wayland terminals.
bool launchWith(const QString& appId, const std::vector<Location>& files, QString* error,
                const LaunchNote& note = {});

// A command line typed by hand, run by sh in the folder of the first
// file with the files' paths appended, shell-quoted; in a terminal when
// asked. False when its program is not on PATH.
bool launchCommand(const QString& command, const std::vector<Location>& files, bool inTerminal, QString* error);

// Runs the shell command in a terminal, cwd set to dir. The terminal is
// xdg-terminal-exec when present, else $TERMINAL, else the first of a
// few common ones. With holdOpen the window stays until Enter is
// pressed, so output can be read.
bool runInTerminal(const Location& dir, const QString& shellCommand, bool holdOpen, QString* error);

// A desktop entry's Exec line as a shell command for these files: %f and
// %u are the first file, %F and %U all of them (paths, or URIs for what
// has no path), shell-quoted; %% is a percent sign; the other codes go.
// Files are appended when the line has no place for them.
QByteArray expandExec(const QByteArray& exec, const std::vector<Location>& files);

// What F4 edits with: $VISUAL, else $EDITOR, else vi.
QString editorCommand(QString* error = nullptr);

// Quotes raw name bytes for a POSIX shell.
QByteArray shellQuote(const QByteArray& arg);

} // namespace core
