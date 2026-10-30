#include "menu.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>

namespace menu {

namespace {

struct Item {
    keymap::Command command;
    const char* text;  // empty: a separator
};

struct Group {
    const char* title;
    std::vector<Item> items;
};

const std::vector<Group>& groups()
{
    using keymap::Command;
    static const std::vector<Group> g = {
        {"&File", {
            {Command::Activate, "&Open"}, {Command::OpenWith, "Open &with"}, {Command::View, "&View"},
            {Command::Edit, "&Edit"}, {Command::Preview, "&Preview"}, {Command::Properties, "P&roperties"},
            {Command::None, ""},
            {Command::Copy, "&Copy"}, {Command::Move, "&Move"}, {Command::Rename, "Re&name"},
            {Command::MultiRename, "Multi-rename"}, {Command::Mkdir, "New &folder"}, {Command::NewFile, "New f&ile"},
            {Command::Trash, "&Trash"}, {Command::DeletePermanent, "&Delete permanently"},
            {Command::None, ""},
            {Command::Quit, "&Quit"},
        }},
        {"&Select", {
            {Command::Mark, "Mark and move &down"}, {Command::MarkUp, "Mark and move &up"},
            {Command::MarkToggle, "&Mark"}, {Command::MarkAll, "Mark &all"}, {Command::InvertMarks, "&Invert marks"},
            {Command::MarkGlob, "Mark by &pattern"}, {Command::UnmarkGlob, "Unmark by pa&ttern"},
            {Command::None, ""},
            {Command::CopyClip, "&Copy to clipboard"}, {Command::CutClip, "Cu&t to clipboard"},
            {Command::Paste, "&Paste"},
        }},
        {"&Go", {
            {Command::GoUp, "&Up"}, {Command::EditPath, "&Path"}, {Command::GoTo, "&Go to"},
            {Command::Find, "&Find in tree"}, {Command::Bookmark, "&Bookmark this folder"},
            {Command::Sync, "&Sync with the other pane"}, {Command::Reload, "&Reload"},
        }},
        {"&View", {
            {Command::ToggleHidden, "&Hidden files"}, {Command::ToggleSizes, "Recursive &sizes"},
            {Command::ToggleThumbnails, "&Thumbnails"},
            {Command::None, ""},
            {Command::HideLeft, "&Left pane"}, {Command::HideRight, "&Right pane"},
            {Command::SwitchPane, "Switch &pane"}, {Command::FocusTree, "Folder &tree"},
            {Command::FocusCommandLine, "&Command line"},
        }},
        {"&Tabs", {
            {Command::NewTab, "&New tab"}, {Command::CloseTab, "&Close tab"},
            {Command::NextTab, "Ne&xt tab"}, {Command::PrevTab, "&Previous tab"}, {Command::LockTab, "&Lock tab"},
        }},
        {"&Help", {
            {Command::Help, "&Keys"},
        }},
    };
    return g;
}

} // namespace

void build(QMenuBar* bar, std::function<void(keymap::Command)> run)
{
    for (const Group& g : groups()) {
        QMenu* m = bar->addMenu(QString::fromLatin1(g.title));
        for (const Item& item : g.items) {
            if (item.command == keymap::Command::None) {
                m->addSeparator();
                continue;
            }
            QStringList keys;
            for (const QKeySequence& k : keymap::keysOf(item.command))
                keys << k.toString(QKeySequence::NativeText);
            // Text after a tab paints in the shortcut column without
            // registering a shortcut.
            QAction* a = m->addAction(QString::fromLatin1(item.text) + QLatin1Char('\t') + keys.join(QStringLiteral(", ")));
            QObject::connect(a, &QAction::triggered, bar, [run, command = item.command] { run(command); });
        }
    }
}

} // namespace menu
