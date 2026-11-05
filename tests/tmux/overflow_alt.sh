#!/usr/bin/env bash
# tests/tmux/overflow_alt.sh - Content taller than the screen, alt screen.
#
# A FLOW-sized textinput pinned to row 1 with 40 logical lines in a 16-row
# terminal. Before the vertical window existed, every row was emitted with
# `CSI r;1H` for r > term_h: terminals clamp that to the last row, so the
# last line overdrew itself and the top of the screen was blank or stale.
#
# Asserts, after shrinking and growing the window:
#   - exactly H input rows are on screen (never more than the screen has)
#   - the first line scrolls out, the last line is visible
#   - the prompt is painted exactly once
#   - no row is painted below the bottom of the screen

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_overflow_alt_$$
BIN="$TMUX_BIN_DIR/tmux_textinput_overflow"
LINES=40

trap 'tmux_kill "$SESSION"' EXIT

# Count the input's rows in the pane: every prefilled line is nNN.
input_rows()
{
	tmux_capture "$SESSION" | grep -cE '^(> |  )n[0-9]{2}' || true
}

prompt_rows()
{
	tmux_capture "$SESSION" | grep -cE '^> ' || true
}

tmux_start "$SESSION" 80 16 "$BIN" --alt --lines "$LINES"

# The cursor starts at the end of the text, so the window is at the bottom.
tmux_wait_for "$SESSION" 'n39'

# 1) Overflow: the last line is visible, the first one is not, and exactly
#    one screenful of input rows is painted.
assert_pane_contains "$SESSION" 'n39'
assert_pane_lacks "$SESSION" 'n00'
rows=$(input_rows)
if [ "$rows" != "16" ]; then
	echo "expected 16 input rows in a 16-row terminal, got: $rows" >&2
	dump_pane "$SESSION"
	exit 1
fi

# 2) Ctrl+Home goes to the buffer start: the prompt appears, exactly once.
tmux_send "$SESSION" C-Home
tmux_wait_for "$SESSION" '^> n00'
assert_pane_contains "$SESSION" 'n00'
assert_pane_lacks "$SESSION" 'n39'
if [ "$(prompt_rows)" != "1" ]; then
	echo "expected exactly one prompt row, got: $(prompt_rows)" >&2
	dump_pane "$SESSION"
	exit 1
fi

# 3) Shrink the window: the frame must shrink with it, and no row may be
#    painted past the bottom (before the clamp those writes overdrew the
#    last row).
tmux resize-window -t "$SESSION" -x 60 -y 8 >/dev/null
tmux_wait_row_count "$SESSION" '^(> |  )n[0-9]{2}' 8
rows=$(input_rows)
if [ "$rows" != "8" ]; then
	echo "expected 8 input rows after shrinking to 8, got: $rows" >&2
	dump_pane "$SESSION"
	exit 1
fi

# 4) Grow it again: hidden rows come back, with no blank tail.
tmux resize-window -t "$SESSION" -x 80 -y 20 >/dev/null
tmux_wait_row_count "$SESSION" '^(> |  )n[0-9]{2}' 20
rows=$(input_rows)
if [ "$rows" != "20" ]; then
	echo "expected 20 input rows after growing to 20, got: $rows" >&2
	dump_pane "$SESSION"
	exit 1
fi
assert_pane_lacks "$SESSION" 'n20'
if [ "$(prompt_rows)" != "1" ]; then
	echo "prompt appeared $(prompt_rows) times, want 1" >&2
	dump_pane "$SESSION"
	exit 1
fi

# 5) And the cursor is on screen, on the row it claims.
cursor=$(tmux_cursor "$SESSION")
cy=${cursor##* }
if [ "$cy" != "0" ]; then
	echo "cursor should be on the prompt row (0), got: $cy" >&2
	dump_pane "$SESSION"
	exit 1
fi

echo "PASS: overflow_alt"
