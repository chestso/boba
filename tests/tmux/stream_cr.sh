#!/usr/bin/env bash
# tests/tmux/stream_cr.sh - a carriage return replaces the open row, on a
# real terminal.
#
# The unit tests assert the bytes boba writes; this asserts what a
# terminal emulator DRAWS from them (tmux capture-pane), because the
# rewind is cursor arithmetic (up N rows, clear, rewrite) and the only
# honest check of cursor arithmetic is the screen it produces. The
# mini-app posts one piece of output per key under an inline prompt row.

set -eu

THIS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$THIS_DIR/lib.sh"

SESSION=boba_stream_cr_$$
BIN="$TMUX_BIN_DIR/tmux_stream_cr"

trap 'tmux_kill "$SESSION"' EXIT

tmux_start "$SESSION" 40 14 "$BIN"
tmux_wait_for "$SESSION" '^> ready'

# Pane rows, as one string per line, trailing blanks trimmed.
rows() { tmux_capture "$SESSION" | sed -e 's/[[:space:]]*$//'; }

# The prompt row, and the row directly above it.
prompt_n() { rows | grep -n '^> ready$' | head -1 | cut -d: -f1; }
above_prompt() { rows | sed -n "$(($(prompt_n) - 1))p"; }

fail() {
	echo "$1" >&2
	dump_pane "$SESSION"
	exit 1
}

# 1) one frame
tmux_send "$SESSION" a
tmux_wait_for "$SESSION" '^10%$'
[ "$(above_prompt)" = "10%" ] || fail "10% should sit right above the prompt"

# 2) the next frame REPLACES it: one progress row, never two
tmux_send "$SESSION" b
tmux_wait_for "$SESSION" '^50%$'
assert_pane_lacks "$SESSION" '10%'
[ "$(above_prompt)" = "50%" ] || fail "50% should have replaced 10%"
[ "$(rows | grep -c '%')" = "1" ] || fail "exactly one progress row expected"

# 3) a lone CR changes nothing on screen. It emits no bytes, so what proves
#    it was PROCESSED is the next frame landing on the SAME row: had the CR
#    broken the row (the legacy reading), "10%" would be a second row below
#    "50%" instead of replacing it. (A sleep would only guess.)
tmux_send "$SESSION" p
tmux_send "$SESSION" a
tmux_wait_for "$SESSION" '^10%$'
assert_pane_lacks "$SESSION" '50%'
[ "$(above_prompt)" = "10%" ] || fail "a lone CR must leave the row intact"
[ "$(rows | grep -c '%')" = "1" ] || fail "a lone CR must not start a new row"

# 4) the final frame ends the row. The meter row already sat above the
#    prompt, so the ending replaces it IN PLACE and the prompt does not move.
before=$(prompt_n)
tmux_send "$SESSION" c
tmux_wait_for "$SESSION" '^100%$'
assert_pane_lacks "$SESSION" '50%'
[ "$(prompt_n)" = "$before" ] || fail "the prompt must not move: the row was already open"
[ "$(above_prompt)" = "100%" ] || fail "100% should end the row"

# 5) the rows above the meter are undamaged by all of that
rows | grep -q '^pre1$' || fail "pre1 damaged"
rows | grep -q '^pre4$' || fail "pre4 damaged"

# 6) a frame that soft-wrapped over three rows is replaced as a whole
tmux_send "$SESSION" w
tmux_wait_for "$SESSION" '^w{40}$'
[ "$(rows | grep -c '^w')" = "3" ] || fail "expected the wrapped row on three screen rows"
tmux_send "$SESSION" s
tmux_wait_for "$SESSION" '^short$'
[ "$(rows | grep -c 'w')" = "0" ] || fail "no 'w' may remain after the replace"
[ "$(above_prompt)" = "short" ] || fail "short should sit right above the prompt"
[ "$(rows | sed -n "$(($(prompt_n) - 2))p")" = "100%" ] || fail "the ended row above must be intact"

# 7) an attribute set after the CR applies to the new frame, and the
#    replaced text is gone
tmux_send "$SESSION" z
tmux_wait_for "$SESSION" '^green$'
assert_pane_lacks "$SESSION" 'short'
green=$(tmux capture-pane -t "$SESSION" -p -e | grep 'green' | head -1)
case "$green" in
*"[32m"*) ;;
*) fail "green frame lost its colour: $green" ;;
esac

# 8) the default message still breaks rows on a bare CR
tmux_send "$SESSION" n
tmux_send "$SESSION" l
tmux_wait_for "$SESSION" '^break$'
rows | grep -q '^legacy$' || fail "legacy CR must break the row, keeping 'legacy'"

# 9) a colour left OPEN at the end of the app's row does not bleed into the
#    frame below it: every committed write ends in the default state. The
#    row itself keeps its colour (that is the stream's state, faithfully).
tmux_send "$SESSION" r
tmux_wait_for "$SESSION" '^bleed$'
attributed=$(tmux capture-pane -t "$SESSION" -p -e)
bleed=$(printf '%s\n' "$attributed" | grep 'bleed' | head -1)
case "$bleed" in
*"[31m"*) ;;
*) fail "the bleed row lost its colour: $bleed" ;;
esac
prompt_line=$(printf '%s\n' "$attributed" | grep '> ready' | head -1)
case "$prompt_line" in
*"[31m"*) fail "an open colour bled into the frame: $prompt_line" ;;
esac

echo "PASS: stream_cr"
