#!/bin/sh
# Headless smoke runs of the built app: a few scripted sessions on a
# temp tree through the dev aids (offscreen platform, posted keys, a
# grab that quits), each checked for a clean exit and its effect on
# disk. CI runs this after the unit tests; it is what a reviewer could
# not do by hand there. Usage: tools/smoke.sh [path/to/superfar]
set -eu
BIN=${1:-build/src/app/superfar}
BIN=$(realpath "$BIN")
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME=$T/cfg XDG_STATE_HOME=$T/state
export QT_LOGGING_RULES='*.warning=false'
# No desktop entries in sight: a scripted session must never open a file
# with its default app on the developer's desktop (CI has none anyway).
export XDG_DATA_HOME=$T/data XDG_DATA_DIRS=$T/data
fail=0

# Each run starts from a fresh config; the state knows the version so the
# about note does not take the keys.
reset() {
    rm -rf "$XDG_CONFIG_HOME" "$XDG_STATE_HOME"
    mkdir -p "$XDG_STATE_HOME/superfar"
    printf '[about]\nlastVersion=%s\n' "$("$BIN" --version | cut -d' ' -f2)" > "$XDG_STATE_HOME/superfar/state.ini"
}
# run NAME KEYS MS ARGS...: a session that posts KEYS after the first load
# and quits MS later. The file operations get time to land: a
# sanitizer build runs slowly.
run() {
    name=$1 keys=$2 ms=$3; shift 3
    if SUPERFAR_DEV_KEYS="$keys" SUPERFAR_GRAB="$T/$name.png" SUPERFAR_GRAB_MS="$ms" \
        timeout 60 "$BIN" "$@" > "$T/$name.log" 2>&1 && [ -s "$T/$name.png" ]; then
        echo "ok   $name"
    else
        echo "FAIL $name (exit $?)"; sed 's/^/     /' "$T/$name.log" | head -20; fail=1
    fi
}
check() {  # check NAME CONDITION...
    name=$1; shift
    if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fail=1; fi
}

mkdir -p "$T/a" "$T/b"
printf 'two\n' > "$T/a/two.txt"
mkdir "$T/a/sub"

reset
run start "" 800 "$T/a" "$T/b"
check "config written with keys" grep -q '^\[keys\]' "$XDG_CONFIG_HOME/superfar/superfar.ini"
check "session saved to state" grep -q '^\[session\]' "$XDG_STATE_HOME/superfar/state.ini"
check "state holds the session, not the config" sh -c "! grep -q '^\[session\]' '$XDG_CONFIG_HOME/superfar/superfar.ini'"
before=$(cat "$XDG_CONFIG_HOME/superfar/superfar.ini")
run restart "" 800
check "second start leaves the config alone" test "$before" = "$(cat "$XDG_CONFIG_HOME/superfar/superfar.ini")"

run mkdir "F7;n;Return" 2500 "$T/a" "$T/b"
check "F7 made the folder" test -d "$T/a/N"
mkdir "$T/r" && printf 'x\n' > "$T/r/a.zzqx"  # alone, so the cursor is on it
run rename "F2;b;Return" 2500 "$T/r" "$T/b"
check "F2 renamed the file" sh -c "test -e '$T/r/B.zzqx' && ! test -e '$T/r/a.zzqx'"
run copy "Ctrl+A;F5;Return" 2500 "$T/a" "$T/b"
check "F5 copied to the other pane" sh -c "test -e '$T/b/two.txt' && test -d '$T/b/sub'"
run newfile "Shift+F4;z;Return" 2500 "$T/a" "$T/b"
check "Shift+F4 made the file" test -f "$T/a/Z"
if command -v bsdtar > /dev/null; then
    run pack "Ctrl+A;Alt+F5;Return" 3500 "$T/a" "$T/b"
    check "Alt+F5 packed the folder into the other pane" sh -c "bsdtar -tf '$T/b/a.zip' | grep -q '^two.txt$'"
fi

run overlays "F1;Escape;F9;Escape;Ctrl+Q;Escape;Alt+Return;Escape;Ctrl+A;Shift+F2;Escape;Shift+F5;Escape;Ctrl+G;Escape;Ctrl+L;Escape" 8000 "$T/a" "$T/b"
# The session brings the cursor back to the Z file from the newfile run;
# Home first, so Return enters a folder instead of opening Z.
run navigation "Home;Return;Backspace;Tab;Ctrl+T;Ctrl+W;Ctrl+H;Ctrl+F3;Insert;Shift+Down;Shift+Up;Space;Asterisk;Ctrl+F1;Ctrl+F1" 7000 "$T/a" "$T/b"
run filter "t;w;Escape;p;Backspace" 3000 "$T/a" "$T/b"
run badargs "" 1200 /nonexistent/deeper foo://x
run fileargs "" 1200 /etc/hostname /etc
run viewer "Down;F3;Escape" 2000 "$T/a" "$T/b"
if command -v pdftoppm > /dev/null && [ -d /usr/share/mime ]; then
    # Alone in its folder, so the cursor is on it; the pages render in a
    # child. The MIME database must be in sight for the file to be a PDF
    # at all, but desktop entries must not: a data dir with only mime.
    mkdir "$T/pdf" "$T/mime" && cp "$(dirname "$0")/../tests/data/pages.pdf" "$T/pdf/"
    ln -s /usr/share/mime "$T/mime/mime"
    XDG_DATA_DIRS=$T/mime
    run pdfpages "Ctrl+Q;Ctrl+PgDown;Ctrl+End;Ctrl+PgUp;Ctrl+Home;Escape" 4500 "$T/pdf" "$T/b"
    XDG_DATA_DIRS=$T/data
fi

exit $fail
