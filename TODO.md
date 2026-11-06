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
  own focus). Reconsider if multiple consumers duplicate
  the same cycling boilerplate.

## Architecture

- **Shared selection abstraction.** Viewport tracks `(visual_line,
display_col)`; textinput tracks `cursor_byte`. They only share the
  `TUI_CMD_CLIPBOARD_COPY` pipeline. Revisit only if a third consumer
  with the same shape appears.

## FIXED — one paint is four writes (ConPTY caret hop)

Fixed 2026-10-19 by the **paint** transaction: `paint_depth` + `out_stage`
on `TuiRuntime`, `runtime_emit()` staging, and ONE delivery at the
outermost close; terminal-like sinks are delivered through the fd so stdio
buffering cannot split a paint. Asserts on `out_write_count`, not bytes.
`6acd0b5` kept; the per-frame `?25l`/`?25h` pair must never come back.
Diagnosis and the residual work: a Windows pseudoconsole harness that logs
`ReadFile` boundaries (nothing in CI can see the caret). The rationale is
in the Paint section of `src/runtime.c`.

## Reconciliation architecture (step 1 landed, step 2 open)

`tui_runtime_flush()` is now `paint_begin → runtime_reconcile() →
runtime_paint() → paint_end`: the reconcile step decides the frame's meaning
(commit pass, `view()`, capability probe) before a byte leaves, and the paint
emits + delivers it. The mode/cursor diffs are still computed _while_
emitting — the next step is to make reconcile produce a real terminal-state
plan (transitions, capability gating, per-row placement) that the paint
executes mechanically, so "reconcile" is a pure function of (model, tracked
state) that tests can call on its own.

## Streaming transcript (step 3+)

The transcript component landed in `include/boba/stream.h`; the terminal
capability probe and the IMAGE commit gate landed with it in
`terminal_profile.h`. The probe's completion signal is the deadline,
never a single reply (FIFO answers arrive in one burst; resolving on DA1
would discard the cell size / XTVERSION that follow). Deferred to the
next steps:

- **Image transport — DONE (2026-09-29, with nevermore's IR step 5)**:
  `TuiImageSpec` frozen (transport/format enums, borrowed payload,
  display size in CELLS as the row reservation), kitty APC (`f=100` PNG,
  `q=2`, `C=1` — the terminal must not move the cursor, our row
  terminators own the geometry) with 4096-byte `m=1/m=0` chunking,
  iTerm2 `OSC 1337 File=inline=1` (PNG/JPEG/GIF), `tui_row_image`'s row
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

## textinput overflow (follow-ups)

The vertical window landed: one layout, one emitter, `NATURAL` / `FIXED` /
`FLOW` sizing, `tui_textinput_scroll_state()`, PgUp/PgDn, and the runtime's
inline cursor-up clamp. Deliberately left for later:

- **Adopt `src/fit.h` in viewport and statusline.** Both still take an
  app-computed height/width. The helper exists precisely so the screen
  clamp is written once; the migration is mechanical but touches their
  render paths.
- **Incremental layout.** `layout_get()` rebuilds from the whole text on
  any edit. A dirty-from byte (the first edited logical line) would make a
  megabyte paste cost one row rebuild instead of two full walks.
- **Detached scrolling** (mouse wheel, Ctrl+Up/Down) needs a "cursor
  off-window" state, and so does mouse drag-to-select: both want the
  screen-to-byte map the layout can now answer. Design them together.
- **Bottom docking.** Chat apps compute `row = term_h - H + 1` after every
  edit. Now that the component knows `term_h` and H, a
  `tui_textinput_dock(BOTTOM)` helper could own that arithmetic.
- **`TuiView.max_inline_rows`.** A runtime-side clip would protect a
  component that does not self-limit. It cannot replace a component's own
  decision (H must be known _before_ painting to choose the top row), so
  it stays a safety net only.
- **In-component hidden-row indicators.** Apps draw "↑ 12 more" from
  `scroll_state()` today. An in-component variant would spend a content row
  and needs its own placement policy; not worth it until a second consumer
  asks.
- **`tui_textinput_set_placement(0)` means INLINE.** The geometry reset
  retired the `terminal_row == 0` sentinel from the struct but this one-line
  setter kept it, so an app that computes `row = term_h - rows` on a screen
  too short for its own frame gets inline placement instead of an absolute
  row (mudlark guards `row >= 1` by hand). It is documented in the header;
  the fix is to deprecate the setter (`BOBA_DEPRECATED`) in favour of
  `set_geometry()` with an explicit `place`, or to reject `row == 0`
  outright.
- **`FIXED` with no `width` does no clipping and no wrapping.** A zero width
  means "no constraint", so a 30-column line is emitted whole on a 20-column
  terminal even with `term_w` known. That is the horizontal mirror of the
  vertical clamp question, and the answer is the same one chosen there:
  document it (done in `textinput.h`), rather than have `FIXED` partly follow
  the screen and blur what it means.
- **`TuiTextInputConfig.geometry` is copied verbatim**, so a config that
  names one field (`{ .geometry = { .width = 20 } }`) also zeroes `sizing`,
  `place` and `scrolloff`. `tui_textinput_create()` lifts a zeroed
  `scrolloff` back to 1, but a `sizing`/`place` the caller meant to keep is
  not restored. Harmless today (NATURAL/INLINE are the defaults) and a sharp
  edge for the next field added. The robust fix is the contract the other
  geometry fields already have — the zero value of every field means the
  default — so no fix-up pass is needed at all.

## Declarative API alignment

- **Declarative replacements for textinput/viewport setters.** The
  bulk of `tui_textinput_set_*()` / `tui_viewport_set_*()` setters
  (`set_history_size`, `set_show_dividers`, `set_echo_mode`,
  `set_word_chars`, etc.) are imperative and don't yet have a declarative
  equivalent. Textinput's size/placement already moved to the declarative
  `TuiTextInputGeometry`; the rest needs the same treatment: which
  init-time options grow on `TuiTextInputConfig`, where render-time
  positioning lives (likely new `TuiView` fields or parent-dispatched
  messages), and which mid-life mutations become message-driven vs. stay
  as setters. Once designed, these setters become deprecation candidates.
  Tracked separately from the v2-alignment plan because it's API design,
  not a mechanical attribute pass.
- **Opaque component structs, project-wide.** `TuiTextInput` is the pilot
  (state read through accessors only). `TuiViewport`, `TuiStatusLine` and
  `TuiListPopup` still expose their fields; migrating them the same way
  removes the "field order is an API" constraint everywhere.
