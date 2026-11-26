# Architecture

How boba works internally. Build, testing and convention detail lives in
`CONTRIBUTING.md`.

## Elm Architecture (Model-View-Update)

Unidirectional data flow: Input bytes → `TuiMsg` → `update(Model, Msg)` →
mutates Model + returns `TuiCmd` → `view(Model)` → `TuiView` (content +
terminal-mode declarations) → runtime reconciles against tracked terminal
state → output bytes.

## Core types

| Type             | File                            | Role                                                            |
| ---------------- | ------------------------------- | --------------------------------------------------------------- |
| `TuiMsg`         | `include/boba/msg.h`            | Tagged union (enum + union) for events                          |
| `TuiCmd`         | `include/boba/cmd.h`            | Tagged union for side effects                                   |
| `TuiComponent`   | `include/boba/component.h`      | Virtual function table: init/update/view/free                   |
| `TuiView`        | `include/boba/component.h`      | Per-frame output: content buffer + terminal-mode flags + cursor |
| `TuiRuntime`     | `include/boba/runtime.h`        | Event loop, terminal management, state reconciliation           |
| `TuiStyle`       | `include/boba/style.h`          | Value-typed style record                                        |
| `DynamicBuffer`  | `include/boba/dynamic_buffer.h` | Auto-growing byte buffer used for rendering                     |
| `TuiInputParser` | `include/boba/input_parser.h`   | Parses raw terminal bytes into TuiMsg structs                   |

`tui.h` is the umbrella header that includes all of the above. Include
`<boba/tui.h>` to get the full public API in one shot.

## Components

Each component lives in `src/components/` with a public header in
`include/boba/components/`. Components expose:

- Direct API: `tui_<name>_create()`, `tui_<name>_free()`,
  `tui_<name>_update()`, `tui_<name>_view()` — for standalone or embedded use.
- Component interface: `tui_<name>_component()` returns `const TuiComponent *`
  — for use with the runtime.

Built-in components: `textinput`, `viewport`, `list_popup`, `statusline`.

**`textinput` reports clipboard events, it does not emit them.** Every kill,
cut, copy and paste request goes to a hook
(`tui_textinput_set_clipboard_hook`) and `update()` returns no
`TUI_CMD_CLIPBOARD_COPY` by default — a kill used to rewrite the user's
system clipboard through OSC 52 without the app asking, and a copy was
unreachable on terminals that drop OSC 52. `tui_textinput_set_clipboard_cmd
(input, 1)` restores the old emission for kills and cuts only; a copy never
becomes a command. Bindings are decided by modifiers, never by the platform
(Cmd/Alt/Ctrl chords resolve in `edit_shortcut()` before the readline
switch); the one platform-flavoured knob, Ctrl+A, is declared by the app.
`make check` covers both encodings of each chord — the component keys off
the message, not the bytes.

**`statusline`** is the odd one: it is pure chrome — one row of app state
(a spinner, a context gauge, a separator rule, a right-aligned identity
block) with a LAYOUT MODEL, and no input of its own. The caller DECLARES
the row as an ordered segment list (`TuiSegment`: text + style + align +
priority + min_cols + pad_left) and the component does the columns:

- `TUI_SEGMENT_LEFT` packs from column 0 in declaration order;
  `TUI_SEGMENT_FILL` repeats its unit to absorb the slack (nevermore's
  separator rule); the first non-LEFT segment starts the right-packed
  group, whose last segment ends at `w - 1`.
- When the declared row is wider than the terminal, `priority` decides
  what gives (higher first; `0` = FIXED, never shrunk) down to each
  segment's `min_cols` floor, then below it to 0 — and a segment at 0
  columns is dropped whole, `pad_left` included. Elision keeps the head,
  cuts on a grapheme boundary and marks the cut with `…`; a segment that
  cannot use its last column (a wide glyph) hands it back to the fills, so
  a row with a fill is always exactly the terminal width.
- The row is composed EAGERLY by the setters into a reused buffer (an
  identical declaration set is a no-op), so `tui_statusline_view()` is a
  pure emit: `\r` + EL + the row, no trailing newline. Placement is the
  composer's job (frame order inline, a CSI position in alt-screen mode) —
  the component carries no `terminal_row`.
- The component is style-agnostic (it paints the caller's styles) and
  cursor-less. `update` handles `TUI_MSG_WINDOW_SIZE` (an eager relayout)
  and ignores everything else.

The row used to be a field of `TuiTextInput` (`status_text` / `TuiSpan` /
`tui_textinput_set_status_line`, deleted): a text widget owned chrome it
could not measure, and the row sat inside the input's height and cursor
arithmetic. A composing app that paints a status row now adds that row to
the cursor row it declares (`tui_textinput_cursor_pos` returns the INPUT's
rows).

## Component composition

The runtime manages ONE model. Composition happens inside that model — a
parent component embeds children as struct fields and routes messages
manually:

```c
typedef struct {
    TuiModel base;
    TuiViewport *viewport;    /* child: scrollable output */
    TuiTextInput *textinput;  /* child: user input */
} MyAppModel;
```

- **Routing messages**: The parent's `update()` inspects `msg.type` and
  forwards to the appropriate child via `tui_<child>_update(child, msg)`.
  `TUI_MSG_WINDOW_SIZE` is typically handled at the parent level to resize
  children.
- **Composing views**: The parent's `view()` calls each child's
  `tui_<child>_view()` to write content into the shared `DynamicBuffer *out`,
  then returns a single `TuiView` with the terminal-mode declarations for the
  frame.
- **Cursor across focusable children**: `tui_textinput_cursor_pos()` and
  `tui_viewport_cursor_pos()` return `tui_cursor_hidden()` when the child is
  unfocused, so the focused child's cursor naturally wins. Pick based on the
  parent's `focus_idx`.
- **Batching commands**: Use `tui_cmd_batch2(cmd1, cmd2)` to combine commands
  from multiple children — it handles NULLs gracefully (returns the non-NULL
  one, or NULL if both are NULL).

## Focus management

Focus is **parent-owned** — there is no library-side focus router. The parent
tracks which child is active and dispatches `TUI_MSG_FOCUS` / `TUI_MSG_BLUR`
when focus moves. Components that care about focus (`textinput`, `viewport`)
update their own `focused` flag in response. Tab / Shift+Tab cycling is
implemented in the parent's `update()`.

## Runtime modes

- **High-level**: `tui_runtime_run()` — owns the event loop, raw mode, signals.
- **Lower-level**: caller drives `tui_runtime_start()` /
  `tui_runtime_process_input()` / `tui_runtime_flush()` / `tui_runtime_stop()`.
- **Inline rendering**: `TUI_RENDER_INLINE` renders in the primary buffer (no
  alt screen) for REPL-style apps. Use `tui_runtime_finish_inline()` before
  printing app output below the rendered content.
- **Message/command scheduling**: `tui_runtime_post()` queues a message through
  the full update cycle; `tui_runtime_schedule()` queues a command for direct
  execution. Both wake the event loop via a self-pipe.
  `tui_runtime_wakeup()` wakes `select()` without queuing (thread-safe,
  async-signal-safe). For lower-level event loops, add
  `tui_runtime_wakeup_fd()` to your `select()`/`poll()` set and call
  `tui_runtime_drain()` when readable.

## TuiRuntimeConfig callbacks

Subscriptions in boba are config-struct callbacks set once at startup, not
re-evaluated per update (terminal event sources are static):

| Callback              | Purpose                                             |
| --------------------- | --------------------------------------------------- |
| `on_tick`             | Called every ~100ms select timeout                  |
| `get_tick_timeout_ms` | Dynamic tick timeout (-1 = block indefinitely)      |
| `on_resize`           | Terminal resized (width/height already updated)     |
| `get_external_fd`     | Return an external FD to poll alongside stdin       |
| `on_io_ready`         | An I/O source is ready for reading/writing          |
| `on_stdin_processed`  | After stdin input is processed through the runtime  |
| `cmd_handler`         | App command callback for custom commands            |
| `clipboard_handler`   | Override OSC 52 clipboard (e.g. shell out to xclip) |

## Clipboard

Components emit `TUI_CMD_CLIPBOARD_COPY`. The runtime handles it two ways:

- **Default — OSC 52**: writes `ESC ] 52 ; c ; <base64> ESC \` to the output.
  Works on kitty, alacritty, wezterm, iTerm2, foot, ghostty, recent xterm.
  VTE-based terminals (GNOME Terminal, XFCE, Terminator) silently drop it.
- **App-supplied handler**: install `clipboard_handler` on `TuiRuntimeConfig`
  to override (e.g. shell out to `xclip` / `wl-copy` / `pbcopy`).

## Software scrolling

The viewport uses software scrolling (absolute cursor positioning + repaint)
instead of ANSI scroll regions (DECSTBM). This keeps rendering in the library's hands —
ANSI scroll regions behave inconsistently across terminal emulators, cause
cursor positioning glitches at region boundaries, and the host terminal
controls scrollback behavior. Software scrolling gives the viewport full
ownership of every pixel it draws.

## Streaming transcript (`include/boba/stream.h`, `src/stream.c`)

The inline transcript protocol generalised: deltas stream in, the
app's **classifier callback** says what each line means (continue /
blank / block-start / reclassify-prev / container-open / close, with
block kinds), and only **finalized** emission units are committed to
the scrollback via `tui_runtime_transcript_write`. Everything
provisional renders in the live region, windowed to the frame height.

Emission granularity is per kind, because retention is only needed
where rendering can still change:

- **byte** — the system stream (`tui_msg_stream_text`, verbatim) and
  unlabeled fences: deltas stage straight to the commit path mid-line
  (the seam extends the open row on the next write); no live block,
  no retention.
- **line** — prose, headings, lists, quotes, labeled fences:
  one-line lookahead; each frozen line renders once via
  `render_block`.
- **block** — tables: the live block is windowed to its tail and
  rendered once, at finalize.

The commit pass runs at the top of `tui_runtime_flush()` for the
attached transcript (`tui_runtime_set_transcript`), so all units
staged in one update reach the scrollback through ONE
`transcript_write` (one geometry baseline per batch). Attach is the
only wiring; apps never call the seam. That commit pass is the first
half of the runtime's **reconcile step** — what a flush decides before
it emits (commit pass, `view()`, capability probe) — followed by the
**paint** that emits and delivers it; see the delivery note below.

**Two atomicities, named separately — collapsing them is how the caret bug
shipped (fixed 2026-10-19):**

- **Geometry-atomic**: erase + write + repaint share one baseline (the
  seam above).
- **Delivery-atomic**: the whole paint leaves in **ONE write**. Under
  ConPTY the presentation boundary IS the write boundary, so a paint that
  delivered twice published its intermediate cursor positions as frames of
  their own — the caret hop between the prompt and the last committed row
  (pre-`6acd0b5`'s `?25l`/`?25h` pair had merely masked it by keeping the
  caret off; the per-frame pair must never come back). Enforced by the
  **paint** — `paint_depth` + `out_stage` on `TuiRuntime`: interior
  emissions stage, only the outermost close writes, and
  `out_write_count` (one per paint) is what tests assert. The paint
  section of `src/runtime.c` carries the same reasoning in code.

Two boundaries are load-bearing and must not be collapsed:

- **Boba owns the terminal; the app owns the meaning.** Boba must never
  learn markdown. It ships only a blank-line default classifier;
  markdown, log, or diff grammars live in the app. If a change makes
  boba parse a specific content format, that is the wrong change.
- **The app describes content; boba materializes bytes.** Renderers
  write through an opaque `TuiRowSink`; boba owns the presentation
  buffer and every framing byte (row terminators, EL, CRLF
  normalization, cursor placement, capability sequences). Committed
  bytes carry NO width break — the terminal owns soft-wrapping and
  reflow; only the transient LIVE frame wraps explicitly, for its own
  row math. No `DynamicBuffer` handle and no raw-bytes
  escape hatch on this seam — an app with a buffer handle can emit a
  bare `\n` or a stray cursor move, which is the exact bug class the
  atomic transcript seam exists to prevent. If an app needs raw bytes,
  the feature belongs in boba.

**Carriage return** — `tui_msg_stream_text(id, text, len, reading)` takes a
`TuiStreamReading` (msg.h) because the reading belongs to the PRODUCER of the
bytes: `TUI_STREAM_LINES` (the default) makes a bare CR a row break, and
`TUI_STREAM_TERMINAL` reads the bytes as a terminal's own output, where a bare
CR marks the open row and the next glyph/tab replaces it (one atomic paint, no
latency), `CR LF` is a line break, and a CR with no following glyph leaves the
last frame standing. The runtime half is
`tui_runtime_transcript_write(rt, bytes, len, replace_open_row)` — rewind
`ceil(cols/width)` rows, CR, ED, rewrite, same one-write paint — whose only
caller is the transcript's commit pass. Nevermore's `!` shell mode is the
`TUI_STREAM_TERMINAL` consumer; tool panels and prose stay on
`TUI_STREAM_LINES`.

**Chunk independence.** A child's bytes arrive cut wherever the read landed,
so the byte path owns the boundaries: a UTF-8 sequence cut by the end of a
message is carried (≤3 bytes of row state) and completed by the next
message, and an ill-formed one prints U+FFFD — one per maximal subpart,
never swallowing the byte that broke it. Where the caller cut its bytes
cannot change the committed stream finalised in v0.10.0.

**Attribute state (SGR containment).** The transcript keeps the SGR set the
stream has left in effect (`sgr_state`, `sgr_committed`, `sgr_cr` — a byte
log since the last reset, capped at 1 KiB). Every committed write ENDS in
the default state, because the live region is repainted in the same write
and must not inherit a colour the app left open; a write that continues an
open row restates the state after any leading break, and a CR replace
restates the state as of the CR where the dropped frame began.

Tests: `tests/test_stream.c` (app-agnosticism via a fake classifier,
granularity, coalescing, ordering, submit/clear, trim, sink math, CR
replacement, UTF-8 chunk independence, SGR containment) plus
`tests/tmux/stream_cr.sh` (what a real terminal draws: one progress row
replaced in place, no bleed into the frame).

## Text measurement (`src/unicode.c`)

Width is UAX #11 (East Asian Width) + UAX #29 (grapheme clusters), read
from the **generated** `src/unicode_tables.h` — never hand-edited. Its
provenance banner records the Unicode version, the SHA-256 of every UCD
file it was built from, and the generator's own SHA-256, so a stale copy
is visible at a glance. coffer's `gen_unicode_tables.py` is the source of
truth for that header (one generator, one Unicode version, two repos):
regenerate with `scripts/sync-unicode-tables.sh` against a coffer
checkout (`$COFFER`, default `../coffer`) and commit the diff.

Two layers — callers want the outer one:

- `tui_codepoint_width(cp)` — one codepoint (ASCII fast path, ZERO table,
  WIDE table). Rarely the right unit on its own.
- `tui_next_cluster(utf8, len, &bytes)` / `tui_cluster_width(cps, len)` —
  one **grapheme cluster**: the column-safe unit. A base, its combining
  marks, a VS16/VS15 selector, and a ZWJ sequence measure and advance
  together, so column math never splits them. A regional-indicator pair
  is one flag. How the selectors affect the width depends on the
  terminal's **width model** (next section).

Every renderer that walks text by display columns (stream staging and the
row sink, the viewport wrap scanners, `style.c` border tiling,
`list_popup` truncation, and every horizontal metric in `textinput` — its
gutter/prompt widths, the cursor's column, the wrap budget and the
horizontal scroll window) iterates by `tui_next_cluster`. Do not add a loop
that steps one _codepoint_ at a time when columns are at stake — that is
the bug class this section exists to prevent (it can wrap a base away from
its modifier, or emit half a ZWJ sequence).

Escape sequences are classified by one grammar, `src/ansi_scan.h` (CSI,
OSC, DCS/APC/PM/SOS, two-byte escapes, split across chunks); the caller
decides what to do with a sequence (the transcript keeps SGR, measurement
and row arithmetic count it as zero, viewport passes it through). Do not
add another `in_escape` loop: the CSI-only ones this replaced counted an
OSC 8 hyperlink's URL as visible columns, so the same string measured
differently in different components.

Fitting styled text into a column budget goes through `text.h` —
`tui_text_fit()` for the prefix, `tui_text_emit()` to write it (a NULL
output measures), with an optional per-cluster hook for styling at
cluster boundaries (viewport's selection reversal). It is the walk behind
viewport clipping and its column lookup, list_popup truncation,
statusline elision, and textinput's wrap/scroll budget; `at_least_one`
keeps a chunked walk progressing. Do not hand-roll another
cluster-stepping fit.

Editing moves by cluster too: `tui_prev_cluster` is the backtracking
counterpart of `tui_next_cluster`, and textinput's backspace, delete,
cursor steps, Ctrl+T and vertical motion use it (a vertical move keeps
the DISPLAY COLUMN and lands on a cluster boundary). Echo masking is one
`*` per cluster. `tui_utf8_char_len` / `tui_utf8_prev_char` are for
byte-offset plumbing, not for an edit step.

### Cell width is a terminal model, not a Unicode constant

The same string occupies different columns on different terminals, so
width is a **model** (`TuiWidthModel`, `unicode.h`) chosen once at startup
and held for the runtime's life:

- `TUI_WIDTH_MODEL_GRID` — xterm, Windows Terminal, unknown. A variation
  selector picks a glyph, not a cell count: **VS15 never narrows a wide
  base** (`⚡︎` is two cells). VS16 still widens a narrow base (`✏️` is
  two cells).
- `TUI_WIDTH_MODEL_WCWIDTH` — strict wcwidth: neither selector changes
  the base's width.
- `TUI_WIDTH_MODEL_UTS51` — portty (coffer's engine). VS15 takes one
  cell where UTS #51 defines a text glyph for the base (`TU_TEXT_PRES` —
  ⚡ U+26A1 narrows from two, CJK and `😀` U+1F600 do not, having no
  narrow form); VS16 forces two; the last selector in the cluster wins.
- `TUI_WIDTH_MODEL_KITTY` — kitty's extension of the presentation rule.
  The selector is resolved against the codepoint it immediately follows
  and the current cell width: VS15 narrows a two-cell base with the
  `Emoji` property (`TU_EMOJI` — `🧠︎` and `😀︎` narrow; CJK does not),
  VS16 widens a one-cell Emoji base. Continuations (combining marks,
  emoji modifiers, ZWJ components) add no width and leave the width state
  alone, so `👍🏽︎` and `👨‍👩‍👧‍👦︎` narrow too. Consecutive selectors do
  not undo the first. Kitty also treats `Emoji_Modifier_Base` as two
  cells and widens a narrow base followed by Thai/Lao SARA AM. This is
  not UTS #51; the Emoji property is broader than the standard's
  variation-sequence list.
- `TUI_WIDTH_MODEL_AUTO` — a configuration value only. The runtime
  resolves it (next paragraph) before the component's `init()`, pins the
  result, and restores the previous model on free. The zero-suffix
  measurement entry points read the ambient model; the `_model` variants
  take it explicitly.

What the terminals actually do, measured on the real ones (2026-10-24:
XTerm 411, kitty, portty — a cluster's cells, asked of the terminal
itself with a DSR cursor report):

| cluster             | xterm | kitty | portty |
| ------------------- | ----- | ----- | ------ |
| `⚡` U+26A1         | 2     | 2     | 2      |
| `⚡︎` U+26A1 + VS15   | 2     | 1     | 1      |
| `⚡️` U+26A1 + VS16  | 2     | 2     | 2      |
| `✏` U+270F          | 1     | 1     | 1      |
| `✏️` U+270F + VS16  | 1     | 2     | 2      |
| `😀` U+1F600        | 2     | 2     | 2      |
| `😀︎` U+1F600 + VS15 | 2     | 1     | 2      |
| `🧠︎` U+1F9E0 + VS15 | 2     | 1     | 2      |

xterm draws the selector's glyph and keeps the base's cell count; kitty
applies its Emoji-property extension; portty applies the narrower UTS #51
list. So the GRID rule's VS16 (widen) is right for the terminals that
render emoji presentation and one cell WIDE for xterm — a cell spare,
never a wrap: measuring wider than the terminal draws leaves the row a
column short, and measuring narrower overflows the line. The grid rule
is the safe direction for a terminal boba cannot identify, and xterm is
the one measured terminal where it costs a cell.

`tui_width_model_detect()` (`terminal_profile.h`) is the resolver, and it
is **environment-only, never the async probe** — the model decides
committed scrollback geometry, so it cannot wait for a reply that arrives
after the first frame (and ConPTY may drop it, and tmux answers for the
wrong authority). In order: `TMUX` / `TERM=tmux*|screen*` → GRID (the
innermost multiplexer owns the grid); `TERM=portty*` → the portty contract
(`PORTTY_WIDTH=uts51|grid|wcwidth` when it declares one, else UTS51);
`TERM=xterm-kitty` → KITTY; `TERM=xterm*` → GRID; and only when TERM
names no terminal we know do the markers decide (`PORTTY_WIDTH`, then
`PORTTY`, then `KITTY_WINDOW_ID` → KITTY, else GRID). **TERM decides
before the markers** because TERM is written by the terminal that owns the
grid, while the markers describe the terminal that spawned the shell — an
outer terminal whenever a nested terminal was started from it. An xterm
launched inside portty inherits `PORTTY=1` and `PORTTY_WIDTH=uts51`; with
the outer terminal's rule its VS15 cluster is measured one cell narrow, the
terminal draws it two cells wide, and the status row wraps (measured on a
real XTerm, 2026-10-24). `TERM_PROGRAM` is never consulted: portty sets it
to `ghostty` for third-party allowlists. `TuiRuntimeConfig.width_model`
overrides detection (the escape hatch for a terminal that overrides its own
TERM — kitty with `TERM=xterm-256color` measures with the grid rule, a cell
off rather than a wrapped row). The model is pinned while a runtime is
live, so no code may change it after the first byte. `KITTY` implements
kitty's selector and cluster rules with boba's Unicode 18 tables, plus
kitty's two-cell regional-indicator, emoji-modifier and
`Emoji_Modifier_Base` bases. Kitty 0.50 embeds Unicode 17 and its own
base-width table, so a few bare bases (the new Unicode 18 emoji, some
bare combining or spacing marks) can still differ. That is a base-table
difference, not a selector-rule one.

The whole-run entry points are `tui_utf8_display_width_n(text, len)` for a
byte range whose bytes are literal glyphs (an input buffer),
`tui_utf8_display_width_ansi(text, len)` for a rendered string that may
carry its own SGR (a gutter span, a prompt, a styled line), and
`tui_utf8_display_width(str)` for the NUL-terminated case. A codepoint
_count_ is never a column count, and neither is a byte count: measuring
either for a width puts every derived column off by the number of wide
clusters in the run (nevermore's `⚡` context gauge sat the cursor one cell
left of the text, 2026-09-22 — `textinput` counted the gutter in
codepoints and its cursor column in bytes). `tui_utf8_cp_index` /
`tui_utf8_byte_offset` are the index-level pair, for byte-offset lookups
only; the codepoint-count helper they used to sit beside had no caller
left after that fix and was deleted.

`AMBIGUOUS` (UAX #11 class A) is vendored but unused: boba has no
per-terminal ambiguous-width setting, so those codepoints stay one cell.
The cluster buffer is capped at `TU_CLUSTER_MAX` codepoints; further
joiners fold into the current cluster rather than overflowing.

## Input parser semantics

The input parser converts raw terminal bytes into typed messages. Notable
semantic mappings:

- `Ctrl+C` (0x03) → `TUI_MSG_INTERRUPT` (semantic interrupt, not a raw key)
- `Ctrl+D` (0x04) → `TUI_MSG_EOF` (semantic end-of-file event)
- `Ctrl+D` → the textinput handles `TUI_MSG_EOF` as `delete-char` (the
  character under the cursor), never as a quit: a component decides what
  an end of input means, and this one decides it means "delete"
- Shift+Tab → `{TUI_KEY_TAB, mods: TUI_MOD_SHIFT}` (xterm `CSI Z` and kitty
  protocol)
- Ctrl+Space → `{rune: ' ', mods: TUI_MOD_CTRL}`
- `ESC DEL` / `ESC BS` → `{TUI_KEY_BACKSPACE, mods: TUI_MOD_ALT}` (Option+
  Backspace in iTerm2 / Terminal.app). `CSI 127;3u` says the same thing.
- Kitty keyboard protocol release events → `tui_msg_key_release()` (only when
  a `TUI_KBD_KITTY_*` mask is declared on `TuiView.kbd_enhancements` and the
  terminal grants the flag)
- Kitty associated text (`CSI key;mod;text u`) → `TuiKeyMsg::text` (bounded
  to one grapheme; `rune` is its first codepoint). A text key's key code is
  the UNSHIFTED key, so the text is what the user typed: shift+a is
  `CSI 97;2;65u` → rune `A`. A pure text event (no key information, an IME
  result) is `CSI 0;;229u` → rune `å`. The textinput inserts `text` when it
  is there, recovers a shifted ASCII letter's capital when a terminal reports
  the key without its text, and never inserts a control code (they are not
  text by spec).
- A FUNCTIONAL key codepoint — the kitty protocol's Private Use Area
  (57344-63743): the modifier keys themselves (which the report-all flag
  reports as key events), the lock keys, F13 and up, the keypad and media
  keys — is not text and is DROPPED (no message), so a Shift press never
  types an invisible character. A functional key that produced TEXT is
  kept: a keypad digit with Num Lock on is a PUA key code whose text
  field carries the digit.
- The kitty modifier field is a BIT FIELD plus one (shift 1, alt 2,
  ctrl 4, super 8, hyper 16, meta 32, caps_lock 64, num_lock 128), never
  an enumeration of the values someone thought of. The LOCK bits are
  masked off — they are keyboard state, and with the report-all flag the
  terminal reports them on EVERY key, so a user with Num Lock on sends
  `CSI 13;130u` for Shift+Enter; a table of exact values reads that as
  NO modifiers (Shift+Enter becomes a submit, Ctrl+Shift+A a plain 'a').
  Super, hyper and meta fold into `TUI_MOD_META`.

**A key's meaning does not depend on its encoding.** Ctrl+C and Ctrl+D are
`TUI_MSG_INTERRUPT` / `TUI_MSG_EOF` whether they arrive as the legacy control
bytes or as `CSI 99;5u` / `CSI 100;5u` under the protocol (alt is excluded:
alt+ctrl+c is a key). The same rule is why a paste is text, not keystrokes.

**Capability replies are not keys.** OSC, DCS, APC and PM/SOS strings on
stdin are captured (payload only, introducer and terminator stripped) and
never dispatched to components. `CSI ?...c` (DA1), `CSI ...t` replies and
the keyboard flags answer `CSI ?<flags>u` (told apart from a key event by
its `?` — a key is `CSI <code>u`) are captured the same way. The parser
parks them on a small ring only while a probe has claimed replies
(`tui_input_parser_claim_reply`), so unsolicited string traffic (a terminal
echoing OSC 52, image passthrough) is dropped rather than accumulated; the
runtime's capability probe is the sole consumer — it decodes them into the
`TuiTerminalProfile` (see below).

## Terminal capability probe (`include/boba/terminal_profile.h`)

Declaring `TuiView.probe_terminal = 1` asks the runtime to probe the
terminal once: kitty graphics query + `CSI ? u` (keyboard flags) + DA1 +
`CSI 16 t` + XTVERSION, emitted ahead of that frame's content. Replies are
decoded as they arrive; `TUI_MSG_TERM_REPLY` is not posted to the component
(the runtime consumes it). `TuiRuntimeConfig::on_term_reply` fires exactly
once: on the `TUI_TERM_PROBE_TIMEOUT_MS` deadline (checked on the tick)
or on loop exit, so a vanished or silent terminal still reaches a
verdict. The deadline — not one particular reply — is the completion
signal, because the terminal answers in FIFO order and resolving on any
single reply would discard the ones still in flight. The transcript's
commit pass gates an IMAGE unit on this profile (see stream.h); a
gated batch with no probe outstanding resolves conservatively rather
than deadlock.

The decoder is deliberately grammar-only (`tui_term_profile_feed`),
and the query bytes are a pure function (`tui_term_probe_query`), so
both are unit-tested without a terminal or a pty.

## Project Structure

```
include/boba/          Public headers (installed)
  components/          Component headers (textinput, viewport, list_popup,
                       statusline)
  tui.h                Umbrella header — includes everything
src/                   Library implementation
  components/          Component implementations
  base64.h/c           Internal (not installed) — used by OSC 52 clipboard
tests/                 Unit tests + tmux e2e tests
  apps/                Mini-apps driven by tmux test scripts
  tmux/                Shell scripts + lib.sh helpers
scripts/               build-ucrt64.sh for Windows
build-aux/             Autotools helpers + git-version.sh
configure.ac           Autoconf config (platform detection, build modes)
Makefile.am            Top-level: format, bear targets
TODO.md                Deferred work (selection/clipboard, focus, API design)
```
