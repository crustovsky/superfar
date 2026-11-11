#include "menu.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPoint>

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
            {Command::Copy, "&Copy"}, {Command::Move, "&Move"}, {Command::Pack, "Pac&k into an archive"},
            {Command::Rename, "Re&name"},
            {Command::MultiRename, "Multi-rename"}, {Command::Mkdir, "New &folder"}, {Command::NewFile, "New f&ile"},
            {Command::Trash, "&Trash"}, {Command::DeletePermanent, "&Delete permanently"},
            {Command::None, ""},
            {Command::Undo, "&Undo the last operation"}, {Command::Redo, "Redo wh&at was undone"},
            {Command::None, ""},
            {Command::Quit, "&Quit"},
        }},
        {"&Select", {
            {Command::Mark, "Mark and move &down"}, {Command::MarkUp, "Mark and move &up"},
            {Command::MarkToggle, "&Mark"}, {Command::MarkAll, "Mark &all"}, {Command::InvertMarks, "&Invert marks"},
            {Command::ClearMarks, "C&lear marks"},
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
            {Command::ToggleThumbnails, "&Thumbnails"}, {Command::Columns, "Col&umns"},
            {Command::ToggleHighlight, "S&yntax colours"},
            {Command::None, ""},
            {Command::HideLeft, "&Left pane"}, {Command::HideRight, "&Right pane"},
            {Command::SwitchPane, "Switch &pane"}, {Command::ToggleTree, "Folder &tree"},
            {Command::FocusTree, "&Focus the tree"},
            {Command::FocusCommandLine, "&Command line"},
        }},
        {"&Tabs", {
            {Command::NewTab, "&New tab"}, {Command::CloseTab, "&Close tab"},
            {Command::NextTab, "Ne&xt tab"}, {Command::PrevTab, "&Previous tab"}, {Command::LockTab, "&Lock tab"},
        }},
        {"&Help", {
            {Command::Help, "&Keys"}, {Command::About, "&About"},
        }},
    };
    return g;
}

// An entry's words and, after a tab, its key: text there paints in the
// shortcut column without registering a shortcut. One key only: the
// menu sizes that column by its longest text and Qt adds the width
// once more, so a list of every key pushes the keys far from the words.
QString withKeys(const QString& text, keymap::Command command)
{
    const QKeySequence key = keymap::shownKey(command);
    return key.isEmpty() ? text : text + QLatin1Char('\t') + key.toString(QKeySequence::NativeText);
}

} // namespace

const std::vector<Entry>& contextEntries(Context context)
{
    using keymap::Command;
    static const std::vector<Entry> item = {
        {Command::Activate}, {Command::OpenWith}, {Command::View}, {Command::Edit}, {Command::Preview},
        {Command::None},
        {Command::Copy}, {Command::Move}, {Command::Rename}, {Command::MultiRename}, {Command::Pack},
        {Command::Trash}, {Command::DeletePermanent},
        {Command::None},
        {Command::CopyClip}, {Command::CutClip}, {Command::Paste},
        {Command::None},
        {Command::Properties},
    };
    static const std::vector<Entry> folder = {
        {Command::Mkdir}, {Command::NewFile}, {Command::Paste},
        {Command::None},
        {Command::Reload}, {Command::ToggleHidden}, {Command::ToggleSizes}, {Command::ToggleThumbnails},
        {Command::Columns},
        {Command::None},
        {Command::Find}, {Command::Bookmark},
    };
    static const std::vector<Entry> tabs = {
        {Command::NewTab}, {Command::CloseTab}, {Command::LockTab},
    };
    // A click in the tree has already taken the active tab to the
    // folder, so these act on it.
    static const std::vector<Entry> tree = {
        {Command::NewTab, "Open in a &new tab"}, {Command::Mkdir}, {Command::Find}, {Command::Bookmark},
        {Command::Reload},
    };
    static const std::vector<Entry> treeMount = {
        {Command::Trash, "&Unmount or eject"},
        {Command::None},
        {Command::NewTab, "Open in a &new tab"}, {Command::Find}, {Command::Reload},
    };
    switch (context) {
    case Context::Item: return item;
    case Context::Folder: return folder;
    case Context::Tabs: return tabs;
    case Context::Tree: return tree;
    case Context::TreeMount: return treeMount;
    }
    return folder;
}

QString entryText(const Entry& entry)
{
    if (entry.text)
        return QString::fromLatin1(entry.text);
    for (const Group& g : groups())
        for (const Item& item : g.items)
            if (item.command == entry.command)
                return QString::fromLatin1(item.text);
    return {};
}

void popup(QWidget* parent, Context context, const QString& title, const QPoint& globalPos,
           std::function<void(keymap::Command)> run)
{
    auto* m = new QMenu(parent);
    m->setAttribute(Qt::WA_DeleteOnClose);
    if (!title.isEmpty()) {
        m->addAction(title)->setEnabled(false);  // what the commands act on
        m->addSeparator();
    }
    for (const Entry& entry : contextEntries(context)) {
        if (entry.command == keymap::Command::None) {
            m->addSeparator();
            continue;
        }
        QAction* a = m->addAction(withKeys(entryText(entry), entry.command));
        a->setData(static_cast<int>(entry.command));
        // parent as the context: the choice outlives the menu.
        QObject::connect(a, &QAction::triggered, parent, [run, command = entry.command] { run(command); });
    }
    m->popup(globalPos);
}

void build(QMenuBar* bar, std::function<void(keymap::Command)> run)
{
    for (const Group& g : groups()) {
        QMenu* m = bar->addMenu(QString::fromLatin1(g.title));
        for (const Item& item : g.items) {
            if (item.command == keymap::Command::None) {
                m->addSeparator();
                continue;
            }
            QAction* a = m->addAction(withKeys(QString::fromLatin1(item.text), item.command));
            QObject::connect(a, &QAction::triggered, bar, [run, command = item.command] { run(command); });
        }
    }
}

} // namespace menu
