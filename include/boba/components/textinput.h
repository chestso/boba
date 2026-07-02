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

#include "../component.h"
#include "../dynamic_buffer.h"
#include "../msg.h"
#include "../style.h"

/* Text input model */
typedef struct TuiTextInput
{
    TuiModel base; /* Base model for component interface */

    char *text;      /* UTF-8 text content */
    size_t text_len; /* Current text length in bytes */
    size_t text_cap; /* Allocated capacity */

    size_t cursor_byte; /* Cursor position in bytes */
    size_t cursor_col;  /* Visual column (0-indexed) */
    size_t cursor_row;  /* Visual row (0-indexed) */

    /* Selection mark. When has_mark is set, the active region is
     * [min(mark_byte, cursor_byte), max(mark_byte, cursor_byte)). Motion
     * extends the region; edits clear it; Esc / C-g clear it; M-w copies
     * it; C-w kills it. */
    size_t mark_byte;
    int has_mark;

    int width;         /* Max width (0 = unlimited) */
    int height;        /* Max visible height (0 = grow to fit) */
    int scroll_offset; /* Vertical scroll position (first visible line) */
    int offset;        /* Horizontal scroll: left edge (codepoint index) */
    int offset_right;  /* Horizontal scroll: right edge (codepoint index) */

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

    int echo_mode; /* 0 = normal, 1 = masked (show * per codepoint) */
    int soft_wrap; /* 0 = horizontal scroll (default), 1 = soft-wrap long lines */

    /* Styled-text highlight hook. If set, render_text_range calls this
     * instead of emitting raw text bytes. The callback returns a malloc'd
     * styled string (ANSI escape sequences + text); textinput appends it
     * to the output buffer and frees it. Pass NULL to restore plain text.
     * Note: selection reverse-video and echo mask are skipped when a
     * highlight callback is set — the callback owns all styling. */
    char *(*highlight_fn)(const char *text, size_t len, void *userdata);
    void *highlight_userdata;
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

/* Set echo mode: 0 = normal, 1 = masked (show * per codepoint) */
void tui_textinput_set_echo_mode(TuiTextInput *input, int mode);

/* Set soft wrap mode: 0 = horizontal scroll (default), 1 = soft-wrap
 * long lines to the next visual row instead of clipping. */
void tui_textinput_set_soft_wrap(TuiTextInput *input, int enable);

/* Set a styled-text highlight callback. When set, the callback is invoked
 * during view() for each text segment, returning a malloc'd styled string
 * (ANSI escapes + text) that replaces the raw text in the rendered output.
 * textinput frees the returned string after appending it.
 * Pass NULL to restore plain-text rendering. */
void tui_textinput_set_text_renderer(TuiTextInput *input,
                                     char *(*fn)(const char *text,
                                                 size_t len,
                                                 void *userdata),
                                     void *userdata);

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
