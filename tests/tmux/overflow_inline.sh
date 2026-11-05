#!/usr/bin/env bash
# tests/tmux/overflow_inline.sh - Content taller than the screen, inline mode.
#
# TUI_RENDER_INLINE with a FLOW-sized textinput. The runtime moves the cursor
# up by the frame's newline count before repainting, so a frame taller than
# the terminal used to strand its top rows in scrollback and repaint from
# mid-frame: the prompt duplicated and walked up the screen.
#
# Asserts:
#   - the frame never has more rows than the terminal has
#   - the prompt appears exactly once after width and height resizes
#   - shrinking the window BELOW the old frame height leaves no duplicated
#     prompt and no stale rows (the runtime clamps its cursor-up to the
#     screen and erases from the frame top down in that case)

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_overflow_inline_$$
BIN="$TMUX_BIN_DIR/tmux_textinput_overflow"

trap 'tmux_kill "$SESSION"' EXIT

# Every prefilled line is nNN; the frame's rows are the prompt row and its
# indented continuations.
ROW_RE='^(> |  )n[0-9]{2}'

prompt_rows()
{
	tmux_capture "$SESSION" | grep -cE '^>' || true
}

input_rows()
{
	tmux_capture "$SESSION" | grep -cE "$ROW_RE" || true
}

check_single_prompt()
{
	local got
	got=$(prompt_rows)
	if [ "$got" != "1" ]; then
		echo "prompt appears $got times, want 1" >&2
		dump_pane "$SESSION"
		exit 1
	fi
}

tmux_start "$SESSION" 60 12 "$BIN" --inline --lines 30

tmux_wait_for "$SESSION" 'n29'
tmux_wait_row_count "$SESSION" "$ROW_RE" 12

# 1) One screenful of rows, not thirty.
assert_pane_contains "$SESSION" 'n29'
assert_pane_lacks "$SESSION" 'n00'

# 2) Back to the top: the prompt is painted once, and nothing stale sits
#    below the frame.
tmux_send "$SESSION" C-Home
tmux_wait_for "$SESSION" '^> n00'
check_single_prompt
assert_pane_lacks "$SESSION" 'n12'

# 3) Width resize: a narrower terminal re-flows nothing in CLIP mode, but the
#    repaint must not duplicate the prompt.
tmux resize-window -t "$SESSION" -x 30 -y 12 >/dev/null
tmux_wait_row_count "$SESSION" "$ROW_RE" 12
check_single_prompt

# 4) Height shrink BELOW the old frame height: the old frame's top rows have
#    already scrolled away, so the repaint must not move up further than the
#    screen allows nor draw a second prompt.
tmux resize-window -t "$SESSION" -x 30 -y 4 >/dev/null
tmux_wait_row_count "$SESSION" "$ROW_RE" 4
check_single_prompt
assert_pane_contains "$SESSION" 'n00'

# 5) Grow back: the frame grows again, still with a single prompt and no
#    duplicated block from the old frame.
tmux resize-window -t "$SESSION" -x 60 -y 14 >/dev/null
tmux_wait_row_count "$SESSION" "$ROW_RE" 14
check_single_prompt
assert_pane_lacks "$SESSION" 'n14'

echo "PASS: overflow_inline"
