# superfar

Dual-pane GUI file manager for Linux. Directory Opus workflow (tree, dual
pane, tabs, preview, multi-rename, folder sync), Thunar weight, FAR-style
function keys. Keyboard first, one window, follows the OS theme.

## Philosophy

Simplicity and readability over cleverness. Less is more. Every feature must
justify its complexity cost. Prefer well-tested platform libraries over new
abstractions. The bar is Thunar: match its time to first rows, stay lean,
beat it on keyboard workflow. Never get in the way: no popups, no
toolkit-imposed layout, no colours of its own.

## Why it exists (Thunar pain points)

These are the product. When in doubt, they win.

1. **Dual pane is the default.** Two panes always open; Ctrl+F1 / Ctrl+F2
   hide the left / right pane.
2. **Tabs per pane.** A tab owns its folder, history, sort, filter,
   selection and cursor. Tabs are restored on start.
3. **The active pane is unmistakable.** Each pane has its own path bar,
   tab strip (at the bottom, Opus style) and status line. The active pane gets an accent border and
   highlighted path bar; the inactive pane shows its cursor as an outline
   only. Never one shared path bar.
4. **Folder tree on the left.** One tree for the window that follows the
   active tab of the active pane. Selecting a folder navigates that tab.
5. **Function key bar, not menus.** A clickable bar at the bottom shows
   F1 to F10 with their actions; holding Shift, Ctrl or Alt relabels it.
   The menu bar is hidden; F9 shows it.
6. **Folder counts and sizes.** Folders show their item count by default on
   local filesystems. An opt-in mode (per tab, toggled from the function
   bar) computes recursive size and count down the tree, Opus style. F3 on
   a folder computes its size.
7. **One window, no popups.** Progress, conflicts, confirmations, password
   prompts, open-with and errors appear inside the window. Copy progress
   is Opus grade: a one-line job strip that expands into a panel with
   from / to, current item bar, overall bar (items and bytes), elapsed and
   remaining, a throughput graph, and Skip, Pause, Unattended and Abort.
   Unattended never stops: it applies the default policy, logs every skip
   and failure, and shows the list when the job ends.
8. **Columns fit their content.** Size, count and date never truncate; each
   pane keeps its own set, order and widths, changed from its header.
9. **Previews are big.** Ctrl+Q previews the cursor item. By default it
   borrows the opposite pane (the window is often one column wide in a
   scrolling layout); a config option gives it a dedicated third slot, Opus
   style. Same component, same key, different slot. Images fit the slot,
   text shows its head, folders show count and recursive size, archives
   show a summary (counts, sizes, one folder or a tarbomb, newest member,
   dropped paths) and the top of their tree from bsdtar's listing,
   anything else shows type, size, dates and permissions.
10. **It follows the OS theme.** The app never defines a colour. Whatever
    the platform theme says (Kvantum, qt6ct, Fusion) is what it looks like.

## Stack

- C++23, CMake + Ninja, warnings as errors
- Qt 6 Widgets only: Core, Gui, Widgets, DBus. No QML, no KDE Frameworks
- GIO (GLib) for the filesystem: local and remote (GVfs), trash, mounts,
  monitors, MIME, default apps, copy and move. GLib is not GTK; Thunar
  uses the same split

Rationale: Qt Widgets fits a dense keyboard-driven app. Current index and
selection are separate objects (an Opus or FAR cursor needs exactly that),
item views virtualize 100k rows, server-side decorations work on
Hyprland, the platform theme plugin applies the user's theme, CPU raster
painting keeps idle memory low, and DnD and the clipboard speak uri-list
with other apps. GIO is the only complete Linux VFS outside KDE, and Qt's
event dispatcher runs on the GLib main loop, so GIO async callbacks land
on the Qt main thread. Qt without KDE Frameworks carries no KDE-isms; GIO
without GTK carries no GNOME-isms.

Rejected: GTK4 + Rust (close second: GPU renderer memory, client-side
decorations only, selection model fights a FAR cursor), Rust-native
toolkits (no drag out, text-only clipboard), KDE Frameworks, GTK3, QML,
web / Flutter / wx, a separate backend process.

## Dependencies

Allowlist: Qt6 (Core, Gui, Widgets, DBus, Test), GLib / GIO, libglycin
(sandboxed image decoding for the preview; if unpackaged, decode in a
child process of our own binary, never in-process). Config is QSettings
INI; no TOML library. Anything else needs a one-line justification in the
commit that adds it. Never: QML, KDE Frameworks, GTK, WebEngine,
GStreamer, Poppler, Boost, an auto-updater, a search indexer.

## Keys (FAR-style, remappable)

- Tab switches panes; Shift+Tab focuses the tree
- Ctrl+T new tab (same folder), Ctrl+W close, Ctrl+Tab / Ctrl+Shift+Tab
  cycle; Ctrl+Shift+L locks a tab so navigation opens a new tab (shown
  as [name])
- F1 help (Shift+F1 about), F2 rename inline (Tab: the row's mode and
  owner cells, when shown), F3 view (folder: compute size), F4 edit, F5
  copy, F6 move, F7 mkdir, F8 trash, F9 menu, F10 quit
- Shift+F2 multi-rename panel for the marked items (wildcard, regex,
  find and replace, {n} numbering, case, presets, undo)
- Shift+F5 folder sync panel: compares the active tab with the other
  pane's by size, time and optional SHA-256; copies or trashes the
  checked differences through the job engine
- Alt+F5 pack panel: the marked items into a new zip, tar.gz, tar.xz,
  tar.zst or 7z (name, level, this folder or the other pane's) by
  bsdtar in a child, with progress, conflict and abort like a copy
- Ctrl+F3 toggles recursive sizes and counts for the tab (Items shows
  the direct count otherwise); Ctrl+F4 thumbnails (tumbler over D-Bus,
  taller rows); Ctrl+F5 hides the tree; a header click sorts the tab,
  a drag moves the column, a right click (or Columns, unbound) lists them
- Shift+F4 new file, Shift+F8 delete permanently (confirm in window),
  Alt+F7 find in tree (fuzzy over names, results in a tab)
- Insert or Shift+Down selects and moves down, Shift+Up selects and
  moves up; Space selects when the filter is empty;
  Ctrl+= / Ctrl+- select and deselect by glob (keypad + and - too);
  Ctrl+A all; * invert; Ctrl+Shift+A clears. Selection is marks,
  separate from the cursor; a folder's marks come back when the tab
  returns to it after a trip into a subfolder or an archive
- Enter opens a file with its default app; Shift+Enter shows the in-window
  open-with list; Alt+Enter the properties panel (mode and owner of the
  marked items, down their folders if asked); Esc clears the filter, closes
  the viewer or panel, then the preview, then the marks
- Ctrl+D bookmarks the current folder (or removes the bookmark)
- Type to quick-filter the current tab (fuzzy, prefix-biased). Backspace
  edits the filter while one is set and goes up otherwise; Esc clears it
- Backspace and Left go up; Right and Enter open the cursor folder;
  Enter on an archive (zip, tar.*, 7z, rar, iso, ...) opens it as a
  read-only folder listed by bsdtar in a child process, F5 extracts the
  marked members to the other pane
- Ctrl+L edits the path (accepts remote URIs; an unmounted one is mounted
  with credentials asked in the window); Ctrl+G go to folder, fuzzy over
  history by frecency; Ctrl+H toggles hidden files; Ctrl+R reloads
- Tree roots: filesystem, trash, GTK bookmarks, mounts and volumes.
  Choosing a volume mounts it; F8 on a mount in the tree unmounts it
- Ctrl+Space focuses the command line, which runs in the active tab's
  directory in a terminal (xdg-terminal-exec, else $TERMINAL) and holds
  the window until Enter; Ctrl+Enter inserts the cursor's name
- Ctrl+Q preview (Ctrl+Home / PgUp / PgDown / End page a PDF or seek a
  video, Ctrl+P plays); Ctrl+C / Ctrl+X / Ctrl+V interoperate with others

## Config

`~/.config/superfar/superfar.ini`, written with defaults on first run
and never again: the user owns it (chezmoi and friends must see no
churn). `[keys]` has one line per command (`Copy=F5`,
`GoUp=Backspace;Left`; empty unbinds). `[columns/left]` and
`[columns/right]` seed a pane: `order=name,modified,size`,
`hidden=items`. `[layout]` has `split=side-by-side|stacked` and
`preview=borrow|third`. `~/.local/state/superfar/state.ini` holds what
the app writes on its own: `[session]` tabs per pane (hidden, locked,
sizes, thumbs, sort, cursor), active tab and pane, tree shown or not;
`[history]` the Ctrl+G folders by visit count and last time; `[window]`
size; `[layout]` splitter sizes; `[columns/*]` dragged widths, and the
order and hidden set once the window changes them (they then win).

## Layout

```
+--------+---------------------------+---------------------------+
|        | path bar (left)           | path bar (right)          |
|  tree  | list                      | list (or preview slot)    |
|        | tabs (left)               | tabs (right)              |
|        | status: items, selection  | status: items, free space |
+--------+---------------------------+---------------------------+
| job strip (only while a job runs; conflicts and confirms here)  |
| command line                                                    |
| 1 Help 2 Rename 3 View 4 Edit 5 Copy 6 Move 7 Mkdir 8 Trash ... |
+-----------------------------------------------------------------+
```

Optional third slot for the preview right of both panes. Split orientation
(side by side or stacked) is a config option, one QSplitter.

## Architecture

Two CMake targets. `core` links QtCore and GIO only, never QtGui or
QtWidgets; the linker enforces the split. `app` links core and Widgets.

```
src/core/             no widgets, GIO only, tested headless with QtTest
  listing.cpp         GIO enumerate in batches, generation id, cancellable
  entry.h             Entry: raw name, type, collation key and, from the
                      listing's stat, size, mtime, mode, uid, gid
  sort.cpp            natural sort by GLib collation key, dirs first
  unixattrs.cpp       mode and owner as text and back, id names cached
  dirsize.cpp         recursive size and count walker (see rule 9)
  jobs/               copy, move, trash, delete, extract, pack: progress,
                      pause, skip, cancel, conflict policy, error log; one
                      worker thread per job. Copy is g_file_copy with a
                      progress callback (reflinks, splice, metadata and
                      server-side remote copy for free): Pause blocks
                      inside the callback, Skip cancels the per-file
                      cancellable. Move is g_file_move. Trash failure on
                      another volume offers permanent delete in the
                      window. Extract and pack run bsdtar in a child.
                      Attributes: chmod and chown down a tree, chmod's X
  monitor.cpp         directory change events, coalesced by timer
  apps.cpp            GAppInfo: default app, open-with list, terminal launch
  mounts.cpp          volumes, mount, unmount; prompts go to the window
  compare.cpp         folder sync engine (later): name, size, mtime, hash
src/app/
  main.cpp
  mainwindow.cpp      tree, two panes, preview slot, active pane, keys
  tree.cpp            QTreeView over a lazy model, folders only
  pane/pane.cpp       tab strip, active tab, path bar, status line
  pane/tab.cpp        one folder view: location, history, filter, marks
  pane/listmodel.cpp  QAbstractTableModel over core's vector<Entry>
  pane/columns.cpp    the column table: name, label, width, default, sort
  pane/delegate.cpp   row painting: cursor outline, marks, palette roles
  funcbar.cpp         function key bar, modifier-aware labels
  jobs/strip.cpp      collapsed job strip, conflict and confirm bars
  jobs/panel.cpp      expanded job panel; throughput graph via QPainter
  viewer.cpp          F3 viewer, in-window, windowed reads, Esc closes
  preview.cpp         Ctrl+Q component, placed in whichever slot config says
  cmdline.cpp         command line
  keymap.cpp          FAR table, remappable from config
  config.cpp          QSettings INI: the config and the state file
```

### Rules

1. **Per-file work stays in the viewport.** Size, date, icon, content type
   and folder counts are queried when a row is painted, with a small cache.
   Sorting by size or date runs one background stat pass, then re-sorts.
   Measure first: GIO's local enumerator stats each entry for most
   attribute sets, so the minimal listing mainly pays off on remotes.
2. **The model is not a list of objects.** `listmodel.cpp` wraps a plain
   vector<Entry>. Sorting and filtering change the vector and emit one
   reset. No QObject, no QStandardItem per row.
3. **Cursor and selection survive everything.** After a reset the cursor is
   restored by name and scrolled into view; going up puts the cursor on the
   folder just left; marks are kept by name across monitor reloads.
4. **Stale work is harmless.** Every async request carries the tab's
   generation id; navigation bumps it and old replies are dropped.
   In-flight loads are cancelled with a GCancellable.
5. **Stream large folders.** The first batch paints before the load ends.
6. **Native paths.** Never assume UTF-8. Names are bytes from GIO;
   convert with the display-name function only for painting.
7. **No god classes.** A class past about 15 fields gets split. Files stay
   under about 600 lines. Pane and tab are separate for this reason.
8. **Untrusted content is not decoded in-process.** Images go through
   libglycin or a child process. The preview reads text as plain bytes,
   capped at 1 MiB. The F3 viewer has no cap: it reads windows through a
   seekable stream. Nothing is parsed. Thumbnails, when they come, use the
   freedesktop cache and tumbler over D-Bus. No other decoders.
9. **Recursive sizes are opt-in background work.** One worker pool, visible
   rows first, results cached by (device, inode, mtime), cancelled on
   navigation. Local filesystems only by default. Never follow symlinks,
   never cross mount points, count hard links once per walk. Cells show
   partial totals while the walk runs. Item counts follow the same local
   only rule and are invalidated by monitor events.
10. **One toplevel window.** Anything that would be a dialog is an
    in-window bar or overlay, including mount passwords and open-with.
11. **No colour literals.** Painting uses palette roles from the style
    option; stylesheets, if any, set spacing only. The acceptance test is
    switching the system theme while the app runs.
12. **Columns are sized from font metrics.** Size, count and date get a
    fixed width from their widest possible string; Name takes the rest.
    Never resize-to-contents on a virtualized view.
13. **Inactive tabs do nothing.** A change seen by an inactive tab's
    monitor only marks it dirty; walks are cancelled; a dirty tab reloads
    when it becomes current.
14. **Tree and pane never ping-pong.** Programmatic selection in either
    direction sets a guard; only user actions navigate.
15. **Symlinks.** Enter follows; copy and move copy the link, not the
    target; the list shows the target; attribute jobs never follow.
16. **Lean by construction.** Bounded bytes per entry, no process for
    thumbnails or indexing, nothing runs while no pane is visible.

## Performance budget

`tools/bench.sh` opens a 100,000-entry folder and a 2,000-entry folder in
superfar and Thunar, warm cache by default and cold with `--cold`. It
reports time to first frame (window mapped, measured from outside with
hyprctl), superfar's own marks (first rows, loaded), and memory after the
app settles: PSS for the whole footprint and anonymous memory for what the
app itself allocates. PSS shares library pages with every process that maps
them, so it drifts with whatever else is running; gates are on anonymous
memory. Thunar is a different toolkit: shown for reference, never a gate.

Gates, warm cache, against the milestone 2 build measured 2026-10-29:

| measure                            | gate     | measured |
|------------------------------------|----------|----------|
| first frame, empty window          | < 80 ms  | 30 ms    |
| first rows, 100k folder            | < 100 ms | 40 ms    |
| loaded, 100k folder                | < 1 s    | 0.5 s    |
| anonymous memory, empty window     | < 8 MB   | 4.4 MB   |
| anonymous memory, 2k folder open   | < 16 MB  | 12 MB    |
| anonymous memory, 100k folder open | < 60 MB  | 48 MB    |
| anonymous memory after leaving it  | < 24 MB  | 17 MB    |

Whole-footprint PSS sits around 25 MB empty and 70 MB with the 100k
folder open, depending on what else shares Qt's pages. Thunar 4.20 on
the same machine: first frame about 340 ms, about 400 MB PSS.

The 100k folder costs about 360 bytes per entry today (name, display string,
collation key, row index, name hash). That is the number to shrink, not the
gate to loosen. A milestone is not done while it misses a gate.

## Milestones

All twelve are done and tagged m1 to m12 (2026-10-29 to 2026-10-30);
the commit on each tag carries the rationale. Since then: issues.

Later: multi-folder create, Opus style (F7 takes one folder per line,
a/b/c makes the path, cursor on the first); inline folder expanders;
translations after 1.0.

## Release

`tools/release.sh X.Y.Z` bumps the version in CMakeLists.txt (the one
source) and the PKGBUILD, commits and tags vX.Y.Z. `git push
--follow-tags` runs release.yml, which refuses a tag that disagrees
with CMakeLists.txt, builds the pacman package and the binary tarball
on Arch and attaches both to a GitHub release. CI builds the same two
files on every push, runs ctest and `tools/smoke.sh` (scripted
offscreen sessions that must exit cleanly and leave the right files).
After the tag is pushed, hash GitHub's archive tarball into the
PKGBUILD's sha256sums (the AUR copy must match) and commit that.

Before a release: ctest in an `-fsanitize=address,undefined` build
(tests/test_hostile.cpp is the input fuzz), clang-tidy with the
bugprone, cert, performance and clang-analyzer checks, a render with a
light theme and one with plain Fusion, and a soak of tabs opening and
closing while memory is sampled: it must plateau. `unshare -Urm
tools/screenshots.sh` re-renders the README screenshots.

## Non-goals

Plugins, theming beyond the platform theme, an embedded terminal, a
search index (the launcher's job), translations before 1.0. Media and
PDF previews via tumbler or a child (pdftoppm, ffmpeg), never a linked decoder.

## Conventions

- No global state; pass tab state explicitly through signals and messages
- Errors are returned (std::expected), not thrown, except on programmer
  error; GError is converted at the core boundary
- GObject handles live in unique_ptr with an unref deleter; no raw
  new / delete, no manual ref counting outside the wrappers
- core emits plain QObject signals; app never includes GIO headers; core
  never includes QtGui or QtWidgets
- Test filesystem ops against a temp dir with QtTest, never mock the
  filesystem
- A defect found while working on something else, pre-existing or just
  made, gets a GitHub issue first (gh issue create, label bug, the
  style of the open ones: what is wrong, then "Proposed:" bullets) and
  its fix closes the issue from the commit. One branch and PR per
  issue; a PR that builds on another is stacked on it
- Commit messages carry the rationale. This file holds rules, not history;
  keep it under 350 lines
