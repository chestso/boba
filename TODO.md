# TODO

Deferred work surfaced during the focus + selection + clipboard effort.
None of these are blockers — they extend or polish what's already in place.

## Selection / clipboard

- **Yank from system clipboard.** `Ctrl-Y` in textinput still pulls from
  the local `kill_buf` only. To pull from the OS clipboard, the runtime
  needs either:
  - an OSC 52 _query_ (`ESC ] 52 ; c ; ? ESC \`, terminal responds with
    base64 contents asynchronously — needs an inbound parser path), or
  - a new `paste_handler` callback on `TuiRuntimeConfig` that lets the
    app shell out to `xclip -o` / `wl-paste` / `pbpaste`.
    Probably ship the callback first; the OSC 52 query path is finicky and
    poorly supported.

- **Mouse drag-to-select inside textinput.** Currently keyboard-only.
  Would need to map mouse `(row, col)` to byte offsets — straightforward
  for single-line, more involved for multiline mode where the existing
  cursor row/col cache helps.

- **Transient-mark-mode behavior** (modern emacs default). Today motion
  preserves the mark; selection stays visible until cleared explicitly.
  An option to flip this — motion deactivates the mark, only Shift-motion
  extends — would match graphical emacs more closely. Requires the parser
  to emit shift-modified arrow keys consistently.

- **`C-x C-x` swap point and mark** in textinput (and viewport). Trivial
  once chord-prefix parsing exists; textinput already has a `ctrl_x_prefix`
  state.

- **OSC 52 size cap / chunking.** `ansi_format_osc52()` writes one large
  buffer; very large clipboards (>~64 KB) can hit terminal line-length
  limits and get silently truncated. Either cap with a documented limit
  (8 KB seems standard) or chunk across multiple OSC 52 emissions if any
  terminal supports concatenation.

- **Terminal OSC 52 capability detection.** Auto-pick OSC 52 vs.
  `clipboard_handler` based on `$TERM` / `$TERM_PROGRAM` so apps don't
  have to. Probably belongs in the consumer (bloom-telnet, bloom-lisp)
  rather than the library, but worth documenting a recommended pattern.

## Focus

- **Focus indicator in viewport rendering.** A subtle visual cue when the
  viewport is focused (left-margin glyph, border tint, status bar entry).
  Made opt-in stub-style in the original plan, then dropped because apps
  typically signal focus via their own status bar. Revisit if a use case
  shows up.

- **`TuiFocusGroup` helper.** Explicitly decided against during planning
  (matches Bubbletea precedent: parents own focus). If multiple consumers
  start duplicating the same focus-cycling boilerplate, a small optional
  helper struct (`add` / `cycle` / `dispatch`) might pay for itself.

## Architecture

- **Shared selection abstraction across components.** Viewport tracks
  `(visual_line, display_col)`; textinput tracks `cursor_byte`. The two
  do not share state at the code level — only the `TUI_CMD_CLIPBOARD_COPY`
  pipeline. Revisit only if a third consumer of selection appears with
  the same shape as one of the existing two.

## v2 alignment (deferred from declarative-View migration)

These were called out as out-of-scope when the declarative `TuiView`
landed. None block usage; each closes a gap with the Charm v2 reference
(see `https://charm.land/blog/v2/` and the per-repo upgrade guides).

- **Key message press/release split.** Bubbletea v2 splits `KeyMsg`
  into `KeyPressMsg` / `KeyReleaseMsg`. In C the natural shape is an
  `action: PRESS | RELEASE` field on `TuiKeyMsg` plus parser support
  for Kitty keyboard-protocol release events (today the parser only
  handles release for mouse). Cosmetic v2 renames (`Type → Code`,
  `Runes → Text`) intentionally skipped — current names read better
  in C.

- **Bracketed paste messages.** Parser doesn't handle
  `ESC [ 200 ~ … ESC [ 201 ~` today. Add detection and emit
  `TUI_MSG_PASTE_START`, `TUI_MSG_PASTE` (carrying buffered text),
  `TUI_MSG_PASTE_END`. Components opt in via the
  `TuiView.bracketed_paste` field that already exists.

- **Mouse message audit.** Button constants are already v2-aligned
  (`TUI_MOUSE_LEFT`, `TUI_MOUSE_WHEEL_UP`); the `action` field carries
  press/release/motion. Verify nothing's missing against Bubbletea v2's
  `MouseClickMsg` / `MouseReleaseMsg` / `MouseMotionMsg` /
  `MouseWheelMsg` interface split — likely no work, just confirm.

- **Lipgloss-equivalent.** A C library for color/style/layout in the
  Lipgloss v2 shape (Style values, `image/color.Color`-equivalent,
  `Place`/`Join`/`Border` primitives). Out of scope for bloom-boba —
  belongs in a separate `bloom-lipgloss` (or similarly named) project.

- **Bubbles parity audit.** Bubbles v2 added textarea improvements
  (virtual cursor mode, scroll position helpers), viewport features
  (soft wrap, left gutter, highlighting), and styling state separation
  (`Styles.Focused` / `Styles.Blurred`). bloom-boba's textinput /
  viewport don't have to mirror everything, but a once-over for
  obviously-missing capabilities — soft wrap in viewport,
  focused/blurred style separation in textinput — could pay for
  itself. Defer until a concrete use case appears.
