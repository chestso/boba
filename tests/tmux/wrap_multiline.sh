#!/usr/bin/env bash
# tests/tmux/wrap_multiline.sh - Regression test for multiline soft-wrap.
#
# With a 40-col window (content width = 38 after the 2-char prompt) and
# soft-wrap enabled, typing beyond one row must WRAP the logical line onto a
# continuation row (indented under the text column) instead of leaving the
# content to autowrap or spill. Erasing back across the boundary must clear
# the vacated row.

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_wrap_$$
BIN="$TMUX_BIN_DIR/tmux_textinput_wrap"

trap 'tmux_kill "$SESSION"' EXIT

tmux_start "$SESSION" 40 10 "$BIN"

# Wait until the prompt has rendered.
tmux_wait_for "$SESSION" '^>'

# Type 60 'a's followed by a sentinel 'X' (61 content cells). With a 2-char
# prompt and a 40-col window, 38 cells fit on row 1; the remaining 23 wrap.
# The sentinel doubles as a sync marker.
row1_content=$(awk 'BEGIN { for (i = 0; i < 60; i++) printf "a"; }')
a38=$(awk 'BEGIN { for (i = 0; i < 38; i++) printf "a"; }')
a22=$(awk 'BEGIN { for (i = 0; i < 22; i++) printf "a"; }')

tmux_send "$SESSION" "${row1_content}X"
tmux_wait_for "$SESSION" 'X'

# 1) Row 1 is exactly prompt + 38 content cells (no autowrap spill).
row1=$(tmux_capture "$SESSION" | sed -n '1p')
if [ "$row1" != "> $a38" ]; then
	echo "row 1 unexpected: '$row1'" >&2
	dump_pane "$SESSION"
	exit 1
fi

# 2) Row 2 carries the wrapped remainder, indented under the text column.
row2=$(tmux_capture "$SESSION" | sed -n '2p')
if [ "$row2" != "  ${a22}X" ]; then
	echo "row 2 unexpected: '$row2'" >&2
	dump_pane "$SESSION"
	exit 1
fi

# 3) Cursor sits on the second visual row, within the window.
cursor=$(tmux_cursor "$SESSION")
cy=${cursor##* }
if [ "$cy" != "1" ]; then
	echo "cursor_y should be 1 (second row), got: $cy" >&2
	dump_pane "$SESSION"
	exit 1
fi
assert_cursor_x_lt "$SESSION" 40

# 4) Backspace back onto a single row: the vacated second row must be cleared.
#    61 - 40 = 21 content cells remain, all on row 1.
for _ in $(seq 1 40); do
	tmux_send "$SESSION" BSpace
done
a21=$(awk 'BEGIN { for (i = 0; i < 21; i++) printf "a"; }')
tmux_wait_for "$SESSION" "^> ${a21}\$"

row2=$(tmux_capture "$SESSION" | sed -n '2p')
if [ -n "$row2" ]; then
	echo "row 2 should be cleared after shrink, got: '$row2'" >&2
	dump_pane "$SESSION"
	exit 1
fi

echo "PASS: wrap_multiline"
