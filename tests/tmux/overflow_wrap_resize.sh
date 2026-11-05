#!/usr/bin/env bash
# tests/tmux/overflow_wrap_resize.sh - soft wrap re-flows on resize.
#
# One long logical line, WRAP mode, alt screen at row 1. A width change
# re-flows the visual rows: the cursor must stay on the glyph it was on, the
# row count must follow, and no stale row may survive below the frame.

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_overflow_wrap_$$
BIN="$TMUX_BIN_DIR/tmux_textinput_overflow"

trap 'tmux_kill "$SESSION"' EXIT

# Rows of the frame: the prompt row plus its indented continuations.
frame_rows()
{
	tmux_capture "$SESSION" | grep -cE '^(> |  )a' || true
}

prompt_rows()
{
	tmux_capture "$SESSION" | grep -cE '^> ' || true
}

tmux_start "$SESSION" 80 20 "$BIN" --alt --wrap --lines 0

tmux_wait_for "$SESSION" '^>'

# 200 cells of content, 2 for the prompt: at width 80 the content width is
# 78, so 200 cells need ceil(200/78) = 3 rows.
a200=$(awk 'BEGIN { for (i = 0; i < 200; i++) printf "a"; }')
tmux_send "$SESSION" "$a200"
tmux_wait_row_count "$SESSION" '^(> |  )a' 3

rows80=$(frame_rows)
if [ "$rows80" != "3" ]; then
	echo "width 80: expected 3 frame rows, got: $rows80" >&2
	dump_pane "$SESSION"
	exit 1
fi
cursor=$(tmux_cursor "$SESSION")
cy=${cursor##* }
if [ "$cy" != "2" ]; then
	echo "width 80: cursor should be on row 2, got: $cy" >&2
	dump_pane "$SESSION"
	exit 1
fi

# Narrower: content width 38, so 200 cells need ceil(200/38) = 6 rows.
tmux resize-window -t "$SESSION" -x 40 -y 20 >/dev/null
tmux_wait_row_count "$SESSION" '^(> |  )a' 6
rows40=$(frame_rows)
if [ "$rows40" != "6" ]; then
	echo "width 40: expected 6 frame rows, got: $rows40" >&2
	dump_pane "$SESSION"
	exit 1
fi
cursor=$(tmux_cursor "$SESSION")
cy=${cursor##* }
if [ "$cy" != "5" ]; then
	echo "width 40: cursor should be on row 5, got: $cy" >&2
	dump_pane "$SESSION"
	exit 1
fi
if [ "$(prompt_rows)" != "1" ]; then
	echo "prompt appears $(prompt_rows) times, want 1" >&2
	dump_pane "$SESSION"
	exit 1
fi

# Wider again: back to three rows, with no leftover row below the frame.
tmux resize-window -t "$SESSION" -x 80 -y 20 >/dev/null
tmux_wait_row_count "$SESSION" '^(> |  )a' 3
rows80=$(frame_rows)
if [ "$rows80" != "3" ]; then
	echo "back at width 80: expected 3 frame rows, got: $rows80" >&2
	dump_pane "$SESSION"
	exit 1
fi
leftover=$(tmux_capture "$SESSION" | sed -n '4p')
if [ -n "$leftover" ]; then
	echo "row 4 should be blank after re-flow, got: '$leftover'" >&2
	dump_pane "$SESSION"
	exit 1
fi
cursor=$(tmux_cursor "$SESSION")
cy=${cursor##* }
if [ "$cy" != "2" ]; then
	echo "back at width 80: cursor should be on row 2, got: $cy" >&2
	dump_pane "$SESSION"
	exit 1
fi

echo "PASS: overflow_wrap_resize"
