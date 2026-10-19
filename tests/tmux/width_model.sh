#!/usr/bin/env bash
# tests/tmux/width_model.sh - The width model is resolved before the first
# frame, and inside a multiplexer it is the GRID rule: tmux owns the
# columns, so markers inherited from the terminal outside it (kitty's
# KITTY_WINDOW_ID, portty's TERM) must not leak through. The probe app
# prints the model the runtime installed, so the assertion is about the
# runtime's resolution, not about the environment.

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_width_$$
BIN="$TMUX_BIN_DIR/width_probe"

trap 'tmux_kill "$SESSION"' EXIT

tmux_start "$SESSION" 60 8 "$BIN"

# The probe prints once per frame; wait for the line to render.
tmux_wait_for "$SESSION" 'model='

# tmux is the innermost authority: grid, not uts51.
assert_pane_contains "$SESSION" "model=grid"

# And the measurement the runtime would lay out with along with it: ⚡︎ is
# two cells under the grid rule (the base's East Asian width), ⚡ is too.
assert_pane_contains "$SESSION" "bolt_vs15=2"
assert_pane_contains "$SESSION" "bolt_wide=2"

echo "PASS: width_model"
