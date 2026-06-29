#!/usr/bin/env bash
# tests/tmux/scroll_multiline.sh - Regression test for multiline horizontal
# scroll. With a 40-col window (content width = 38 after the 2-char prompt),
# typing more characters than fit on one row must NOT spill into subsequent
# rows: the cursor's logical line scrolls horizontally instead.

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_scroll_$$
BIN="$TMUX_BIN_DIR/tmux_textinput_multi"

trap 'tmux_kill "$SESSION"' EXIT

tmux_start "$SESSION" 40 10 "$BIN"

# Wait until the prompt has rendered. tmux capture-pane strips trailing
# whitespace so we anchor the regex to the start of a line.
tmux_wait_for "$SESSION" '^>'

# Type 78 'a's followed by a sentinel 'X'. 78 + the 2-char prompt = 80, more
# than the 40-col pane can fit; the cursor is at codepoint 79 of its line.
# The sentinel doubles as a sync marker: when 'X' is rendered we know the
# entire send-keys batch has been processed.
tmux_send "$SESSION" "$(printf 'a%.0s' $(seq 78))X"
tmux_wait_for "$SESSION" 'X'

# 1) Cursor must be inside the 40-col window. Pre-fix it would have been
#    placed off-screen; tmux clamps the column but the pane row would still
#    show wrapped content (see check #2).
assert_cursor_x_lt "$SESSION" 40

# 2) The visible line must NOT have spilled onto row 2 — that is the symptom
#    the user reported ("typing in the dark"). Pre-fix, with autowrap on,
#    the trailing chars wrap; post-fix, the line scrolls horizontally and
#    rows 2+ stay empty.
row2=$(tmux_capture "$SESSION" | sed -n '2p')
if [ -n "$row2" ]; then
    echo "row 2 should be empty, got: '$row2'" >&2
    dump_pane "$SESSION"
    exit 1
fi

# 3) The sentinel 'X' must be on row 1 (the cursor's logical line), proving
#    the most recent character is visible.
row1=$(tmux_capture "$SESSION" | sed -n '1p')
case "$row1" in
    *X) ;;
    *)
        echo "row 1 must end with sentinel 'X', got: '$row1'" >&2
        dump_pane "$SESSION"
        exit 1
        ;;
esac

# 4) Press C-a (move cursor to start of line). Offsets must reset to 0 and
#    the leading '>' prompt must reappear.
tmux_send "$SESSION" C-a
tmux_wait_for "$SESSION" '^> aaaaaaaaaa'

# 5) After C-a, cursor must land just past the 2-char prompt.
assert_cursor_x_lt "$SESSION" 4

echo "PASS: scroll_multiline"
