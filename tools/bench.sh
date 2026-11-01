#!/usr/bin/env bash
# Opens a 100,000-entry folder and a 2,000-entry folder in superfar and
# Thunar and reports time to first frame (window mapped, measured from
# outside with hyprctl), superfar's own marks (first rows, loaded), and
# memory after the app settles: PSS for the whole footprint and anonymous
# memory for what the app itself allocates. PSS shares library pages
# with every other process mapping them, so it drifts with whatever else
# is running; the anonymous figure is the one to gate on. Thunar is a
# different toolkit and is shown for reference only.
#
#   tools/bench.sh [--cold] [--no-thunar]
#
# --cold drops the page cache before each launch (needs sudo).
# Data lives in $BENCH_DIR (default /tmp/superfar-bench) and is reused.
set -euo pipefail

# A throwaway config and state so bench folders never enter the real
# session; the state knows the version so no about note skews timing.
export XDG_CONFIG_HOME=$(mktemp -d)
export XDG_STATE_HOME=$XDG_CONFIG_HOME
trap 'rm -rf "$XDG_CONFIG_HOME"' EXIT

BIN=${SUPERFAR_BIN:-build/src/app/superfar}
mkdir -p "$XDG_STATE_HOME/superfar"
printf '[about]\nlastVersion=%s\n' "$("$BIN" --version | cut -d' ' -f2)" > "$XDG_STATE_HOME/superfar/state.ini"
DATA=${BENCH_DIR:-/tmp/superfar-bench}
SETTLE=${SETTLE:-3}
COLD=0
THUNAR=1
for arg in "$@"; do
    case $arg in
    --cold) COLD=1 ;;
    --no-thunar) THUNAR=0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

[[ -x $BIN ]] || { echo "no binary at $BIN; build first" >&2; exit 1; }
command -v hyprctl >/dev/null || { echo "hyprctl needed for window timing" >&2; exit 1; }
command -v jq >/dev/null || { echo "jq needed" >&2; exit 1; }

prepare() {
    if [[ ! -f $DATA/.ready ]]; then
        echo "creating bench folders under $DATA" >&2
        rm -rf "$DATA"
        mkdir -p "$DATA/big" "$DATA/small"
        (cd "$DATA/big" && seq 1 90000 | sed 's/^/file/' | xargs touch && seq 1 10000 | sed 's/^/dir/' | xargs mkdir)
        (cd "$DATA/small" && seq 1 1800 | sed 's/^/file/' | xargs touch && seq 1 200 | sed 's/^/dir/' | xargs mkdir)
        touch "$DATA/.ready"
    fi
}

now_ms() { date +%s%3N; }

drop_caches() {
    sync
    sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
}

# Polls until a window with the class is mapped; prints ms since start.
wait_window() {
    local class=$1 start=$2 deadline=$(( $(now_ms) + 20000 ))
    while (( $(now_ms) < deadline )); do
        if hyprctl clients -j | jq -e --arg c "$class" 'any(.[]; .class == $c and .mapped)' >/dev/null; then
            echo $(( $(now_ms) - start ))
            return
        fi
        sleep 0.005
    done
    echo "-"
}

pss_mb() {
    awk '/^Pss:/{printf "%.1f", $2 / 1024}' "/proc/$1/smaps_rollup"
}

anon_mb() {
    awk '/^Anonymous:/{printf "%.1f", $2 / 1024}' "/proc/$1/smaps_rollup"
}

mark() {
    # mark <log> <name>: ms of the first matching bench line, or -
    awk -v n="$2" '$1 == "bench" && $2 == n {print $3; exit}' "$1" | grep . || echo "-"
}

row() {
    printf "%-9s %-9s %10s %10s %8s %8s %8s %9s\n" "$@"
}

run_superfar() {
    local label=$1; shift
    local log
    log=$(mktemp)
    (( COLD )) && drop_caches
    local start
    start=$(now_ms)
    SUPERFAR_BENCH=1 "$BIN" "$@" 2>"$log" &
    local pid=$!
    local frame
    frame=$(wait_window superfar "$start")
    sleep "$SETTLE"
    local pss anon
    pss=$(pss_mb "$pid")
    anon=$(anon_mb "$pid")
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null || true
    row superfar "$label" "$frame" "$(mark "$log" first-rows)" "$(mark "$log" loaded)" "$pss" "$anon" "-"
    rm -f "$log"
}

run_superfar_then() {
    # Loads big, then navigates to small; PSS after both is the leak check.
    local log
    log=$(mktemp)
    (( COLD )) && drop_caches
    local start
    start=$(now_ms)
    SUPERFAR_BENCH=1 SUPERFAR_BENCH_THEN="$DATA/small" "$BIN" "$DATA/big" 2>"$log" &
    local pid=$!
    wait_window superfar "$start" >/dev/null
    sleep $(( SETTLE + 3 ))
    local pss anon
    pss=$(pss_mb "$pid")
    anon=$(anon_mb "$pid")
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null || true
    row superfar "big>small" "-" "-" "-" "$pss" "$anon" "-"
    rm -f "$log"
}

run_thunar() {
    local label=$1 folder=$2
    if pgrep -x thunar >/dev/null; then
        row thunar "$label" "-" "-" "-" "-" "-" "running, skipped"
        return
    fi
    (( COLD )) && drop_caches
    local start
    start=$(now_ms)
    thunar "$folder" >/dev/null 2>&1 &
    local pid=$!
    local frame
    frame=$(wait_window thunar "$start")
    sleep "$SETTLE"
    local pss anon
    pss=$(pss_mb "$pid")
    anon=$(anon_mb "$pid")
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null || true
    row thunar "$label" "$frame" "-" "-" "$pss" "$anon" "-"
}

prepare
echo "cache: $([[ $COLD == 1 ]] && echo cold || echo warm), settle ${SETTLE}s"
row app folder frame_ms rows_ms loaded_ms pss_mb anon_mb note
row --- ------ -------- ------- --------- ------ ------- ----
run_superfar baseline --baseline
run_superfar small "$DATA/small"
run_superfar big "$DATA/big"
run_superfar_then
if (( THUNAR )); then
    run_thunar small "$DATA/small"
    run_thunar big "$DATA/big"
fi
