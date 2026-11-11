#include "help.h"

#include "keymap.h"

#include <QCoreApplication>
#include <QStringList>

namespace help {

namespace {

QString describe(keymap::Command c)
{
    using keymap::Command;
    switch (c) {
    case Command::Help: return QStringLiteral("this help");
    case Command::Rename: return QStringLiteral("rename inline; Up and Down move to the next row, Tab to the row's permissions and owner when shown, Enter applies");
    case Command::View: return QStringLiteral("view a file (windowed, hex for binaries); on a folder compute its size");
    case Command::Edit: return QStringLiteral("edit in $VISUAL or $EDITOR in a terminal");
    case Command::Copy: return QStringLiteral("copy the marked items (or the cursor) to the other pane; inside an archive: extract them. While a job runs, Shift+Enter in a job's prompt queues the new one behind it");
    case Command::Move: return QStringLiteral("move the marked items (or the cursor) to the other pane");
    case Command::Mkdir: return QStringLiteral("new folder");
    case Command::Trash: return QStringLiteral("move to trash; on a mount in the tree: unmount");
    case Command::Menu: return QStringLiteral("show the menu bar; Esc or a choice hides it again");
    case Command::Quit: return QStringLiteral("quit, keeping the session");
    case Command::NewFile: return QStringLiteral("new empty file");
    case Command::DeletePermanent: return QStringLiteral("delete permanently");
    case Command::Find: return QStringLiteral("find in this folder and below by name; results open in a tab");
    case Command::HideLeft: return QStringLiteral("hide or show the left pane");
    case Command::HideRight: return QStringLiteral("hide or show the right pane");
    case Command::SwitchPane: return QStringLiteral("switch panes");
    case Command::FocusTree: return QStringLiteral("focus the folder tree, showing it first if hidden");
    case Command::NewTab: return QStringLiteral("new tab in the same folder");
    case Command::CloseTab: return QStringLiteral("close the tab");
    case Command::NextTab: return QStringLiteral("next tab");
    case Command::PrevTab: return QStringLiteral("previous tab");
    case Command::ToggleHidden: return QStringLiteral("show or hide hidden files");
    case Command::Reload: return QStringLiteral("reload the folder");
    case Command::Mark: return QStringLiteral("mark and move down");
    case Command::MarkUp: return QStringLiteral("mark and move up");
    case Command::MarkToggle: return QStringLiteral("mark (when no filter is set)");
    case Command::MarkAll: return QStringLiteral("mark all");
    case Command::InvertMarks: return QStringLiteral("invert marks");
    case Command::ClearMarks: return QStringLiteral("clear the marks (they come back with the folder after a round trip)");
    case Command::GoUp: return QStringLiteral("go up; with a filter set, edit the filter");
    case Command::Activate: return QStringLiteral("enter the folder, open the file with its default app, or open an archive as a folder");
    case Command::Preview: return QStringLiteral("preview of the cursor item in the other pane, follows the cursor");
    case Command::ToggleSizes: return QStringLiteral("recursive folder sizes and counts for this tab");
    case Command::Cancel: return QStringLiteral("clear the filter, close the viewer or panel, close the preview, then clear the marks");
    case Command::MarkGlob: return QStringLiteral("mark by pattern");
    case Command::UnmarkGlob: return QStringLiteral("unmark by pattern");
    case Command::OpenWith: return QStringLiteral("open the marked items with a chosen app, or a typed command");
    case Command::FocusCommandLine: return QStringLiteral("command line: runs in this folder in a terminal; Ctrl+Enter inserts the cursor name");
    case Command::CopyClip: return QStringLiteral("copy to the clipboard (works with other file managers)");
    case Command::CutClip: return QStringLiteral("cut to the clipboard");
    case Command::Paste: return QStringLiteral("paste into this folder");
    case Command::EditPath: return QStringLiteral("type a path or URI; remote ones are mounted");
    case Command::GoTo: return QStringLiteral("go to a folder from history, ranked by frecency");
    case Command::LockTab: return QStringLiteral("lock the tab: navigation opens a new tab");
    case Command::Properties: return QStringLiteral("properties of the marked items or the cursor item; change their permissions and owner, down the folders if asked");
    case Command::Bookmark: return QStringLiteral("bookmark this folder (or remove the bookmark); bookmarks are tree roots");
    case Command::ToggleThumbnails: return QStringLiteral("thumbnails in this tab (from tumbler over D-Bus)");
    case Command::MultiRename: return QStringLiteral("rename the marked items by pattern, with preview and presets");
    case Command::Sync: return QStringLiteral("compare this folder with the other pane's and copy or delete the differences");
    case Command::About: return QStringLiteral("name, version and build date");
    case Command::ToggleTree: return QStringLiteral("hide or show the folder tree; hidden, it does no work");
    case Command::Pack: return QStringLiteral("pack the marked items (or the cursor) into a new archive: zip, tar.gz, tar.xz, tar.zst or 7z");
    case Command::PreviewFirst: return QStringLiteral("previewed PDF or video: first page, or the start (the list keeps these keys otherwise)");
    case Command::PreviewPrev: return QStringLiteral("previewed PDF or video: previous page, or back 10 seconds");
    case Command::PreviewNext: return QStringLiteral("previewed PDF or video: next page, or forward 10 seconds");
    case Command::PreviewLast: return QStringLiteral("previewed PDF or video: last page, or the end");
    case Command::PreviewPlay: return QStringLiteral("previewed video: play or pause (no sound)");
    case Command::Columns: return QStringLiteral("the pane's columns as a list: Space shows or hides one, Shift+Up and Shift+Down move it");
    case Command::Undo: return QStringLiteral("undo the last copy, move, trash, rename, new folder or file, extract, pack or attribute change; what it created goes to the trash, never deleted");
    case Command::Redo: return QStringLiteral("redo what was undone");
    case Command::ToggleHighlight: return QStringLiteral("syntax colours in the viewer and the preview, on or off; they come from bat when it is installed, in the theme bat is set to");
    case Command::None: return {};
    }
    return {};
}

} // namespace

QString about()
{
    QStringList lines;
    lines << QStringLiteral("superfar %1, built %2").arg(QStringLiteral(SUPERFAR_VERSION), QStringLiteral(SUPERFAR_BUILD_DATE));
    lines << QString();
    // Short lines: the viewer does not wrap and the pane is half a window.
    lines << QCoreApplication::translate("help", "A dual-pane file manager for Linux in the Directory Opus");
    lines << QCoreApplication::translate("help", "mould, at Thunar's weight, with FAR-style function keys.");
    lines << QCoreApplication::translate("help", "Keyboard first, one window, and it never defines a colour:");
    lines << QCoreApplication::translate("help", "whatever your platform theme says is what it looks like.");
    lines << QString();
    lines << QStringLiteral("This is my file manager. There are many like it,");
    lines << QStringLiteral("but this one is mine.");
    lines << QString();
    lines << QStringLiteral("My file manager is my best friend. Without me,");
    lines << QStringLiteral("my file manager is useless. Without my file manager,");
    lines << QStringLiteral("I am ls.");
    lines << QString();
    lines << QStringLiteral("https://github.com/crustovsky/superfar");
    lines << QString();
    lines << QCoreApplication::translate("help", "F1 lists the keys; the bar at the bottom shows the");
    lines << QCoreApplication::translate("help", "function keys. Esc closes this.");
    return lines.join(QLatin1Char('\n'));
}

QString text()
{
    // The same header as the first start, then the keys.
    QStringList lines;
    lines << about();
    lines << QString();
    lines << QCoreApplication::translate("help", "Keys (change them in ~/.config/superfar/superfar.ini, section [keys];");
    lines << QCoreApplication::translate("help", "a line with several keys is quoted: GoUp=\"Backspace;Left\")");
    lines << QString();
    int width = 0;
    QList<QPair<QString, QString>> rows;
    for (keymap::Command c : keymap::allCommands()) {
        QStringList keys;
        for (const QKeySequence& k : keymap::keysOf(c))
            keys << k.toString(QKeySequence::NativeText);
        const QString key = keys.isEmpty() ? QStringLiteral("(unbound)") : keys.join(QStringLiteral(", "));
        width = std::max<int>(width, static_cast<int>(key.size()));
        rows.append({key, describe(c)});
    }
    for (const auto& [key, what] : rows)
        lines << key.leftJustified(width + 2) + what;
    lines << QString();
    lines << QCoreApplication::translate("help", "Type to filter the list; Backspace edits the filter, Esc clears it.");
    lines << QCoreApplication::translate("help", "Click a column header to sort by it; click again to reverse. Folders stay first.");
    lines << QCoreApplication::translate("help", "Drag a header to move its column, its edge to resize it; a right click lists the columns (Ctrl+F6 from the keys).");
    lines << QCoreApplication::translate("help", "Drag files between panes, onto folders and to other apps; drops accept files from them.");
    lines << QCoreApplication::translate("help", "A right click on an item, the empty list, a tab or the tree lists what the keys do there.");
    return lines.join(QLatin1Char('\n'));
}

} // namespace help
