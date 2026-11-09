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
};

// Finds the default handler for the file's content type and launches
// it. done() runs on the main thread with an empty message on success.
void openDefault(const Location& file, std::function<void(const QString& error)> done);

// Every app that can open the file, recommended ones first. Empty when
// nothing is registered for its content type.
void appsFor(const Location& file, std::function<void(std::vector<AppChoice>)> done);

// An app that runs in a terminal (Terminal=true) is started in the
// terminal runInTerminal() finds, in the folder of the first file: GLib
// would pick from a list of its own that knows neither $TERMINAL nor
// most Wayland terminals.
bool launchWith(const QString& appId, const std::vector<Location>& files, QString* error);

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
