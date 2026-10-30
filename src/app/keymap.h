// Every key the app reacts to goes through this table, and so do the
// function bar buttons and, later, the menu. Milestone 7 makes it
// remappable from config; until then the defaults are the table.
#pragma once

#include <QKeySequence>
#include <QString>

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
    MarkToggle,   // toggle the cursor row, stay
    MarkAll,
    InvertMarks,
    GoUp,
    Activate,  // enter the cursor folder
    Preview,
    ToggleSizes,  // per tab: recursive folder sizes and counts
};

Command lookup(const QKeySequence& key);
Command lookup(const QKeyEvent* event);
// Short label for the function bar; empty for None.
QString label(Command command);

} // namespace keymap
