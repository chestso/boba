/* textinput.h - Multi-line text input component for boba
 *
 * A text input widget supporting:
 * - Single and multi-line input
 * - Unicode/UTF-8 text
 * - Cursor navigation (arrows, home, end, page up/down)
 * - Basic editing (insert, delete, backspace)
 * - Optional prompt string
 * - Soft-wrapped or horizontally clipped long lines
 * - A vertical window over content taller than the box it may paint
 *
 * ## The model is opaque
 *
 * TuiTextInput is created by tui_textinput_create() and read through the
 * accessors at the bottom of this header. Fields are not part of the API:
 * derived state (the visual-row layout, the horizontal window, the scroll
 * anchor) belongs to the component, and an app that cached its own copy
 * would only be able to disagree with it.
 *
 * ## Geometry in one place
 *
 * Everything about "how big am I and where do I go" is one struct,
 * TuiTextInputGeometry, set with one call. Three named modes replace the
 * old 0-sentinels:
 *
 *   NATURAL  paint every row the text needs (the default)
 *   FIXED    the app owns the box: `width` and `rows` say what it is
 *   FLOW     follow the terminal window, inside the app's reserves and caps
 *
 * and placement is explicit (INLINE or AT_ROW) instead of "terminal_row
 * happens to be non-zero". The terminal size is consumed from
 * TUI_MSG_WINDOW_SIZE whenever the app routes it to the input, in every
 * mode: knowing the screen is what makes the clamps possible, so it is not
 * an opt-in.
 */

#ifndef BOBA_TEXTINPUT_H
#define BOBA_TEXTINPUT_H

#include <stddef.h>
#include <stdint.h>

#include "../component.h"
#include "../dynamic_buffer.h"
#include "../msg.h"
#include "../style.h"

/* A styled byte range of the text: [start, end), absolute offsets into
 * tui_textinput_text(). See tui_textinput_set_spans. */
typedef struct TuiTextSpan
{
    size_t start, end;
    TuiStyle style; /* colors and attributes; box-model fields are ignored */
} TuiTextSpan;

/* Why textinput is handing the app clipboard text. */
typedef enum
{
    TUI_TEXTINPUT_CLIP_KILL,          /* ring contents after append/prepend */
    TUI_TEXTINPUT_CLIP_COPY,          /* the selection, or (M-w) the input   */
    TUI_TEXTINPUT_CLIP_CUT,           /* the cut text only                   */
    TUI_TEXTINPUT_CLIP_PASTE_REQUEST, /* text == NULL: ask the app to paste  */
} TuiTextInputClipKind;

struct TuiTextInput; /* forward: the callback below takes the model */

/* Fired from update() when a key press wants the clipboard touched. `text`
 * is borrowed, valid only for the duration of the call; for
 * PASTE_REQUEST it is NULL with len 0. It fires after the edit and after
 * the undo commit, so it sees the final text, and it must not call
 * textinput mutators (it would re-enter the edit it is reporting).
 *
 * Which key reports what:
 *   KILL   C-k, C-u, C-w, Cmd+Backspace, Ctrl/Alt+Backspace, Ctrl+Delete,
 *          Alt+d          — the ring, after any append/prepend
 *   COPY   M-w (selection, else the whole input), Cmd+C, Ctrl+Insert,
 *          Ctrl+Shift+C   — the selection only; nothing when there is none
 *   CUT    Cmd+X, Shift+Delete — the cut bytes, which also enter the ring
 *   PASTE_REQUEST  Cmd+V, Shift+Insert, Ctrl+Shift+V — the app owns the
 *          clipboard read and posts a TUI_MSG_PASTE
 *
 * Three deliberate asymmetries, not oversights:
 *   - COPY never becomes a TUI_CMD_CLIPBOARD_COPY, whatever the app's
 *     opt-in says: a copy is the app's to route, never silently hijacked
 *     onto OSC 52.
 *   - Cmd+C with no selection copies nothing; the "no mark => whole
 *     input" fallback belongs to M-w alone.
 *   - Ctrl+A is line start unless the app asks for select-all (see
 *     tui_textinput_set_ctrl_a_select_all) — boba never inspects the
 *     platform to pick a binding.
 *
 * See tui_textinput_set_clipboard_hook(). */
typedef void (*TuiTextInputClipboardFn)(struct TuiTextInput *input,
                                        TuiTextInputClipKind kind,
                                        const char *text, size_t len,
                                        void *userdata);

/* The model. Opaque: use the accessors below. */
typedef struct TuiTextInput TuiTextInput;

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/* How the input decides how many rows to paint.
 *
 *   NATURAL  every row the text needs. Nothing is hidden vertically, and
 *            the app is responsible for the frame fitting (this is the
 *            behaviour of every version before the window existed).
 *   FIXED    the app owns the box: `rows` caps the painted rows and the
 *            input scrolls vertically inside them.
 *   FLOW     the input follows the terminal window itself: the usable
 *            height is term_h - reserve_above - reserve_below, further
 *            capped by `rows` and floored by `min_rows`. Requires the app
 *            to route TUI_MSG_WINDOW_SIZE to the input.
 */
typedef enum
{
    TUI_TEXTINPUT_NATURAL = 0,
    TUI_TEXTINPUT_FIXED,
    TUI_TEXTINPUT_FLOW,
} TuiTextInputSizing;

/* Where the first painted row goes. INLINE rows are relative: the runtime
 * counts the frame's newlines to move the cursor, so the input emits one
 * row separator per row and nothing else. AT_ROW rows are absolute screen
 * coordinates starting at `row` (1-based). */
typedef enum
{
    TUI_TEXTINPUT_INLINE = 0,
    TUI_TEXTINPUT_AT_ROW,
} TuiTextInputPlacement;

/* What happens to a logical line wider than the content width.
 *
 *   CLIP  one horizontal window, shared by every row: the text scrolls
 *         under a fixed column window centred on the cursor
 *   WRAP  the line continues on the next visual row, aligned under the
 *         text column, so nothing is hidden horizontally
 */
typedef enum
{
    TUI_TEXTINPUT_CLIP = 0,
    TUI_TEXTINPUT_WRAP,
} TuiTextInputWrap;

/* Everything about size and placement, in one struct.
 *
 * Zero-initialise it and set what you need: a zeroed geometry is NATURAL
 * sizing, INLINE placement, no caps, no reserves — today's defaults.
 *
 *   sizing         NATURAL | FIXED | FLOW (above)
 *   place, row     INLINE, or AT_ROW with `row` = the 1-based screen row
 *                  of the input's first row
 *   width          content width in columns. FIXED: the box width. FLOW:
 *                  0 means derive from the terminal (term_w - side_margin);
 *                  a non-zero width wins, for apps that box the input in a
 *                  border. 0 in NATURAL/FIXED means "no constraint": no
 *                  wrapping and no horizontal clipping.
 *   rows           maximum rows painted; 0 = no cap
 *   min_rows       floor for FLOW sizing (>= 1); never lifts the result
 *                  above the text's own height or above what the screen
 *                  allows
 *   reserve_above  FLOW: rows the app paints above the input in the frame
 *   reserve_below  FLOW: rows the app paints below it
 *   side_margin    FLOW: columns subtracted from the terminal width
 *   scrolloff      rows of context kept visible above/below the cursor
 *                  when the window scrolls, capped at (visible_rows - 1) / 2.
 *                  0 means none; tui_textinput_create() seeds 1 when the
 *                  config leaves it 0, so a zeroed geometry has context.
 */
typedef struct TuiTextInputGeometry
{
    TuiTextInputSizing sizing;
    TuiTextInputPlacement place;
    int row;
    int width;
    int rows;
    int min_rows;
    int reserve_above;
    int reserve_below;
    int side_margin;
    int scrolloff;
} TuiTextInputGeometry;

/* Configuration for creating a text input. Zero-initialise it: every field
 * is optional.
 *
 *   placeholder  shown when the text is empty (not owned, must outlive)
 *   prompt       the prompt string (not owned, must outlive)
 *   multiline    1 = Enter inserts a newline, 0 = Enter submits
 *   wrap         CLIP (default) or WRAP
 *   rows         seeds geometry.rows when geometry.rows is 0
 *   geometry     initial geometry; a zeroed struct means the defaults
 */
typedef struct TuiTextInputConfig
{
    const char *placeholder;
    const char *prompt;
    int multiline;
    TuiTextInputWrap wrap;
    int rows;
    TuiTextInputGeometry geometry;
} TuiTextInputConfig;

/* Where the text currently sits in the window. Both axes, one call: an app
 * drawing "↑ 12 more" chrome, a scrollbar, or a horizontal position
 * indicator needs all of it, and asking twice could answer from two
 * different frames. */
typedef struct TuiTextInputScroll
{
    int total_rows;   /* visual rows the text needs (the natural height) */
    int visible_rows; /* visual rows painted this frame                  */
    int first_row;    /* index of the first visible visual row           */
    int hidden_above; /* visual rows scrolled out above                  */
    int hidden_below; /* visual rows scrolled out below                  */
    int offset_left;  /* CLIP only: left display column of the window    */
    int offset_right; /* CLIP only: right display column of the window   */
} TuiTextInputScroll;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/* Create a new text input component.
 * Parameters: config — optional (NULL for defaults)
 * Returns: new text input model, or NULL on failure */
TuiTextInput *tui_textinput_create(const TuiTextInputConfig *config);

/* Free text input component */
void tui_textinput_free(TuiTextInput *input);

/* Update text input with message
 *
 * Parameters:
 *   input: Text input model
 *   msg: Message to process
 *
 * Returns: Update result with optional command
 */
TuiUpdateResult tui_textinput_update(TuiTextInput *input, TuiMsg msg);

/* Render text input to output buffer
 *
 * Parameters:
 *   input: Text input model (const)
 *   out: Output buffer to append to
 */
void tui_textinput_view(const TuiTextInput *input, DynamicBuffer *out);

/* Get component interface for text input */
const TuiComponent *tui_textinput_component(void);

/* ------------------------------------------------------------------ */
/* Geometry and scrolling                                              */
/* ------------------------------------------------------------------ */

/* Set the whole geometry in one call. `g` is copied; NULL resets to the
 * defaults. Re-derives the window and keeps the cursor visible. */
void tui_textinput_set_geometry(TuiTextInput *input,
                                const TuiTextInputGeometry *g);

/* Read back the geometry in force (the stored struct, not the derived
 * window; see tui_textinput_scroll_state for the derived numbers). */
TuiTextInputGeometry tui_textinput_geometry(const TuiTextInput *input);

/* One-liners for the two knobs that change most often. Both write the same
 * struct as tui_textinput_set_geometry() — there is only one geometry.
 *
 * tui_textinput_set_placement(row): row > 0 places the input absolutely at
 * that 1-based screen row; row == 0 renders inline.
 * tui_textinput_set_rows(rows): cap the painted rows (0 = no cap) and
 * scroll vertically inside them; sizing becomes FIXED unless it is FLOW. */
void tui_textinput_set_placement(TuiTextInput *input, int row);
void tui_textinput_set_rows(TuiTextInput *input, int rows);

/* The window the text is currently painted through. */
TuiTextInputScroll tui_textinput_scroll_state(const TuiTextInput *input);

/* Visual rows the whole text needs at the current width (the natural
 * height), and the rows actually painted (the visible height H). They are
 * equal whenever nothing caps the input. */
int tui_textinput_total_rows(const TuiTextInput *input);
int tui_textinput_visible_rows(const TuiTextInput *input);

/* The cursor's position inside the painted window, 0-based: row 0 is the
 * input's first painted row in both placement modes. Returns 0 in *row and
 * *col (and does nothing) for a hidden cursor. Use tui_textinput_cursor_pos
 * for the runtime's coordinate frame. */
void tui_textinput_cursor_visual(const TuiTextInput *input, int *row,
                                 int *col);

/* Report cursor position for the runtime to place the hardware cursor.
 * Returns tui_cursor_hidden() when the input is unfocused, or when the
 * cursor is outside the visible window — which update() cannot leave it in
 * (a focused input's cursor row is always inside [top, top + H) once
 * ensure_cursor_visible() has run; a hidden cursor is the safe answer if a
 * future path ever breaks that). The row is absolute in AT_ROW mode and
 * frame-relative in INLINE mode. */
TuiCursor tui_textinput_cursor_pos(const TuiTextInput *input);

/* ------------------------------------------------------------------ */
/* Content                                                             */
/* ------------------------------------------------------------------ */

/* Get current text content (borrowed; valid until the next edit) */
const char *tui_textinput_text(const TuiTextInput *input);

/* Get text length in bytes */
size_t tui_textinput_len(const TuiTextInput *input);

/* Set text content */
void tui_textinput_set_text(TuiTextInput *input, const char *text);

/* Clear text content */
void tui_textinput_clear(TuiTextInput *input);

/* Check if focused */
int tui_textinput_is_focused(const TuiTextInput *input);

/* Get cursor position (byte offset) */
size_t tui_textinput_cursor(const TuiTextInput *input);

/* Set cursor position (byte offset) */
void tui_textinput_set_cursor(TuiTextInput *input, size_t pos);

/* Get number of logical lines in content */
int tui_textinput_line_count(const TuiTextInput *input);

/* The text's revision: a counter that changes whenever the bytes do,
 * and not for cursor movement, selection or focus. It is how an app
 * tells "this key edited the text" from "this key moved around in it"
 * without keeping a copy to compare. The value itself means nothing —
 * only equal/unequal between two reads. */
uint64_t tui_textinput_revision(const TuiTextInput *input);

/* ------------------------------------------------------------------ */
/* Selection and editing                                               */
/* ------------------------------------------------------------------ */

/* 1 when a selection is active (a mark is set at a byte other than the
 * cursor's). */
int tui_textinput_has_selection(const TuiTextInput *input);

/* 1 when a mark is set at all — a "mark set" mode line wants this, because
 * an empty mark (mark == cursor) is still a mark the next motion extends. */
int tui_textinput_has_mark(const TuiTextInput *input);

/* The mark's byte offset. Only meaningful when tui_textinput_has_mark() is
 * 1; returns the cursor's offset otherwise. */
size_t tui_textinput_mark(const TuiTextInput *input);

/* The active selection as [start, end), in bytes. Returns 1 when there is
 * one, 0 (leaving the outputs untouched) when there is not. */
int tui_textinput_selection_range(const TuiTextInput *input, size_t *start,
                                  size_t *end);

/* How many edits can be undone. Read-mostly: for a status line, or a test
 * asserting that a burst of typing is one undo step. */
int tui_textinput_undo_depth(const TuiTextInput *input);

/* Set maximum history size */
void tui_textinput_set_history_size(TuiTextInput *input, int size);

/* Add a line to history */
void tui_textinput_history_add(TuiTextInput *input, const char *line);

/* Insert a completion word, replacing the current word starting at word_start.
 * word_start is the byte offset of the word being completed. */
void tui_textinput_insert_completion(TuiTextInput *input, int word_start,
                                     const char *word);

/* Replace the bytes [start, end) with `text` and leave the cursor after
 * it, as ONE undoable edit. Offsets are clamped to the text. Unlike
 * insert_completion it does not assume the range ends at the cursor, so
 * a token the cursor sits in the middle of can be replaced whole. */
void tui_textinput_replace_range(TuiTextInput *input, size_t start, size_t end,
                                 const char *text);

/* Extract the word at the cursor position. Returns a malloc'd string
 * (the text from word_start to cursor) and sets *word_start to the
 * byte offset where the word begins. Returns NULL (and sets
 * *word_start to -1) if the cursor is not within a word (e.g. on
 * whitespace or in an empty buffer). Caller frees the returned string. */
char *tui_textinput_word_at_cursor(const TuiTextInput *input, int *word_start);

/* Set characters that form words for tab completion and word movement.
 * When set, only these characters are considered part of a word.
 * Pass NULL to use default behavior (non-whitespace = word). */
void tui_textinput_set_word_chars(TuiTextInput *input, const char *chars);

/* Pull-style access to the kill ring, for embedders that poll instead of
 * taking the hook. The pointer is borrowed and valid until the next kill;
 * NULL / 0 before the first one. Kills and cuts fill it; an explicit copy
 * does not. */
const char *tui_textinput_kill_buffer(const TuiTextInput *input, size_t *len);

/* ------------------------------------------------------------------ */
/* Presentation                                                        */
/* ------------------------------------------------------------------ */

/* Set whether to show the prompt */
void tui_textinput_set_show_prompt(TuiTextInput *input, int show);

/* Set the prompt string (not owned; must outlive the input) */
void tui_textinput_set_prompt(TuiTextInput *input, const char *prompt);

/* Set the continuation prompt string (shown on lines after the first, and
 * on wrapped rows). Must have the same display width as the main prompt.
 * Pass NULL to fall back to space-padding (default behavior). */
void tui_textinput_set_continuation_prompt(TuiTextInput *input,
                                           const char *prompt);

/* Lipgloss-shaped style setters for the prompt. When set (i.e., the
 * style has any color or attribute set), these take precedence over the
 * legacy ANSI-color field for the matching focus state. The style's
 * box-model (padding/margin/border) is ignored — styling is applied
 * inline. */
void tui_textinput_set_focused_prompt_style(TuiTextInput *input, TuiStyle s);
void tui_textinput_set_blurred_prompt_style(TuiTextInput *input, TuiStyle s);

/* Set echo mode: 0 = normal, 1 = masked (one * per grapheme cluster) */
void tui_textinput_set_echo_mode(TuiTextInput *input, int mode);

/* Get echo mode */
int tui_textinput_get_echo_mode(const TuiTextInput *input);

/* Set how a logical line wider than the content width is painted: CLIP
 * (one horizontal window for every row) or WRAP (continue on the next
 * visual row). Default CLIP. */
void tui_textinput_set_wrap(TuiTextInput *input, TuiTextInputWrap mode);

/* Set a styled-text highlight callback. Prefer tui_textinput_set_spans:
 * this callback receives only the text of one rendered chunk (a wrapped
 * row or the scrolled window) with no offset or context, so it cannot
 * style anything that spans chunks, and it bypasses the selection and
 * echo mode.
 * When set, the callback is invoked
 * during view() for each text segment, returning a malloc'd styled string
 * (ANSI escapes + text) that replaces the raw text in the rendered output.
 * textinput frees the returned string after appending it.
 * Pass NULL to restore plain-text rendering. */
void tui_textinput_set_text_renderer(TuiTextInput *input,
                                     char *(*fn)(const char *text,
                                                 size_t len,
                                                 void *userdata),
                                     void *userdata);

/* Style byte ranges of the text (e.g. a file mention, a slash command).
 * The input copies the array. Ranges are absolute offsets, must be
 * sorted, disjoint, non-empty and inside the text; a bad set is refused
 * whole (-1) and the previous set stays.
 *
 * Spans describe the text AS OF the current revision: after any edit
 * they stop being painted until the app declares them again, so a stale
 * range can never style the wrong bytes. They compose with the
 * selection (reverse video) and echo mode, and are clipped to whatever
 * the row shows, so a span split by soft-wrap or scrolled partly out of
 * the window is styled exactly where its bytes are. Pass n == 0 to
 * clear. Ignored while a text renderer (above) is set. */
int tui_textinput_set_spans(TuiTextInput *input, const TuiTextSpan *spans,
                            size_t n);

/* ------------------------------------------------------------------ */
/* Clipboard                                                           */
/* ------------------------------------------------------------------ */

/* Report clipboard events to `fn` instead of (or as well as) emitting a
 * command. Pass NULL to stop reporting. The hook is the ONLY delivery path
 * by default: textinput never emits TUI_CMD_CLIPBOARD_COPY on its own, so
 * a kill or a copy can never be silently hijacked onto OSC 52. An app that
 * wants the runtime's clipboard (OSC 52) can schedule its own command from
 * the hook (tui_runtime_schedule(rt, tui_cmd_clipboard_copy(...))) or shell
 * out to pbcopy / wl-copy / xclip. */
void tui_textinput_set_clipboard_hook(TuiTextInput *input,
                                      TuiTextInputClipboardFn fn,
                                      void *userdata);

/* Opt back in to the pre-0.7 behaviour: update() also RETURNS
 * tui_cmd_clipboard_copy() for KILL and CUT. COPY and PASTE_REQUEST never
 * return a command, whatever this says — a copy is the app's to route, and
 * a paste request has no text to copy. Default 0. */
void tui_textinput_set_clipboard_cmd(TuiTextInput *input, int enable);

/* Ctrl+A: move to the start of the line (the readline/emacs binding, the
 * default) or select the whole input (the Windows editing convention).
 * boba does not infer this from the platform — an app that wants Windows
 * behaviour asks for it, so a terminal session over ssh behaves like the
 * app it is, not like the kernel underneath. Default 0. Cmd+A selects all
 * regardless. */
void tui_textinput_set_ctrl_a_select_all(TuiTextInput *input, int enable);

#endif /* BOBA_TEXTINPUT_H */
