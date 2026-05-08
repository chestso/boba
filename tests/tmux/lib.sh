#!/usr/bin/env bash
# tests/tmux/lib.sh - Helpers for tmux-driven end-to-end tests.
#
# Sourced by every script under tests/tmux/. The driving Makefile sets
# TMUX_BIN_DIR to the directory containing the test mini-apps.

set -u

: "${TMUX_BIN_DIR:?TMUX_BIN_DIR must point at the directory of test mini-apps}"

# Start a detached tmux session of the given size running the given binary.
# Sets remain-on-exit so a crashed binary leaves a debuggable pane.
tmux_start()
{
    local session=$1 cols=$2 rows=$3 bin=$4
    shift 4
    if [ ! -x "$bin" ]; then
        echo "tmux_start: binary not executable: $bin" >&2
        return 1
    fi
    TERM=xterm-256color LC_ALL=C.UTF-8 \
        tmux new-session -d -x "$cols" -y "$rows" -s "$session" "$bin" "$@"
    tmux set-option -t "$session" remain-on-exit on >/dev/null
}

tmux_send()
{
    local session=$1
    shift
    tmux send-keys -t "$session" "$@"
}

tmux_capture()
{
    tmux capture-pane -t "$1" -p
}

# Print "<x> <y>" of the cursor in the given session's active pane.
tmux_cursor()
{
    tmux display-message -t "$1" -p '#{cursor_x} #{cursor_y}'
}

tmux_kill()
{
    tmux kill-session -t "$1" 2>/dev/null || true
}

# Poll capture-pane until the regex matches or timeout (in ms) elapses.
tmux_wait_for()
{
    local session=$1 regex=$2 timeout_ms=${3:-2000}
    local deadline=$(( $(date +%s%3N) + timeout_ms ))
    while [ "$(date +%s%3N)" -lt "$deadline" ]; do
        if tmux_capture "$session" | grep -qE -- "$regex"; then
            return 0
        fi
        sleep 0.05
    done
    echo "tmux_wait_for: regex /$regex/ never matched within ${timeout_ms}ms" >&2
    echo "--- pane contents ---" >&2
    tmux_capture "$session" >&2
    echo "--- end pane ---" >&2
    return 1
}

# Dump the captured pane to stderr. Useful in failure paths.
dump_pane()
{
    echo "--- pane: $1 ---" >&2
    tmux_capture "$1" >&2
    echo "--- end pane ---" >&2
}

assert_pane_contains()
{
    local session=$1 needle=$2
    if ! tmux_capture "$session" | grep -qF -- "$needle"; then
        echo "assert_pane_contains failed: pane lacks '$needle'" >&2
        dump_pane "$session"
        return 1
    fi
}

assert_pane_lacks()
{
    local session=$1 needle=$2
    if tmux_capture "$session" | grep -qF -- "$needle"; then
        echo "assert_pane_lacks failed: pane contains '$needle'" >&2
        dump_pane "$session"
        return 1
    fi
}

assert_cursor_x_lt()
{
    local session=$1 max=$2
    local x y
    read -r x y < <(tmux_cursor "$session")
    if [ -z "$x" ] || [ "$x" -ge "$max" ]; then
        echo "assert_cursor_x_lt failed: cursor_x=$x not < $max" >&2
        dump_pane "$session"
        return 1
    fi
}
