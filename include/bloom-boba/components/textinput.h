/* textinput.h - Multi-line text input component for bloom-boba
 *
 * A text input widget supporting:
 * - Single and multi-line input
 * - Unicode/UTF-8 text
 * - Cursor navigation (arrows, home, end)
 * - Basic editing (insert, delete, backspace)
 * - Optional prompt string
 * - Auto-growing height for multi-line content
 */

#ifndef BLOOM_BOBA_TEXTINPUT_H
#define BLOOM_BOBA_TEXTINPUT_H

#include "../component.h"
#include "../dynamic_buffer.h"
#include "../msg.h"

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

    int focused;            /* Whether component has focus */
    int multiline;          /* Allow multiple lines (Enter inserts newline) */
    int show_prompt;        /* Whether to display the prompt (default: 1) */
    int show_dividers;      /* Whether to show dividers above/below (default: 0) */
    int terminal_width;     /* Terminal width for divider rendering (0 = 80) */
    int terminal_row;       /* Row for absolute positioning (1-indexed, 0 = not set) */
    char divider_color[32]; /* Custom ANSI color for dividers (empty = SGR_DIM) */
    char prompt_color[32];  /* Custom ANSI color for prompt (empty = no color) */

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

/* Set whether to show dividers above/below the input */
void tui_textinput_set_show_dividers(TuiTextInput *input, int show);

/* Set terminal width for divider rendering */
void tui_textinput_set_terminal_width(TuiTextInput *input, int width);

/* Set terminal row for absolute positioning (1-indexed)
 * When set, view() uses absolute cursor positioning instead of relative moves.
 * This is the row for the input line; dividers use adjacent rows.
 */
void tui_textinput_set_terminal_row(TuiTextInput *input, int row);

/* Set custom ANSI color escape sequence for dividers.
 * Pass an ANSI SGR sequence (e.g., "\033[38;2;37;160;101m").
 * Pass NULL or "" to reset to default (SGR_DIM).
 */
void tui_textinput_set_divider_color(TuiTextInput *input, const char *color);

/* Set custom ANSI color escape sequence for the prompt.
 * Pass an ANSI SGR sequence (e.g., "\033[38;2;255;6;183m").
 * Pass NULL or "" to reset to default (no color).
 */
void tui_textinput_set_prompt_color(TuiTextInput *input, const char *color);

/* Set the continuation prompt string (shown on lines after the first).
 * Must have the same display width as the main prompt.
 * Pass NULL to fall back to space-padding (default behavior).
 */
void tui_textinput_set_continuation_prompt(TuiTextInput *input,
                                           const char *prompt);

/* Set echo mode: 0 = normal, 1 = masked (show * per codepoint) */
void tui_textinput_set_echo_mode(TuiTextInput *input, int mode);

/* Get echo mode */
int tui_textinput_get_echo_mode(const TuiTextInput *input);

/* Get render height in rows (includes dividers if enabled)
 * Returns 1 for the input line itself, +2 if show_dividers is enabled.
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

#endif /* BLOOM_BOBA_TEXTINPUT_H */
