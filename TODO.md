# TODO

Deferred work surfaced during the focus + selection + clipboard effort.
None are blockers — they extend or polish what's already in place.

## Keyboard protocol

The kitty keyboard protocol is wired: `TuiView.kbd_enhancements` is a
bitmask of flags 1/8/16, pushed onto the terminal's stack (popped on
change and at stop, in both render modes), the probe asks `CSI ? u` and
reports `TuiTerminalProfile::kbd_protocol` / `kbd_flags`, Ctrl+C / Ctrl+D
keep their INTERRUPT / EOF verdicts under the protocol, and the parser
decodes the associated text (flag 16) into `TuiKeyMsg::text` — so a
consumer can declare all three flags: Shift+Enter (`CSI 13;2u`), capitals
from the reported text, and IME results.

- **Event types (flag 2) are not requested** — releases and repeats would
  arrive as sub-fields the parser already reads (`:1`/`:2`/`:3`), but no
  consumer wants them yet, and the textinput ignores releases.

## Selection / clipboard

- **System-clipboard yank.** `Ctrl-Y` in textinput pulls from the local
  `kill_buf` only. Add a `paste_handler` callback on `TuiRuntimeConfig`
  (apps shell out to `xclip -o` / `wl-paste` / `pbpaste`). The OSC 52
  query path (`ESC ] 52 ; c ; ? ESC \`) is finicky and poorly supported —
  defer.

- **OSC 52 polish.** (a) Cap or chunk `ansi_format_osc52()` output;
  large clipboards (>~64 KB) hit terminal line-length limits and get
  silently truncated (8 KB is the common cap). (b) Auto-pick OSC 52 vs.
  `clipboard_handler` based on `$TERM` / `$TERM_PROGRAM` — probably
  belongs in the consumer (mudlark, ditty), worth documenting.

- **Mouse drag-to-select in textinput.** Map mouse `(row, col)` to byte
  offsets — straightforward for single-line, harder in multiline where
  the cursor row/col cache helps.

- **Transient-mark-mode option.** Today motion preserves the mark.
  Optional flip — motion deactivates, only Shift-motion extends — to
  match graphical emacs. Needs consistent shift-modified arrows from
  the parser.

- **`C-x C-x` swap point and mark** in textinput / viewport. Trivial
  once chord-prefix parsing exists; textinput already has `ctrl_x_prefix`.

## Focus

- **Viewport focus indicator.** Subtle visual cue (margin glyph, border
  tint, status entry). Dropped because apps signal focus via their own
  status bar; revisit if a use case shows up.

- **`TuiFocusGroup` helper.** Decided against during planning (parents
  own focus, per Bubbletea). Reconsider if multiple consumers duplicate
  the same cycling boilerplate.

## Architecture

- **Shared selection abstraction.** Viewport tracks `(visual_line,
display_col)`; textinput tracks `cursor_byte`. They only share the
  `TUI_CMD_CLIPBOARD_COPY` pipeline. Revisit only if a third consumer
  with the same shape appears.

## Streaming transcript (step 3+)

The transcript component landed in `include/boba/stream.h` (see
AGENTS.md, "Streaming transcript"); the terminal capability probe and
the IMAGE commit gate landed with it in `terminal_profile.h`. The
probe's completion signal is the deadline, never a single reply (FIFO
answers arrive in one burst; resolving on DA1 would discard the cell
size / XTVERSION that follow). Deferred to the next steps:

- **Image transport — DONE (2026-09-29, with nevermore's IR step 5,
  see nevermore's `docs/TRANSCRIPT-IMAGE-PLAN.md`)**: `TuiImageSpec`
  frozen (transport/format enums, borrowed payload, display size in
  CELLS as the row reservation), kitty APC (`f=100` PNG, `q=2`,
  `C=1` — the terminal must not move the cursor, our row terminators
  own the geometry) with 4096-byte `m=1/m=0` chunking, iTerm2
  `OSC 1337 File=inline=1` (PNG/JPEG/GIF), `tui_row_image`'s row
  accounting, `blk.image_id` boba-assigned (monotonic across clear),
  the profile copied into the transcript per commit pass, and
  `emit_unit`'s measure/render_image branch with a DEFER queue: an
  IMAGE unit frozen before the profile resolves (and everything
  freezing behind it, byte staging and row closes included) is held
  in freeze order and replayed after the gate's verdict — emission
  is freeze-time, the profile is commit-time, and deferral is what
  keeps guarantee 4 through the gap. `TUI_TRANSCRIPT_STAGED_CAP`
  1→4 MiB (above the app policy's worst legal batch). sixel needs
  client-side pixel decode and stays deferred (the pixel tier:
  sixel + halfblocks + kitty `f=32`).

- **Commit payload cap**: a single block >cap force-commits a safe
  prefix at frozen widths (the one case the raw-buffer watermark
  cannot bound). Correctness is unaffected; memory bound only.

- **Live-row budget partition between streams**: at most one stream
  grows at a time on every observed wire, so the growing stream takes
  the whole budget today; revisit if an interleaving provider shows up
  (the interleaved test already guards ordering).

## Status line component

The status row is now `TuiStatusLine` (`include/boba/components/statusline.h`):
a declared segment list with left / fill / right packing, `priority` /
`min_cols` elision, an eager layout and a pure `view`. It used to be a field
of `TuiTextInput` (`TuiSpan`, `tui_textinput_set_status_line`), which is
deleted. Deferred:

- **`max_cols` / `pad_right` on a segment**: no caller. `pad_left` covers
  the one gap nevermore declares, and a bounded segment is a row policy
  nobody has asked for.
- **A tmux e2e app**: the component is unit-tested end to end (declarations
  in, painted bytes out) and exercised through nevermore, but boba's tmux
  apps do not register one, so there is no standalone paint test in a real
  terminal. Cheap to add (a mini-app wrapping the component); do it when
  the next tmux app is touched.
- **Multi-row chrome** (`get_height` > 1): deliberately a different
  component — a taller row breaks the separator contract and the frame
  budget math the one-row shape is built on.

## Declarative API alignment

- **Declarative replacements for textinput/viewport setters.** The
  bulk of `tui_textinput_set_*()` / `tui_viewport_set_*()` setters
  (`set_history_size`, `set_terminal_width`, `set_terminal_row`,
  `set_show_dividers`, `set_echo_mode`, `set_word_chars`, etc.) are
  imperative and don't yet have a declarative equivalent. Designing
  one needs three calls: which init-time options grow on
  `TuiTextInputConfig`, where render-time positioning lives (likely
  new `TuiView` fields or parent-dispatched messages), and which
  mid-life mutations become message-driven vs. stay as setters. Once
  designed, these setters become deprecation candidates. Tracked
  separately from the v2-alignment plan because it's API design, not
  a mechanical attribute pass.
