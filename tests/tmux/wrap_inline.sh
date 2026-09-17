#!/usr/bin/env bash
# tests/tmux/wrap_inline.sh - Regression test for inline-mode soft-wrap.
#
# The nevermore/ditty REPL shape: a soft-wrapping multiline textinput in
# TUI_RENDER_INLINE (no alt screen, no terminal_row). The runtime counts the
# inline frame height by '\n', so a logical line that softly wraps must be
# emitted as explicit rows — otherwise the tracked and painted row counts
# disagree and the cursor drifts one row up per keystroke until the prompt
# walks off the top of the screen. The mini-app prefills six lines so the
# frame starts below the top edge, where that drift is observable.

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_wrap_inline_$$
BIN="$TMUX_BIN_DIR/tmux_textinput_inline"

trap 'tmux_kill "$SESSION"' EXIT

tmux_start "$SESSION" 40 12 "$BIN"

# Wait until the prompt has rendered below the prefill.
tmux_wait_for "$SESSION" '^>'

# The prompt must start on pane row 7 (six prefill lines, then the frame).
prompt_row_before=$(tmux_capture "$SESSION" | grep -n '^>' | head -1 | cut -d: -f1)
if [ "$prompt_row_before" != "7" ]; then
	echo "prompt should start on row 7, got: $prompt_row_before" >&2
	dump_pane "$SESSION"
	exit 1
fi

# Type 60 'a's plus a sentinel 'X' (61 content cells). With a 2-char prompt
# and a 40-col window, 38 cells fit on the first row; the remaining 23 wrap.
a60=$(awk 'BEGIN { for (i = 0; i < 60; i++) printf "a"; }')
a38=$(awk 'BEGIN { for (i = 0; i < 38; i++) printf "a"; }')
a22=$(awk 'BEGIN { for (i = 0; i < 22; i++) printf "a"; }')

tmux_send "$SESSION" "${a60}X"
tmux_wait_for "$SESSION" 'X'

# 1) Row 1 carries the wrapped first chunk; row 2 the indented remainder.
row1=$(tmux_capture "$SESSION" | sed -n '7p')
if [ "$row1" != "> $a38" ]; then
	echo "row 7 unexpected: '$row1'" >&2
	dump_pane "$SESSION"
	exit 1
fi
row2=$(tmux_capture "$SESSION" | sed -n '8p')
if [ "$row2" != "  ${a22}X" ]; then
	echo "row 8 unexpected: '$row2'" >&2
	dump_pane "$SESSION"
	exit 1
fi

# 2) The frame must not have drifted: the prompt is still on row 7 and the
#    prefill line is intact. Pre-fix, each keystroke moved the frame up one
#    row, overwriting 'pre1'.
prompt_row_after=$(tmux_capture "$SESSION" | grep -n '^>' | head -1 | cut -d: -f1)
if [ "$prompt_row_after" != "7" ]; then
	echo "prompt drifted to row $prompt_row_after (was 7)" >&2
	dump_pane "$SESSION"
	exit 1
fi
assert_pane_contains "$SESSION" 'pre1'

# 3) The cursor sits on the wrapped (second) visual row of the frame.
cursor=$(tmux_cursor "$SESSION")
cy=${cursor##* }
if [ "$cy" != "7" ]; then
	echo "cursor_y should be 7 (second frame row), got: $cy" >&2
	dump_pane "$SESSION"
	exit 1
fi
assert_cursor_x_lt "$SESSION" 40

# 4) Backspace back onto a single row: the vacated row must be cleared and
#    the prompt must still sit on row 7. 61 - 40 = 21 content cells remain.
for _ in $(seq 1 40); do
	tmux_send "$SESSION" BSpace
done
a21=$(awk 'BEGIN { for (i = 0; i < 21; i++) printf "a"; }')
tmux_wait_for "$SESSION" "^> ${a21}\$"

row2=$(tmux_capture "$SESSION" | sed -n '8p')
if [ -n "$row2" ]; then
	echo "row 8 should be cleared after shrink, got: '$row2'" >&2
	dump_pane "$SESSION"
	exit 1
fi
prompt_row_final=$(tmux_capture "$SESSION" | grep -n '^>' | head -1 | cut -d: -f1)
if [ "$prompt_row_final" != "7" ]; then
	echo "prompt drifted to row $prompt_row_final (was 7)" >&2
	dump_pane "$SESSION"
	exit 1
fi

echo "PASS: wrap_inline"
