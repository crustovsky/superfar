// F9: the menu bar, hidden until asked for, and the right-click menus.
// Every entry is a keymap command with its current keys shown beside
// it; choosing one runs it exactly as the key would. No QAction
// shortcuts: the views own the keys.
#pragma once

#include "keymap.h"

#include <QString>
#include <functional>
#include <vector>

class QMenuBar;
class QPoint;
class QWidget;

namespace menu {

// Fills the bar with the menus; run is called with the chosen command.
void build(QMenuBar* bar, std::function<void(keymap::Command)> run);

// What a right click was on.
enum class Context {
    Item,       // a row of the list: the marked items, or that row
    Folder,     // the list's empty space: the folder itself
    Tabs,       // a tab of the strip
    Tree,       // a folder of the tree
    TreeMount,  // a mount or volume of the tree
};
// One entry: None is a separator; text, when set, replaces the menu
// bar's wording where the same command means something else here.
struct Entry {
    keymap::Command command;
    const char* text = nullptr;
};
const std::vector<Entry>& contextEntries(Context context);
// The words an entry shows, without its keys.
QString entryText(const Entry& entry);
// Shows the context's menu at a global position; it deletes itself when
// it closes. title, when given, is a first line saying what the
// commands will act on.
void popup(QWidget* parent, Context context, const QString& title, const QPoint& globalPos,
           std::function<void(keymap::Command)> run);

} // namespace menu
