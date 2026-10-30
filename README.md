# superfar

A dual-pane file manager for Linux in the Directory Opus mould, at
Thunar's weight, with FAR-style function keys. Keyboard first, one
window, and it never defines a colour: whatever your platform theme
says is what it looks like.

![Dual pane with folder counts](docs/screenshots/dual-pane.png)

## Why

Thunar is light but slow to improve, and Linux has had no dual-pane
manager that feels native rather than borrowed from GNOME or KDE.
superfar keeps the Opus workflow (tree, two panes, tabs, preview,
folder sizes) and the Thunar footprint, and puts every action on a key
you can see in the bar at the bottom.

- Two panes, always. Each has its own path bar, tabs and status line;
  the active one is unmistakable.
- Tabs own their folder, history, filter, marks and cursor, come back
  on start, and can be locked.
- Folder item counts by default; recursive sizes and counts per tab on
  Ctrl+F3, Opus style, with partial totals while the walk runs.
- Nothing pops up. Progress, conflicts, confirmations, passwords and
  the open-with list are bars inside the window.
- Previews are big: Ctrl+Q borrows the other pane (or a third slot)
  and follows the cursor. Images decode in libglycin's sandbox.
- Quick filter: just type. Marks are separate from the cursor.
- Clipboard and drag and drop speak the same formats as other file
  managers. Mounts, the trash, the network and your GTK bookmarks sit in
  the tree.
- Find in tree opens its results as a tab. Multi-rename and folder sync
  are panels inside the pane, with live previews, presets and undo.
- Archives open as read-only folders through bsdtar in a child process;
  thumbnails and video or PDF previews come from tumbler over D-Bus.
- One process, no indexer, no thumbnailer process, no daemon. About
  5 MB of its own memory with an empty window, under 50 MB with a
  100,000-entry folder open.

## Screenshots

Recursive folder sizes, computed in the background:

![Folder sizes](docs/screenshots/folder-sizes.png)

The preview following the cursor:

![Preview](docs/screenshots/preview.png)

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
| F1 F2 F3 F4 | help, rename inline, view (folder: compute size), edit in a terminal |
| F5 F6 F7 F8 | copy, move, new folder, trash (F5 inside an archive extracts) |
| Shift+F2, Shift+F5 | multi-rename panel, folder sync panel |
| Alt+F7, Alt+Enter | find in tree, properties and permissions |
| F10 | quit, keeping the session |
| Shift+F4, Shift+F8 | new file, delete permanently |
| Tab, Shift+Tab | switch panes, focus the tree |
| Ctrl+T, Ctrl+W, Ctrl+Tab | new tab, close tab, next tab |
| Ctrl+Shift+L | lock the tab: navigation opens a new tab |
| Ctrl+F1, Ctrl+F2 | hide the left or right pane |
| Ctrl+F3, Ctrl+F4 | recursive sizes and counts, thumbnails for this tab |
| Ctrl+D | bookmark this folder, or remove the bookmark |
| Insert, Space, Ctrl+A, * | mark and move down, mark, mark all, invert |
| Ctrl+=, Ctrl+- | mark and unmark by pattern |
| Enter, Shift+Enter | open with the default app, choose an app |
| Backspace, Left, Right | go up, go up, enter |
| Ctrl+Q | preview the cursor item |
| Ctrl+L, Ctrl+G | type a path or URI, go to a folder from history |
| Ctrl+H | show hidden files |
| Ctrl+C, Ctrl+X, Ctrl+V | clipboard, shared with other file managers |
| Ctrl+Space | the command line: runs in the tab's folder in a terminal |
| Esc | clear the filter, close the viewer, close the preview |

Typing filters the current tab. Backspace edits the filter while one is
set, Space appends to it.

## Build

Dependencies: Qt 6 (Core, Gui, Widgets, DBus), GLib and GIO 2.74 or
newer, libglycin 2, CMake 3.28, Ninja, a C++23 compiler. At run time,
bsdtar (libarchive) for archives and tumbler for thumbnails, both
optional.

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

On Arch, `tools/mkpkg.sh` builds a pacman package from the checkout
(the PKGBUILD lives in `packaging/aur`); install it with `pacman -U`.
Tagged releases carry the package as an asset.

## Config

`~/.config/superfar/superfar.ini` is written with the default keys on
first run. `[keys]` has one line per command (`Copy=F5`,
`GoUp=Backspace;Left`, empty unbinds); `[columns/left]` and
`[columns/right]` hold widths and `hidden=items,modified`; `[layout]`
has `split=side-by-side|stacked` and `preview=borrow|third`; the
session and the Ctrl+G history live there too.

## Status

All twelve milestones of the plan in [CLAUDE.md](CLAUDE.md) are done.
Non-goals: plugins, theming beyond the platform theme, an embedded
terminal, a search index, translations before 1.0. Media and PDF show
as tumbler thumbnails, never through a decoder in the process.

## Licence

MIT.
