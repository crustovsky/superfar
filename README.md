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

[Install](#install) · [Features](#features) · [Screenshots](#screenshots) ·
[Keys](#keys) · [Mouse](#mouse) · [Columns](#columns) ·
[Permissions and owner](#permissions-and-owner) · [Config](#config) ·
[Building](#building-from-source) · [Status](#status-and-limits)

## Why

Thunar is light but slow to improve, and Linux has had no dual-pane
manager that feels native rather than borrowed from GNOME or KDE.
superfar keeps the Opus workflow (tree, two panes, tabs, preview,
folder sizes) and the Thunar footprint, and puts every action on a key
you can see in the bar at the bottom.

## Install

Each [release](https://github.com/crustovsky/superfar/releases) carries
two files, both built on Arch and needing its library generation (Qt
6.12, glib 2.90 and glycin 2 or newer):

- a pacman package: `pacman -U superfar-*.pkg.tar.zst`
- a binary tarball whose `install.sh` copies it under `~/.local` or a
  prefix you name

Anywhere else, [build it from source](#building-from-source). To make it
the default for folders:

```sh
xdg-mime default superfar.desktop inode/directory
```

Some features lean on a tool that may not be installed. Without it the
window says so when you reach the feature, or the preview keeps the
thumbnail; nothing else changes.

| Tool | Used for |
|---|---|
| bsdtar (libarchive) | archives: open, extract, pack |
| tumbler | thumbnails |
| pdftoppm and pdfinfo (poppler) | PDF pages in the preview |
| ffmpeg and ffprobe | video in the preview |
| bat (0.25 or newer) | syntax colours in the viewer and the preview |
| gvfs | the trash, mounts and network shares; going on as administrator (with a polkit agent running) |
| xdg-terminal-exec or `$TERMINAL` | F4, the command line and Alt+F3 |

## Features

### The window

| | What you get | Where |
|---|---|---|
| **Two panes** | Always open, each with its own path bar, tabs and status line; the active one is unmistakable. | Tab, Ctrl+F1, Ctrl+F2 |
| **Tabs** | A tab owns its folder, history, sort, filter, marks and cursor. Tabs come back on start and can be locked. | Ctrl+T, Ctrl+W, Ctrl+F10 |
| **Folder tree** | One tree that follows the active tab: the filesystem, the trash, your GTK bookmarks, mounts and the network. | Shift+Tab, Ctrl+F3 |
| **Function key bar** | F1 to F10 with what they do, relabelled while Shift, Ctrl or Alt is held. The menu bar stays out of the way until F9, and a right click offers the same commands for what is under the pointer. | always on screen |
| **Columns** | Size, Items, Modified, Permissions, Owner. Drag a header to reorder, right click it to choose; each pane keeps its own. Size, count and date are never cut short. | Ctrl+F8, the header |

### Working with files

| | What you get | Where |
|---|---|---|
| **Quick filter and marks** | Just type to filter. Marks are separate from the cursor and come back when you return to a folder. | typing, Insert, Space, Ctrl+A |
| **Copy, move, trash** | A job strip inside the window: progress for items and bytes, a throughput graph, Pause, Skip and Abort, conflicts and errors as bars, and an unattended mode that logs instead of asking. While a job runs, the next one asks whether to queue behind it (Enter) or run now beside it (Shift+Enter); queued jobs are listed under the running ones, each with Start now and Remove. | F5, F6, F8 |
| **Permissions and owner** | `rwxr-xr-x` and `user:group` in the list as `ls -l` has them, sortable, edited in the row. For many items at once, and down their folders, in the properties panel. | F2 then Tab, Alt+F2 |
| **As administrator** | Where permission is denied on a local folder or item, a bar offers to go on as administrator: opening the folder, the job's item (for all that follow, if you say so), a rename, a new folder or file. It goes through GVfs's `admin://` and polkit, so the password dialog is the system's own and nothing of superfar runs as root. Unattended jobs never escalate. | the bar, or `admin:///path` in Ctrl+L |
| **Archives** | Open as read-only folders, extract with F5, pack the marked items into zip, tar.gz, tar.xz, tar.zst or 7z. | Enter, F5, Alt+F5 |
| **Open with and edit** | Open the marked items with another app: type to narrow the list, Enter opens, Ctrl+Enter also makes it the default; text that names no app runs as a command. F4 edits in a terminal with `$VISUAL`, `$EDITOR`, else nvim, vim, nano or vi. | Shift+F3, F4 |
| **Multi-rename** | Wildcards, regex, find and replace, numbering, case and presets, with a live preview that calls out clashes. | Shift+F2 |
| **Undo and redo** | Copy, move, trash, rename, new folder or file, extract, pack and attribute changes can be taken back. What an operation created goes to the trash, what it replaced comes back from it, and an item changed since is left alone. Nothing is deleted, except where there is no trash (a network share): there undo asks before it deletes. | Ctrl+Z, Ctrl+Shift+Z, Shift+F9, Shift+F10 |
| **Folder sync** | Compares the two panes by size, time and, if asked, SHA-256; copies or trashes what you check. | Shift+F5 |

### Looking and finding

| | What you get | Where |
|---|---|---|
| **Folder counts and sizes** | Item counts by default; recursive sizes and counts per tab, with partial totals while the walk runs. | Ctrl+F5, F3 on a folder |
| **Preview** | Follows the cursor in the other pane or a third slot: images, text, PDF pages, video, an archive's summary and tree, a folder's totals. | Ctrl+F4 |
| **Viewer** | Any file of any size, read in windows; hex for binaries. Source and config files up to 1 MiB in syntax colours when `bat` is installed, in the theme bat is set to for dark or light. | F3, Ctrl+F9 |
| **Thumbnails** | From tumbler over D-Bus, in taller rows, per tab. | Ctrl+F6 |
| **Find and go** | Find in tree with the results in a tab, folder history ranked by frecency, a path or a remote URI typed in. | Alt+F7, Alt+F1, Ctrl+L |
| **Remote and removable** | sftp, smb and the rest through GVfs. Mounting asks for credentials inside the window; F8 on a mount unmounts or ejects it. | Ctrl+L, the tree |

### How it behaves

| | What you get | Where |
|---|---|---|
| **Plays well with others** | Clipboard and drag and drop speak the formats other file managers do; the command line runs in the tab's folder in your terminal, and Alt+F3 opens that terminal there. | Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+Space, Alt+F3 |
| **Nothing pops up** | Progress, questions, passwords, the open-with list and errors are bars inside the one window. | |
| **Your theme** | No colour of its own: what the platform theme says (qt6ct, Kvantum, Fusion) is what it looks like. | |
| **Light** | One process, no indexer, no daemon. About 5 MB of its own memory with an empty window, under 50 MB with a 100,000-entry folder open. | |

Nothing decodes media or documents inside the process: images go
through libglycin's sandbox, PDF pages through pdftoppm and video
frames through ffmpeg in child processes (video plays without sound),
archives through bsdtar, thumbnails through tumbler.

## Screenshots

Click one for its full size.

| | |
|---|---|
| ![Permissions](docs/screenshots/permissions.png)<br>Permissions and owner as columns; F2 opens the name, Tab moves to the row's mode | ![Properties](docs/screenshots/properties.png)<br>Properties on Alt+F2 over several marked items: bits they differ in are half checked and stay |
| ![Columns](docs/screenshots/columns.png)<br>The column list on Ctrl+F8: Space shows or hides, Shift+Up and Shift+Down move | ![Folder sizes](docs/screenshots/folder-sizes.png)<br>Recursive folder sizes on Ctrl+F5, computed in the background; with Ctrl held the bar shows that layer |
| ![Preview](docs/screenshots/preview.png)<br>The preview on Ctrl+F4, following the cursor | ![PDF pages](docs/screenshots/pdf-pages.png)<br>A PDF in the preview, paged with Ctrl+PgUp and Ctrl+PgDown or the buttons |
| ![Video](docs/screenshots/video.png)<br>A video in the preview, playing after Ctrl+P, without sound; the same keys seek | ![Viewer](docs/screenshots/viewer.png)<br>The F3 viewer on a binary file |
| ![Multi-rename](docs/screenshots/multi-rename.png)<br>Multi-rename on Shift+F2, the clashes called out before anything is renamed | ![Folder sync](docs/screenshots/folder-sync.png)<br>Folder sync on Shift+F5, comparing the two panes |
| ![Archive](docs/screenshots/archive.png)<br>An archive opened as a folder, its tab named after it | ![Find](docs/screenshots/find.png)<br>Find in tree on Alt+F7, results in a tab |
| ![Go to](docs/screenshots/go-to.png)<br>Folder history on Alt+F1, ranked by frecency | ![Thumbnails](docs/screenshots/thumbnails.png)<br>Thumbnails on Ctrl+F6, from tumbler through the freedesktop cache |

## Keys

Almost everything is on a function key, in four layers, and the bar at
the bottom shows the layer of whatever modifier you hold, naming the
modifier at its left end:

| | plain | Shift | Ctrl | Alt |
|---|---|---|---|---|
| **F1** | help | about | left pane | go to |
| **F2** | rename | multi-rename | right pane | properties |
| **F3** | view | open with | tree | terminal |
| **F4** | edit | new file | preview | |
| **F5** | copy | sync | sizes | pack |
| **F6** | move | | thumbnails | bookmark |
| **F7** | new folder | | hidden files | find |
| **F8** | trash | delete | columns | invert marks |
| **F9** | menu | undo | syntax colours | mark by pattern |
| **F10** | quit | redo | lock tab | unmark by pattern |

Each layer has one rule:

- **plain** is FAR's row.
- **Shift** is the plain key in another form: rename and multi-rename,
  trash and delete. The menu and quit have no other form, so Shift+F9
  and Shift+F10 hold undo and redo.
- **Ctrl** is what is shown, each key a switch: F1 to F4 the parts of
  the window, F5 to F10 the settings of the tab.
- **Alt** is tools and panels, then the marks on F8 to F10. Alt+F4 is
  left to the window manager.

F1 shows the same grid and every key inside the window, with whatever
you have remapped. A config written by 0.9 or older follows the keys
that moved in 0.9.5 (the Ctrl layer); a line you changed yourself is
left alone.

The rest is not on the bar:

| Key | Action |
|-----|--------|
| Tab, Shift+Tab | switch panes, focus the tree |
| Enter, Right | open with the default app; enter a folder or an archive |
| Backspace, Left | go up |
| any text | filters the current tab; Backspace edits the filter while one is set |
| Insert, Shift+Down, Shift+Up | mark and move down, or up |
| Space, Ctrl+A, Ctrl+Shift+A | mark (while no filter is set), mark all, clear the marks |
| Esc | clear the filter, close the viewer or panel, close the preview, then clear the marks |
| Ctrl+T, Ctrl+W, Ctrl+Tab, Ctrl+Shift+Tab | new tab, close tab, next tab, previous tab |
| Ctrl+L, Ctrl+R | type a path or URI, reload the folder |
| Ctrl+C, Ctrl+X, Ctrl+V | clipboard, shared with other file managers |
| Ctrl+Z, Ctrl+Shift+Z | undo and redo, as on Shift+F9 and Shift+F10 |
| Ctrl+Space | the command line: runs one command in the tab's folder in a terminal |
| Ctrl+Home, Ctrl+PgUp, Ctrl+PgDown, Ctrl+End, Ctrl+P | page a previewed PDF or seek a video; play or pause |

Several keys of the bar also answer to the chord other file managers
use: Ctrl+Q preview, Ctrl+H hidden files, Ctrl+G go to, Ctrl+D
bookmark, Alt+Enter properties, Shift+Enter open with, `*` invert the
marks, Ctrl+= and Ctrl+- mark and unmark by pattern, Ctrl+Shift+L lock
the tab.

## Mouse

- **Right click** brings a short menu of what the keys do for the thing
  under it, each entry with its key beside it: an item of the list (the
  marked items when there are marks; the menu's first line says which),
  the list's empty space (new folder, paste, what the pane shows), a
  tab, a folder or a mount in the tree.
- **Column headers:** a click sorts and a second click reverses it
  (folders stay first), a drag moves the column, an edge resizes it, a
  right click lists the columns.
- **Tabs:** middle click closes one; a double click on the empty strip
  opens one at home.
- **Path bar:** a double click edits the path (the same as Ctrl+L, for a
  remote location).
- **Drag and drop:** files drag between panes, onto tree folders and to
  other apps, and drops from other apps are accepted.

## Columns

Name, Size, Items, Modified, Permissions and Owner; the last two are
off until you switch them on. Ctrl+F8 lists a pane's columns: Space
shows or hides one, Shift+Up and Shift+Down move it, and Reset columns
at the bottom goes back to the defaults. The header does the same with
the mouse. Name stays first and stays shown; when a pane is too narrow
for its columns the list scrolls sideways instead of cutting them off.

Each pane keeps its own columns and remembers them in the state file.
The config can give a pane a starting point, which applies until you
change that pane's columns in the window (Reset columns brings it
back):

```ini
[columns/left]
order=name,permissions,owner,modified
hidden=size,items
```

The names are `name`, `size`, `items`, `modified`, `permissions` and
`owner`. Listing Permissions or Owner in `order=` switches it on.

## Permissions and owner

Switch the two columns on from the header's list. In a row, F2 opens
the name and Tab moves to the mode and then the owner (Shift+Tab goes
back, Up and Down move to the next row in the same column). The mode
takes the nine letters or octal, setuid, setgid and sticky included
(`rwsr-xr-x`, `2755`); the owner takes `user:group`, `user` or
`:group`, names or numbers, and completes names as you type. A row over
a link shows its target, and that is what an edit there changes.

The properties panel (Alt+F2, or Alt+Enter) does the same for everything
marked. Apply runs as a job in the strip, with progress, Abort and a log of
what was refused. With "the contents of folders too" checked it goes
down the tree the careful way: folders take the bits as set, a file
takes execute only where it already has one, links are never followed
and another filesystem mounted below is left alone. Only root can give
a file to another user; anyone can change the group to one of their
own.

## Config

Two files, so a dotfile manager can own the first without seeing churn
from the second:

| File | Holds | Written by |
|---|---|---|
| `~/.config/superfar/superfar.ini` | keys, a starting point for the columns, layout choices | you; the app writes the default keys on first run, adds a line when a version brings a new command, and updates a line still at an old default |
| `~/.local/state/superfar/state.ini` | the session, the folder history, window and splitter sizes, each pane's columns | the app |

```ini
[keys]
Copy=F5
GoUp="Backspace;Left"

[layout]
split=side-by-side
preview=borrow
```

- `[keys]` has one line per command. A line with more than one key
  needs its quotes, since an unquoted semicolon starts a comment; an
  empty value unbinds the command.
- `[layout]`: `split` is `side-by-side` or `stacked`; `preview` is
  `borrow` (the other pane) or `third` (a slot of its own).
- `[columns/left]` and `[columns/right]`: see [Columns](#columns).

## Building from source

Qt 6 (Core, Gui, Widgets, DBus), GLib and GIO 2.74 or newer, libglycin
2, CMake 3.28, Ninja and a C++23 compiler:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
build/src/app/superfar [left-folder [right-folder]]
```

`cmake --install build --prefix ~/.local` installs the binary, the
desktop entry and the icon. `tools/mkpkg.sh` and `tools/mktarball.sh`
build the two release files from a checkout.

## Status and limits

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
