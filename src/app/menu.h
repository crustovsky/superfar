// F9: the menu bar, hidden until asked for. Every entry is a keymap
// command with its current keys shown beside it; choosing one runs it
// exactly as the key would. No QAction shortcuts: the views own the keys.
#pragma once

#include "keymap.h"

#include <functional>

class QMenuBar;

namespace menu {

// Fills the bar with the menus; run is called with the chosen command.
void build(QMenuBar* bar, std::function<void(keymap::Command)> run);

} // namespace menu
