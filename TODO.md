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
  belongs in the consumer (bloom-telnet, bloom-lisp), worth documenting.

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

## v2 alignment (deferred from declarative-View migration)

Each closes a gap with the Charm v2 reference (see
`https://charm.land/blog/v2/`). None block usage. Cosmetic v2 renames
(`Type → Code`, `Runes → Text`) intentionally skipped — current names
read better in C.

- **Key press/release split.** Add `action: PRESS | RELEASE` to
  `TuiKeyMsg` and parser support for Kitty keyboard-protocol release
  events (parser handles release for mouse only today).

- **Bracketed paste.** Detect `ESC [ 200 ~ … ESC [ 201 ~` and emit
  `TUI_MSG_PASTE_START` / `TUI_MSG_PASTE` / `TUI_MSG_PASTE_END`.
  Components opt in via the existing `TuiView.bracketed_paste` field.

- **Mouse message audit.** Buttons and `action` are already v2-aligned.
  Verify nothing's missing against `MouseClickMsg` / `MouseReleaseMsg` /
  `MouseMotionMsg` / `MouseWheelMsg` — likely no work, just confirm.

- **Lipgloss-equivalent.** `TuiColor`, `TuiStyle`, and
  `Render` / `Place` / `Join` / `Border` primitives. bloom-boba is the
  all-in-one (bubbletea + lipgloss + bubbles), so this lives here, not
  in a separate repo.

- **Bubbles parity audit.** Once-over for obviously-missing capabilities:
  viewport soft wrap, focused/blurred style separation in textinput,
  textarea virtual-cursor / scroll helpers. Defer until a concrete use
  case appears.
