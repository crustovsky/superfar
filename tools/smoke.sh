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
fail=0

# Each run starts from a fresh config; the state knows the version so the
# about note does not take the keys.
reset() {
    rm -rf "$XDG_CONFIG_HOME" "$XDG_STATE_HOME"
    mkdir -p "$XDG_STATE_HOME/superfar"
    printf '[about]\nlastVersion=%s\n' "$("$BIN" --version | cut -d' ' -f2)" > "$XDG_STATE_HOME/superfar/state.ini"
}
# run NAME KEYS MS ARGS...: a session that posts KEYS after the first load
# and quits MS later.
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

run mkdir "F7;n;Return" 1500 "$T/a" "$T/b"
check "F7 made the folder" test -d "$T/a/N"
mkdir "$T/r" && printf 'x\n' > "$T/r/a.zzqx"  # alone, so the cursor is on it
run rename "F2;b;Return" 1500 "$T/r" "$T/b"
check "F2 renamed the file" sh -c "test -e '$T/r/B.zzqx' && ! test -e '$T/r/a.zzqx'"
run copy "Ctrl+A;F5;Return" 2500 "$T/a" "$T/b"
check "F5 copied to the other pane" sh -c "test -e '$T/b/two.txt' && test -d '$T/b/sub'"
run newfile "Shift+F4;z;Return" 1500 "$T/a" "$T/b"
check "Shift+F4 made the file" test -f "$T/a/Z"

run overlays "F1;Escape;F9;Escape;Ctrl+Q;Escape;Alt+Return;Escape;Ctrl+A;Shift+F2;Escape;Shift+F5;Escape;Ctrl+G;Escape;Ctrl+L;Escape" 8000 "$T/a" "$T/b"
run navigation "Return;Backspace;Tab;Ctrl+T;Ctrl+W;Ctrl+H;Ctrl+F3;Insert;Shift+Down;Shift+Up;Space;Asterisk;Ctrl+F1;Ctrl+F1" 7000 "$T/a" "$T/b"
run filter "t;w;Escape;p;Backspace" 3000 "$T/a" "$T/b"
run badargs "" 1200 /nonexistent/deeper foo://x
run fileargs "" 1200 /etc/hostname /etc
run viewer "Down;F3;Escape" 2000 "$T/a" "$T/b"

exit $fail
