#!/usr/bin/env bash
# tests/tmux/focus_shift_tab.sh - Regression test for the canonical
# Bubbletea-style focus-cycle composition pattern. Boots a textinput +
# viewport mini-app where the parent intercepts TUI_KEY_TAB and swaps
# focus between the two children. Verifies that pressing Shift+Tab
# (which the input parser decodes from CSI Z) flips focus from the
# input to the viewport and back again.

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_focus_$$
BIN="$TMUX_BIN_DIR/tmux_focus_swap"

trap 'tmux_kill "$SESSION"' EXIT

tmux_start "$SESSION" 40 12 "$BIN"

# Initial focus indicator and prompt must render. The viewport content
# (`alpha`) doubles as proof both children rendered.
tmux_wait_for "$SESSION" '\[INPUT\]'
tmux_wait_for "$SESSION" '^>'
assert_pane_contains "$SESSION" 'alpha'

# Shift+Tab → CSI Z. Parent intercepts and swaps focus to viewport.
tmux_send "$SESSION" BTab
tmux_wait_for "$SESSION" '\[VIEW\]'
assert_pane_lacks "$SESSION" '[INPUT]'

# Shift+Tab again → cycles back to textinput.
tmux_send "$SESSION" BTab
tmux_wait_for "$SESSION" '\[INPUT\]'
assert_pane_lacks "$SESSION" '[VIEW]'

echo "PASS: focus_shift_tab"
