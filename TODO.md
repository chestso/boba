# TODO

Deferred work surfaced during the focus + selection + clipboard effort.
None are blockers — they extend or polish what's already in place.

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

- **Image transport** (`TuiImageSpec`, `tui_row_image`): kitty APC,
  sixel and iTerm2 encoders behind the sink; row reservation sized
  from the profile. (`tui_row_image` is currently a no-op; nevermore's
  renderer degrades IMAGE blocks to their text.)

- **Commit payload cap**: a single block >cap force-commits a safe
  prefix at frozen widths (the one case the raw-buffer watermark
  cannot bound). Correctness is unaffected; memory bound only.

- **Live-row budget partition between streams**: at most one stream
  grows at a time on every observed wire, so the growing stream takes
  the whole budget today; revisit if an interleaving provider shows up
  (the interleaved test already guards ordering).

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
