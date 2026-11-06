# superfar

A dual-pane file manager for Linux in the Directory Opus mould, at
Thunar's weight, with FAR-style function keys. Keyboard first, one
window, and it never defines a colour: whatever your platform theme
says is what it looks like.

> This is my file manager. There are many like it, but this one is mine.
>
> My file manager is my best friend. Without me, my file manager is
> useless. Without my file manager, I am `ls`.

![Dual pane with folder counts](docs/screenshots/dual-pane.png)

## Why

Thunar is light but slow to improve, and Linux has had no dual-pane
manager that feels native rather than borrowed from GNOME or KDE.
superfar keeps the Opus workflow (tree, two panes, tabs, preview,
folder sizes) and the Thunar footprint, and puts every action on a key
you can see in the bar at the bottom.

## Features

| | What you get | Where |
|---|---|---|
| **Two panes** | Always open, each with its own path bar, tabs and status line; the active one is unmistakable. | Tab, Ctrl+F1, Ctrl+F2 |
| **Tabs** | A tab owns its folder, history, sort, filter, marks and cursor. Tabs come back on start and can be locked. | Ctrl+T, Ctrl+W, Ctrl+Shift+L |
| **Folder tree** | One tree that follows the active tab: the filesystem, the trash, your GTK bookmarks, mounts and the network. | Shift+Tab, Ctrl+F5 |
| **Function key bar** | F1 to F10 with what they do, relabelled while Shift, Ctrl or Alt is held. The menu bar stays out of the way until F9. | always on screen |
| **Quick filter and marks** | Just type to filter. Marks are separate from the cursor and come back when you return to a folder. | typing, Insert, Space, Ctrl+A |
| **Columns** | Size, Items, Modified, Permissions, Owner. Drag a header to reorder, right click it to choose; each pane keeps its own. Size, count and date are never cut short. | the header, `Columns` |
| **Permissions and owner** | `rwxr-xr-x` and `user:group` in the list as `ls -l` has them, sortable, edited in the row. For many items at once, and down their folders, in the properties panel. | F2 then Tab, Alt+Enter |
| **Folder counts and sizes** | Item counts by default; recursive sizes and counts per tab, with partial totals while the walk runs. | Ctrl+F3, F3 on a folder |
| **Copy, move, trash** | A job strip inside the window: progress for items and bytes, a throughput graph, Pause, Skip and Abort, conflicts and errors as bars, and an unattended mode that logs instead of asking. | F5, F6, F8 |
| **Preview** | Follows the cursor in the other pane or a third slot: images, text, PDF pages, video, an archive's summary and tree, a folder's totals. | Ctrl+Q |
| **Viewer** | Any file of any size, read in windows; hex for binaries. | F3 |
| **Archives** | Open as read-only folders, extract with F5, pack the marked items into zip, tar.gz, tar.xz, tar.zst or 7z. | Enter, F5, Alt+F5 |
| **Multi-rename** | Wildcards, regex, find and replace, numbering, case, presets and undo, with a live preview that calls out clashes. | Shift+F2 |
| **Folder sync** | Compares the two panes by size, time and, if asked, SHA-256; copies or trashes what you check. | Shift+F5 |
| **Find and go** | Find in tree with the results in a tab, folder history ranked by frecency, a path or a remote URI typed in. | Alt+F7, Ctrl+G, Ctrl+L |
| **Thumbnails** | From tumbler over D-Bus, in taller rows, per tab. | Ctrl+F4 |
| **Remote and removable** | sftp, smb and the rest through GVfs. Mounting asks for credentials inside the window; F8 on a mount unmounts or ejects it. | Ctrl+L, the tree |
| **Plays well with others** | Clipboard and drag and drop speak the formats other file managers do; the command line runs in the tab's folder in your terminal. | Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+Space |
| **Nothing pops up** | Progress, questions, passwords, the open-with list and errors are bars inside the one window. | |
| **Your theme** | No colour of its own: what the platform theme says (qt6ct, Kvantum, Fusion) is what it looks like. | |
| **Light** | One process, no indexer, no daemon. About 5 MB of its own memory with an empty window, under 50 MB with a 100,000-entry folder open. | |

Nothing decodes media or documents inside the process: images go
through libglycin's sandbox, PDF pages through pdftoppm and video
frames through ffmpeg in child processes (video plays without sound),
archives through bsdtar, thumbnails through tumbler. In a previewed PDF
or video, Ctrl+Home, Ctrl+PgUp, Ctrl+PgDown and Ctrl+End page or seek
and Ctrl+P plays and pauses; the strip under the details has the same
buttons and a playhead.

## Screenshots

Recursive folder sizes, computed in the background:

![Folder sizes](docs/screenshots/folder-sizes.png)

Permissions and owner as columns. F2 opens the name, Tab moves to the
row's mode, which takes `rwxr-xr-x` or `0755`:

![Permissions](docs/screenshots/permissions.png)

Properties on Alt+Enter over several marked items: a bit they differ in
is half checked and stays as each item has it, and Apply changes only
what you changed:

![Properties](docs/screenshots/properties.png)

The column list, from the `Columns` command or as a menu from a right
click on the header:

![Columns](docs/screenshots/columns.png)

The preview following the cursor:

![Preview](docs/screenshots/preview.png)

A PDF in the preview, paged with Ctrl+PgUp and Ctrl+PgDown or the
buttons; pdftoppm renders each page in a child process:

![PDF pages](docs/screenshots/pdf-pages.png)

A video in the preview, playing after Ctrl+P, without sound, decoded by
ffmpeg in a child process; the same keys seek:

![Video](docs/screenshots/video.png)

The F3 viewer on a binary file:

![Viewer](docs/screenshots/viewer.png)

Folder history on Ctrl+G, ranked by frecency:

![Go to](docs/screenshots/go-to.png)

Multi-rename on Shift+F2, with the clashes called out before anything
is renamed:

![Multi-rename](docs/screenshots/multi-rename.png)

Folder sync on Shift+F5, comparing the two panes:

![Folder sync](docs/screenshots/folder-sync.png)

An archive opened as a folder, with its tab named after it:

![Archive](docs/screenshots/archive.png)

Find in tree on Alt+F7, results in a tab:

![Find](docs/screenshots/find.png)

Thumbnails on Ctrl+F4, from tumbler through the freedesktop cache:

![Thumbnails](docs/screenshots/thumbnails.png)

## Keys

F1 shows the full table inside the window, with whatever you have
remapped. The defaults:

| Key | Action |
|-----|--------|
| F1 F2 F3 F4 | help, rename inline (Tab moves on to the row's permissions and owner when those columns are shown), view (folder: compute size), edit in a terminal ($VISUAL, $EDITOR, else nvim, vim, nano or vi) |
| Shift+F1 | about: name, version and build date |
| F5 F6 F7 F8 | copy, move, new folder, trash (F5 inside an archive extracts) |
| Shift+F2, Shift+F5 | multi-rename panel, folder sync panel |
| Alt+F5 | pack the marked items into a new archive (zip, tar.gz, tar.xz, tar.zst, 7z) in the other pane |
| Alt+F7, Alt+Enter | find in tree; properties, with permissions and owner for every marked item |
| F9, F10 | the menu bar until a choice or Esc; quit, keeping the session |
| Shift+F4, Shift+F8 | new file, delete permanently |
| Tab, Shift+Tab | switch panes, focus the tree |
| Ctrl+T, Ctrl+W, Ctrl+Tab, Ctrl+Shift+Tab | new tab, close tab, next tab, previous tab |
| Ctrl+R | reload the folder |
| Ctrl+Shift+L | lock the tab: navigation opens a new tab |
| Ctrl+F1, Ctrl+F2 | hide the left or right pane |
| Ctrl+F5 | hide or show the folder tree |
| Ctrl+F3, Ctrl+F4 | recursive sizes and counts, thumbnails for this tab |
| (no key) `Columns` | the pane's column list as a bar: Space shows or hides a column; also under View in the F9 menu |
| Ctrl+D | bookmark this folder, or remove the bookmark |
| Insert, Shift+Down, Shift+Up | mark and move down, the same, mark and move up |
| Space, Ctrl+A, * | mark, mark all, invert |
| Ctrl+Shift+A | clear the marks (a folder's marks come back when you return to it) |
| Ctrl+=, Ctrl+- | mark and unmark by pattern |
| Enter, Shift+Enter | open with the default app, choose an app |
| Backspace, Left, Right | go up, go up, enter |
| Ctrl+Q | preview the cursor item |
| Ctrl+Home, Ctrl+PgUp, Ctrl+PgDown, Ctrl+End | previewed PDF: first, previous, next, last page; video: start, back 10 s, forward 10 s, end |
| Ctrl+P | previewed video: play or pause |
| Ctrl+L, Ctrl+G | type a path or URI, go to a folder from history |
| Ctrl+H | show hidden files |
| Ctrl+C, Ctrl+X, Ctrl+V | clipboard, shared with other file managers |
| Ctrl+Space | the command line: runs in the tab's folder in a terminal |
| Esc | clear the filter, close the viewer or panel, close the preview, then clear the marks |

Typing filters the current tab. Backspace edits the filter while one is
set, Space appends to it. The bar at the bottom shows what each
function key does; hold Shift, Ctrl or Alt and it relabels, naming the
modifier at its left end. F8 on a mount in the tree unmounts it, or
ejects it when it is removable.

With the mouse: middle click closes a tab, a double click on the empty
tab strip opens one at home, a double click on the path bar edits the
path (the same as Ctrl+L, for a remote location), and files drag
between panes, onto tree folders and to other apps. On a column header
a click sorts by it and a second click reverses it (folders stay
first), a drag moves the column, an edge resizes it, and a right click
lists the columns with check marks to show or hide them, with Reset
columns at the bottom. Name stays first and stays shown. Each pane
keeps its own columns, and remembers them.

## Build

Dependencies: Qt 6 (Core, Gui, Widgets, DBus), GLib and GIO 2.74 or
newer, libglycin 2, CMake 3.28, Ninja, a C++23 compiler. At run time,
optional: bsdtar (libarchive) for archives, tumbler for thumbnails,
pdftoppm and pdfinfo (poppler) for PDF pages in the preview, ffmpeg
and ffprobe for video in the preview, gvfs for the trash, mounts and
network shares, xdg-terminal-exec or $TERMINAL for F4 and the command
line. Without one of them the window says so when you reach the
feature, or the preview keeps the thumbnail; nothing else changes.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
build/src/app/superfar [left-folder [right-folder]]
```

`cmake --install build --prefix ~/.local` installs the binary, the
desktop entry and the icon. To make it the default for folders:

```sh
xdg-mime default superfar.desktop inode/directory
```

Each [release](https://github.com/crustovsky/superfar/releases) carries
a pacman package (`pacman -U superfar-*.pkg.tar.zst`) and a binary
tarball whose `install.sh` copies it under `~/.local` or a prefix you
name. Both are built on Arch and need its library generation: Qt 6.12,
glib 2.90 and glycin 2 or newer. From a checkout, `tools/mkpkg.sh` and
`tools/mktarball.sh` build the same two files.

## Permissions and owner

Switch the two columns on from the header's list. In a row, F2 opens
the name and Tab moves to the mode and then the owner (Shift+Tab goes
back, Up and Down move to the next row in the same column). The mode
takes the nine letters or octal, setuid, setgid and sticky included
(`rwsr-xr-x`, `2755`); the owner takes `user:group`, `user` or
`:group`, names or numbers, and completes names as you type. A row over
a link shows its target, and that is what an edit there changes.

The properties panel (Alt+Enter) does the same for everything marked.
Apply runs as a job in the strip, with progress, Abort and a log of
what was refused. With "the contents of folders too" checked it goes
down the tree the careful way: folders take the bits as set, a file
takes execute only where it already has one, links are never followed
and another filesystem mounted below is left alone. Only root can give
a file to another user; anyone can change the group to one of their
own.

## Config

`~/.config/superfar/superfar.ini` is written with the default keys on
first run and never touched again, so a dotfile manager can own it.
`[keys]` has one line per command (`Copy=F5`, `GoUp=Backspace;Left`,
empty unbinds); `[columns/left]` and `[columns/right]` take
`order=name,modified,size` and `hidden=items,modified`; `[layout]` has
`split=side-by-side|stacked` and `preview=borrow|third`. What the app
records on its own goes to `~/.local/state/superfar/state.ini`: the
session, the Ctrl+G history, window and splitter sizes, and the
columns of each pane (order, hidden set, dragged widths). The column
names are `name`, `size`, `items`, `modified`, `permissions` and
`owner`; the last two are off until `order=` lists them or the window
switches them on.

The columns in the config are a starting point: they apply until you
change a pane's columns in the window, after which the state file's
copy is what counts for that pane. Reset columns in the list drops
that copy and the config (or the built-in order) applies again.

## Status

All twelve milestones of the plan are done; [CLAUDE.md](CLAUDE.md)
holds the rules the code follows.
Non-goals: plugins, theming beyond the platform theme, an embedded
terminal, a search index, translations before 1.0. Nothing decodes
media or documents in the process: thumbnails come from tumbler, PDF
pages and video frames from poppler and ffmpeg in child processes.

Known limits: copies go through GIO, which writes the holes of a sparse
file out as zeros (Nautilus and Thunar do the same). Drag and drop,
mounting with credentials and remote shares have had the least testing.

## Licence

MIT.
