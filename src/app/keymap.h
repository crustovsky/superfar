// Every key the app reacts to goes through this table, and so do the
// function bar buttons and, later, the menu. The defaults live here;
// the [keys] section of the config replaces bindings by command name.
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QKeyEvent;

namespace keymap {

enum class Command {
    None,
    Help,
    Rename,
    View,
    Edit,
    Copy,
    Move,
    Mkdir,
    Trash,
    Menu,
    Quit,
    NewFile,
    DeletePermanent,
    Find,
    HideLeft,
    HideRight,
    SwitchPane,
    FocusTree,
    NewTab,
    CloseTab,
    NextTab,
    PrevTab,
    ToggleHidden,
    Reload,
    Mark,         // toggle the cursor row and move down
    MarkUp,       // toggle the cursor row and move up
    MarkToggle,   // toggle the cursor row, stay
    MarkAll,
    InvertMarks,
    GoUp,
    Activate,  // enter the cursor folder
    Preview,
    ToggleSizes,  // per tab: recursive folder sizes and counts
    Cancel,       // Esc: clear the filter, close what is open
    MarkGlob,     // select by pattern
    UnmarkGlob,
    OpenWith,     // choose an app for the cursor file
    FocusCommandLine,
    CopyClip,     // clipboard, interoperable with other file managers
    CutClip,
    Paste,
    EditPath,     // Ctrl+L: type a path or URI
    GoTo,         // Ctrl+G: folder history by frecency
    LockTab,      // navigation in a locked tab opens a new tab
    Properties,   // Alt+Enter: the properties panel
    Bookmark,     // Ctrl+D: bookmark the current folder, or remove it
    ToggleThumbnails,  // per tab: thumbnails in taller rows
    MultiRename,       // Shift+F2: the multi-rename panel
    Sync,              // Shift+F5: folder sync between the two active tabs
};

Command lookup(const QKeySequence& key);
Command lookup(const QKeyEvent* event);
// Short label for the function bar; empty for None.
QString label(Command command);
// Stable names for the config file, and back.
QString name(Command command);
Command fromName(const QString& name);
// The default table as (name, portable key strings), in command order.
QList<QPair<QString, QStringList>> defaults();
// Current keys of a command, for the help and the function bar.
QList<QKeySequence> keysOf(Command command);
// Every command in table order, for the help.
QList<Command> allCommands();
// Replaces the bindings of the named commands; unknown names and
// unparsable sequences are returned as problems.
QStringList applyBindings(const QHash<QString, QStringList>& bindings);

} // namespace keymap
