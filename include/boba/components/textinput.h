/* textinput.h - Multi-line text input component for boba
 *
 * A text input widget supporting:
 * - Single and multi-line input
 * - Unicode/UTF-8 text
 * - Cursor navigation (arrows, home, end)
 * - Basic editing (insert, delete, backspace)
 * - Optional prompt string
 * - Auto-growing height for multi-line content
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
 * See tui_textinput_set_clipboard_hook(). */
typedef void (*TuiTextInputClipboardFn)(struct TuiTextInput *input,
                                        TuiTextInputClipKind kind,
                                        const char *text, size_t len,
                                        void *userdata);

/* Text input model */
typedef struct TuiTextInput
{
    TuiModel base; /* Base model for component interface */

    char *text;      /* UTF-8 text content */
    size_t text_len; /* Current text length in bytes */
    size_t text_cap; /* Allocated capacity */

    size_t cursor_byte; /* Cursor position in bytes */
    size_t cursor_col;  /* Cursor's display column in its logical line */
    size_t cursor_row;  /* Logical line index (0-indexed) */

    /* Selection mark. When has_mark is set, the active region is
     * [min(mark_byte, cursor_byte), max(mark_byte, cursor_byte)). Motion
     * extends the region; edits clear it; Esc / C-g clear it; M-w reports
     * it to the clipboard hook; C-w kills it. */
    size_t mark_byte;
    int has_mark;

    int width;         /* Max width (0 = unlimited) */
    int height;        /* Max visible height (0 = grow to fit) */
    int scroll_offset; /* Vertical scroll position (first visible line) */
    int offset;        /* Horizontal scroll: left edge (display column) */
    int offset_right;  /* Horizontal scroll: right edge (display column) */

    const char *prompt;              /* Optional prompt string (not owned) */
    int prompt_len;                  /* Cached prompt display width */
    const char *continuation_prompt; /* Prompt for continuation lines (not owned) */
    int continuation_prompt_len;     /* Cached continuation prompt display width */

    int focused;           /* Whether component has focus */
    int multiline;         /* Allow multiple lines (Enter inserts newline) */
    int show_prompt;       /* Whether to display the prompt (default: 1) */
    int terminal_width;    /* Width used for line wrapping / overflow (0 = 80) */
    int terminal_row;      /* Row for absolute positioning (1-indexed, 0 = not set) */
    char prompt_color[32]; /* Custom ANSI color for prompt (empty = no color) */

    /* Lipgloss-shaped focused/blurred styles for the prompt. When any of
     * these has a non-default styling (color or attribute), it overrides
     * the legacy prompt_color field above for the matching focus state. */
    TuiStyle focused_prompt_style;
    TuiStyle blurred_prompt_style;

    /* History management */
    char **history;    /* Array of past input lines */
    int history_size;  /* Max history entries */
    int history_count; /* Current number of history entries */
    int history_pos;   /* Navigation position (-1 = current input) */
    char *saved_input; /* Saved current input when navigating history */

    /* Tab completion */
    char *word_chars; /* Characters that form words for completion and word movement */

    /* Kill/yank buffer */
    char *kill_buf;      /* Killed text (malloc'd, NULL initially) */
    size_t kill_buf_len; /* Length of killed text in bytes */
    int last_was_kill;   /* Whether previous key was a kill command */

    /* Undo stack */
    struct
    {
        char *text;
        size_t text_len;
        size_t cursor_byte;
    } *undo_stack;
    int undo_count;
    int undo_cap;
    int ctrl_x_prefix; /* Waiting for second key after C-x */

    /* Pre-edit snapshot buffer (reused across keystrokes) */
    char *snap_buf;
    size_t snap_buf_cap;
    size_t snap_len;
    size_t snap_cursor;
    int snap_valid; /* Whether snapshot should be committed */

    int echo_mode; /* 0 = normal, 1 = masked (one * per grapheme cluster) */
    int soft_wrap; /* 0 = horizontal scroll (default), 1 = soft-wrap long lines */

    /* Rows drawn by the last absolute-positioning view() pass. Used to erase
     * surplus rows when a soft-wrapped line shrinks (e.g. backspacing across
     * a wrap boundary) — the runtime does not clear stale alt-screen rows,
     * so the component owns cleanup of its own area. */
    int last_render_rows;

    /* Bumped by every change to the text's bytes (typing, deletes, kills,
     * undo, paste, set_text, clear, replace_range), never by movement.
     * Spans carry the revision they were computed for. */
    uint64_t revision;
    TuiTextSpan *spans; /* owned copy, sorted, disjoint */
    size_t n_spans;
    uint64_t spans_revision;

    /* Styled-text highlight hook. If set, render_text_range calls this
     * instead of emitting raw text bytes. The callback returns a malloc'd
     * styled string (ANSI escape sequences + text); textinput appends it
     * to the output buffer and frees it. Pass NULL to restore plain text.
     * Note: selection reverse-video and echo mask are skipped when a
     * highlight callback is set — the callback owns all styling. */
    char *(*highlight_fn)(const char *text, size_t len, void *userdata);
    void *highlight_userdata;

    /* Clipboard reporting (see TuiTextInputClipboardFn). Appended here so
     * the public struct keeps its field order. */
    TuiTextInputClipboardFn clipboard_fn; /* NULL = report nothing */
    void *clipboard_ud;
    int clipboard_cmd; /* 0 = emit no TuiCmd at all (default) */
} TuiTextInput;

/* Configuration for creating text input */
typedef struct TuiTextInputConfig
{
    const char *placeholder; /* Placeholder text (shown when empty) */
    const char *prompt;      /* Prompt string (e.g., "> ") */
    int width;               /* Max width (0 = unlimited) */
    int height;              /* Max height (0 = grow to fit) */
    int multiline;           /* Allow multiple lines */
} TuiTextInputConfig;

/* Create a new text input component
 *
 * Parameters:
 *   config: Optional configuration (NULL for defaults)
 *
 * Returns: New text input model, or NULL on failure
 */
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

/* Get current text content */
const char *tui_textinput_text(const TuiTextInput *input);

/* Get text length */
size_t tui_textinput_len(const TuiTextInput *input);

/* Set text content */
void tui_textinput_set_text(TuiTextInput *input, const char *text);

/* Clear text content */
void tui_textinput_clear(TuiTextInput *input);

/* Set focus state */
BOBA_DEPRECATED(
    "send tui_msg_focus() / tui_msg_blur() through tui_textinput_update()")
void tui_textinput_set_focus(TuiTextInput *input, int focused);

/* Check if focused */
int tui_textinput_is_focused(const TuiTextInput *input);

/* Get cursor position (byte offset) */
size_t tui_textinput_cursor(const TuiTextInput *input);

/* Set cursor position (byte offset) */
void tui_textinput_set_cursor(TuiTextInput *input, size_t pos);

/* Get number of lines in content */
int tui_textinput_line_count(const TuiTextInput *input);

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

/* The text's revision: a counter that changes whenever the bytes do,
 * and not for cursor movement, selection or focus. It is how an app
 * tells "this key edited the text" from "this key moved around in it"
 * without keeping a copy to compare. The value itself means nothing —
 * only equal/unequal between two reads. */
uint64_t tui_textinput_revision(const TuiTextInput *input);

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
 * clear. Ignored while a text renderer (below) is set. */
int tui_textinput_set_spans(TuiTextInput *input, const TuiTextSpan *spans,
                            size_t n);

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

/* Set whether to show the prompt */
void tui_textinput_set_show_prompt(TuiTextInput *input, int show);

/* Set the prompt string */
void tui_textinput_set_prompt(TuiTextInput *input, const char *prompt);

/* Set width used for line wrapping / horizontal overflow */
void tui_textinput_set_terminal_width(TuiTextInput *input, int width);

/* Set terminal row for absolute positioning (1-indexed).
 * When set, view() uses absolute cursor positioning instead of relative
 * moves. This is the row for the input line.
 */
void tui_textinput_set_terminal_row(TuiTextInput *input, int row);

/* Set custom ANSI color escape sequence for the prompt.
 * Pass an ANSI SGR sequence (e.g., "\033[38;2;255;6;183m").
 * Pass NULL or "" to reset to default (no color).
 */
BOBA_DEPRECATED(
    "use tui_textinput_set_focused_prompt_style / _blurred_prompt_style")
void tui_textinput_set_prompt_color(TuiTextInput *input, const char *color);

/* Lipgloss-shaped style setters for the prompt. When set (i.e., the
 * style has any color or attribute set), these take precedence over
 * the legacy set_prompt_color above for the matching focus state. The
 * style's box-model (padding/margin/border) is ignored — styling is
 * applied inline. */
void tui_textinput_set_focused_prompt_style(TuiTextInput *input, TuiStyle s);
void tui_textinput_set_blurred_prompt_style(TuiTextInput *input, TuiStyle s);

/* Set the continuation prompt string (shown on lines after the first).
 * Must have the same display width as the main prompt.
 * Pass NULL to fall back to space-padding (default behavior).
 */
void tui_textinput_set_continuation_prompt(TuiTextInput *input,
                                           const char *prompt);

/* Set echo mode: 0 = normal, 1 = masked (one * per grapheme cluster) */
void tui_textinput_set_echo_mode(TuiTextInput *input, int mode);

/* Set soft wrap mode: 0 = horizontal scroll (default), 1 = soft-wrap
 * long lines to the next visual row instead of clipping. */
void tui_textinput_set_soft_wrap(TuiTextInput *input, int enable);

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

/* Pull-style access to the kill ring, for embedders that poll instead of
 * taking the hook. The pointer is borrowed and valid until the next kill;
 * NULL / 0 before the first one. Kills and cuts fill it; an explicit copy
 * does not. */
const char *tui_textinput_kill_buffer(const TuiTextInput *input, size_t *len);

/* Get echo mode */
int tui_textinput_get_echo_mode(const TuiTextInput *input);

/* Get render height in rows.
 * Returns 1 for single-line input, line count for multi-line.
 */
int tui_textinput_get_height(const TuiTextInput *input);

/* Report cursor position for the runtime to place the hardware cursor.
 * Returns tui_cursor_hidden() when the input is unfocused or when
 * terminal_row is 0 (relative-positioning mode — caller has no absolute
 * coordinate frame). Otherwise returns the absolute (row, col) of the
 * cursor on the input line. */
TuiCursor tui_textinput_cursor_pos(const TuiTextInput *input);

/* Get component interface for text input */
const TuiComponent *tui_textinput_component(void);

#endif /* BOBA_TEXTINPUT_H */
