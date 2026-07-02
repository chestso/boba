/* textinput.c - Multi-line text input component implementation */

#include <boba/ansi_sequences.h>
#include <boba/cmd.h>
#include <boba/components/textinput.h>
#include <boba/unicode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTINPUT_INITIAL_CAP 256
#define TEXTINPUT_TYPE_ID     (TUI_COMPONENT_TYPE_BASE + 1)

/* Check if a character is a word character */
static int is_word_char(const TuiTextInput *input, char c)
{
    if (input->word_chars)
        return strchr(input->word_chars, c) != NULL;
    return c != ' ' && c != '\t'; /* fallback when word_chars not set */
}

/* Display width of the prompt rendered on a given logical line:
 * line 0 uses the main prompt; later lines use the continuation prompt or
 * fall back to spaces of width prompt_len (matches render_continuation_prompt). */
static int line_prompt_width(const TuiTextInput *input, int line_index)
{
    if (!input->show_prompt || !input->prompt || input->prompt_len <= 0)
        return 0;
    if (line_index == 0)
        return input->prompt_len;
    if (input->continuation_prompt && input->continuation_prompt_len > 0)
        return input->continuation_prompt_len;
    return input->prompt_len;
}

/* Byte range of the logical line containing the cursor */
static void cursor_line_bounds(const TuiTextInput *input, size_t *out_start,
                               size_t *out_end)
{
    size_t start = 0;
    for (size_t i = input->cursor_byte; i > 0; i--) {
        if (input->text[i - 1] == '\n') {
            start = i;
            break;
        }
    }
    size_t end = input->text_len;
    for (size_t i = input->cursor_byte; i < input->text_len; i++) {
        if (input->text[i] == '\n') {
            end = i;
            break;
        }
    }
    *out_start = start;
    *out_end = end;
}

/* Adjust horizontal scroll offset so cursor stays visible on its line.
 * offset/offset_right are codepoint indices within the cursor's logical line. */
static void handle_overflow(TuiTextInput *input)
{
    size_t line_start, line_end;
    cursor_line_bounds(input, &line_start, &line_end);
    size_t line_bytes = line_end - line_start;

    int prompt_width = line_prompt_width(input, (int)input->cursor_row);
    int content_width = input->terminal_width - prompt_width;

    int total = tui_utf8_codepoint_count(input->text + line_start, line_bytes);

    if (content_width <= 0 || input->terminal_width == 0) {
        input->offset = 0;
        input->offset_right = total;
        return;
    }

    int cursor_cp = tui_utf8_cp_index(input->text + line_start,
                                      input->cursor_byte - line_start);

    if (total <= content_width) {
        /* Everything fits */
        input->offset = 0;
        input->offset_right = total;
        return;
    }

    if (cursor_cp < input->offset) {
        /* Cursor scrolled left of window */
        input->offset = cursor_cp;
        input->offset_right = input->offset + content_width;
        if (input->offset_right > total)
            input->offset_right = total;
    } else if (cursor_cp >= input->offset_right) {
        /* Cursor scrolled right of window */
        input->offset_right = cursor_cp + 1;
        input->offset = input->offset_right - content_width;
        if (input->offset < 0)
            input->offset = 0;
    } else {
        /* Cursor in view — clamp offset_right */
        input->offset_right = input->offset + content_width;
        if (input->offset_right > total)
            input->offset_right = total;
    }
}

/* Count lines and find cursor position */
static void recalculate_cursor_position(TuiTextInput *input)
{
    input->cursor_row = 0;
    input->cursor_col = 0;

    size_t col = 0;
    for (size_t i = 0; i < input->cursor_byte && i < input->text_len; i++) {
        if (input->text[i] == '\n') {
            input->cursor_row++;
            col = 0;
        } else {
            col++;
        }
    }
    input->cursor_col = col;

    /* Adjust horizontal scroll so cursor stays visible (single- and multi-line).
     * Skip when soft_wrap is on — wrapping replaces scrolling. */
    if (!input->soft_wrap)
        handle_overflow(input);
}

/* Ensure text buffer has enough capacity */
static int ensure_capacity(TuiTextInput *input, size_t needed)
{
    if (input->text_cap >= needed)
        return 0;

    size_t new_cap = input->text_cap;
    if (new_cap == 0)
        new_cap = TEXTINPUT_INITIAL_CAP;
    while (new_cap < needed)
        new_cap *= 2;

    char *new_text = (char *)realloc(input->text, new_cap);
    if (!new_text)
        return -1;

    input->text = new_text;
    input->text_cap = new_cap;
    return 0;
}

/* Clear the selection mark (no-op if no mark is set). Called from edit
 * paths that invalidate any previously-set mark byte offset. */
static void clear_mark_if_set(TuiTextInput *input)
{
    if (input->has_mark) {
        input->has_mark = 0;
        input->mark_byte = 0;
    }
}

/* Return the normalized selection range when has_mark is set:
 * *out_start = min(mark_byte, cursor_byte)
 * *out_end   = max(mark_byte, cursor_byte)
 * Returns 1 if a selection exists, 0 otherwise. */
static int selection_range(const TuiTextInput *input, size_t *out_start,
                           size_t *out_end)
{
    if (!input->has_mark)
        return 0;
    if (input->mark_byte <= input->cursor_byte) {
        *out_start = input->mark_byte;
        *out_end = input->cursor_byte;
    } else {
        *out_start = input->cursor_byte;
        *out_end = input->mark_byte;
    }
    return 1;
}

/* Insert text at cursor position */
static int insert_text(TuiTextInput *input, const char *text, size_t len)
{
    if (len == 0)
        return 0;

    if (ensure_capacity(input, input->text_len + len + 1) < 0)
        return -1;

    /* Move text after cursor */
    if (input->cursor_byte < input->text_len) {
        memmove(input->text + input->cursor_byte + len,
                input->text + input->cursor_byte,
                input->text_len - input->cursor_byte);
    }

    /* Insert new text */
    memcpy(input->text + input->cursor_byte, text, len);
    input->text_len += len;
    input->cursor_byte += len;
    input->text[input->text_len] = '\0';

    clear_mark_if_set(input);
    recalculate_cursor_position(input);
    return 0;
}

/* Insert a single UTF-8 codepoint */
static int insert_codepoint(TuiTextInput *input, uint32_t cp)
{
    char buf[5];
    int len = tui_utf8_encode(cp, buf);
    return insert_text(input, buf, len);
}

/* Delete character before cursor (backspace) */
static void delete_before(TuiTextInput *input)
{
    if (input->cursor_byte == 0)
        return;

    size_t prev = tui_utf8_prev_char(input->text, input->cursor_byte);
    size_t del_len = input->cursor_byte - prev;

    memmove(input->text + prev, input->text + input->cursor_byte,
            input->text_len - input->cursor_byte);
    input->text_len -= del_len;
    input->cursor_byte = prev;
    input->text[input->text_len] = '\0';

    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Delete character at cursor (delete key) */
static void delete_at(TuiTextInput *input)
{
    if (input->cursor_byte >= input->text_len)
        return;

    int char_len = tui_utf8_char_len(input->text + input->cursor_byte);
    size_t del_end = input->cursor_byte + char_len;
    if (del_end > input->text_len)
        del_end = input->text_len;

    memmove(input->text + input->cursor_byte, input->text + del_end,
            input->text_len - del_end);
    input->text_len -= (del_end - input->cursor_byte);
    input->text[input->text_len] = '\0';

    clear_mark_if_set(input);
}

/* Move cursor left by one character */
static void cursor_left(TuiTextInput *input)
{
    if (input->cursor_byte > 0) {
        input->cursor_byte = tui_utf8_prev_char(input->text, input->cursor_byte);
        recalculate_cursor_position(input);
    }
}

/* Move cursor right by one character */
static void cursor_right(TuiTextInput *input)
{
    if (input->cursor_byte < input->text_len) {
        input->cursor_byte += tui_utf8_char_len(input->text + input->cursor_byte);
        if (input->cursor_byte > input->text_len)
            input->cursor_byte = input->text_len;
        recalculate_cursor_position(input);
    }
}

/* Move cursor left by one word (Ctrl+Left) */
static void cursor_word_left(TuiTextInput *input)
{
    if (input->cursor_byte == 0)
        return;

    size_t pos = input->cursor_byte;

    /* Skip non-word characters before the word */
    while (pos > 0 && !is_word_char(input, input->text[pos - 1]))
        pos--;

    /* Skip the word itself */
    while (pos > 0 && is_word_char(input, input->text[pos - 1]))
        pos--;

    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Move cursor right by one word (Ctrl+Right) */
static void cursor_word_right(TuiTextInput *input)
{
    if (input->cursor_byte >= input->text_len)
        return;

    size_t pos = input->cursor_byte;

    /* Skip the current word */
    while (pos < input->text_len && is_word_char(input, input->text[pos]))
        pos++;

    /* Skip non-word characters after the word */
    while (pos < input->text_len && !is_word_char(input, input->text[pos]))
        pos++;

    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Move cursor to start of current line */
static void cursor_home(TuiTextInput *input)
{
    while (input->cursor_byte > 0 &&
           input->text[input->cursor_byte - 1] != '\n') {
        input->cursor_byte--;
    }
    recalculate_cursor_position(input);
}

/* Move cursor to end of current line */
static void cursor_end(TuiTextInput *input)
{
    while (input->cursor_byte < input->text_len &&
           input->text[input->cursor_byte] != '\n') {
        input->cursor_byte++;
    }
    recalculate_cursor_position(input);
}

/* Find line start position for given line number */
static size_t find_line_start(const char *text, size_t len, int line)
{
    if (line <= 0)
        return 0;

    int current_line = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            current_line++;
            if (current_line == line)
                return i + 1;
        }
    }
    return len;
}

/* Find line length (without newline) */
static size_t line_length(const char *text, size_t len, size_t start)
{
    size_t end = start;
    while (end < len && text[end] != '\n') {
        end++;
    }
    return end - start;
}

/* Move cursor up one line */
static void cursor_up(TuiTextInput *input)
{
    if (input->cursor_row == 0)
        return;

    /* Find previous line start */
    size_t prev_line_start =
        find_line_start(input->text, input->text_len, (int)input->cursor_row - 1);
    size_t prev_line_len =
        line_length(input->text, input->text_len, prev_line_start);

    /* Move to same column or end of line if shorter */
    size_t col = input->cursor_col;
    if (col > prev_line_len)
        col = prev_line_len;

    input->cursor_byte = prev_line_start + col;
    recalculate_cursor_position(input);
}

/* Move cursor down one line */
static void cursor_down(TuiTextInput *input)
{
    /* Find next line */
    size_t next_line_start = input->cursor_byte;
    while (next_line_start < input->text_len &&
           input->text[next_line_start] != '\n') {
        next_line_start++;
    }
    if (next_line_start >= input->text_len)
        return;        /* No next line */
    next_line_start++; /* Skip newline */

    size_t next_line_len =
        line_length(input->text, input->text_len, next_line_start);

    /* Move to same column or end of line if shorter */
    size_t col = input->cursor_col;
    if (col > next_line_len)
        col = next_line_len;

    input->cursor_byte = next_line_start + col;
    recalculate_cursor_position(input);
}

/* Save text to kill buffer (append=1 appends to existing buffer) and also
 * push the resulting kill-ring contents to the system clipboard via a
 * TUI_CMD_CLIPBOARD_COPY command. Returns the command (caller threads it
 * back through tui_update_result), or NULL on empty input / OOM.
 *
 * Matches graphical emacs's `interprogram-cut-function = gui-select-text`
 * default — every kill broadcasts to the system clipboard. */
static TuiCmd *kill_save(TuiTextInput *input, const char *text, size_t len,
                         int append)
{
    if (len == 0)
        return NULL;

    if (append && input->kill_buf && input->kill_buf_len > 0) {
        char *new_buf =
            (char *)realloc(input->kill_buf, input->kill_buf_len + len);
        if (!new_buf)
            return NULL;
        memcpy(new_buf + input->kill_buf_len, text, len);
        input->kill_buf = new_buf;
        input->kill_buf_len += len;
    } else {
        free(input->kill_buf);
        input->kill_buf = (char *)malloc(len);
        if (!input->kill_buf) {
            input->kill_buf_len = 0;
            return NULL;
        }
        memcpy(input->kill_buf, text, len);
        input->kill_buf_len = len;
    }

    /* Push the current kill-ring contents (after append) to the system
     * clipboard. Consecutive kills produce a clipboard cmd carrying the
     * accumulated text — same as emacs. */
    return tui_cmd_clipboard_copy(input->kill_buf, input->kill_buf_len);
}

/* Transpose the two characters before the cursor (Ctrl+T) */
static void transpose_chars(TuiTextInput *input)
{
    if (input->cursor_byte == 0 || input->text_len < 2)
        return;

    /* If at end of text, transpose the two chars before cursor.
     * Otherwise, transpose char before and at cursor, then advance. */
    size_t pos = input->cursor_byte;
    if (pos >= input->text_len)
        pos = input->text_len; /* at end */

    /* Find the two characters to swap */
    size_t c2_start, c2_end, c1_start;

    if (pos >= input->text_len) {
        /* At end: swap last two characters */
        c2_end = input->text_len;
        c2_start = tui_utf8_prev_char(input->text, c2_end);
        c1_start = tui_utf8_prev_char(input->text, c2_start);
    } else {
        /* In middle: swap char before cursor with char at cursor */
        c2_start = pos;
        c2_end = pos + tui_utf8_char_len(input->text + pos);
        if (c2_end > input->text_len)
            c2_end = input->text_len;
        c1_start = tui_utf8_prev_char(input->text, c2_start);
    }

    size_t c1_len = c2_start - c1_start;
    size_t c2_len = c2_end - c2_start;

    if (c1_len == 0 || c2_len == 0)
        return;

    /* Swap using a small temp buffer */
    char tmp[8];
    if (c1_len + c2_len > sizeof(tmp))
        return;

    memcpy(tmp, input->text + c2_start, c2_len);
    memcpy(tmp + c2_len, input->text + c1_start, c1_len);
    memcpy(input->text + c1_start, tmp, c1_len + c2_len);

    input->cursor_byte = c2_end;
    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Capture pre-edit state into the reusable snapshot buffer */
static inline void undo_snapshot(TuiTextInput *input)
{
    size_t needed = input->text_len + 1;
    if (input->snap_buf_cap < needed) {
        size_t new_cap = input->snap_buf_cap == 0 ? 256 : input->snap_buf_cap;
        while (new_cap < needed)
            new_cap *= 2;
        char *new_buf = (char *)realloc(input->snap_buf, new_cap);
        if (!new_buf)
            return;
        input->snap_buf = new_buf;
        input->snap_buf_cap = new_cap;
    }
    memcpy(input->snap_buf, input->text, input->text_len + 1);
    input->snap_len = input->text_len;
    input->snap_cursor = input->cursor_byte;
    input->snap_valid = 1;
}

/* Push snapshot onto undo stack only if text actually changed */
static void undo_commit(TuiTextInput *input)
{
    if (!input->snap_valid)
        return;
    input->snap_valid = 0;

    /* Text unchanged — nothing to do */
    if (input->snap_len == input->text_len &&
        memcmp(input->snap_buf, input->text, input->snap_len) == 0) {
        return;
    }

    /* Text was modified — reset history navigation so next arrow-up
     * treats current text as a new prefix search. */
    if (input->history_pos != -1) {
        input->history_pos = -1;
        free(input->saved_input);
        input->saved_input = NULL;
    }

    /* Grow stack if needed */
    if (input->undo_count >= input->undo_cap) {
        int new_cap = input->undo_cap == 0 ? 32 : input->undo_cap * 2;
        void *new_stack =
            realloc(input->undo_stack, new_cap * sizeof(*input->undo_stack));
        if (!new_stack)
            return;
        input->undo_stack = new_stack;
        input->undo_cap = new_cap;
    }

    /* Copy snapshot into the undo stack */
    char *copy = (char *)malloc(input->snap_len + 1);
    if (!copy)
        return;
    memcpy(copy, input->snap_buf, input->snap_len + 1);

    int idx = input->undo_count;
    input->undo_stack[idx].text = copy;
    input->undo_stack[idx].text_len = input->snap_len;
    input->undo_stack[idx].cursor_byte = input->snap_cursor;
    input->undo_count++;
}

/* Restore most recent undo snapshot */
static void undo_pop(TuiTextInput *input)
{
    if (input->undo_count == 0)
        return;

    int idx = input->undo_count - 1;

    /* Restore state */
    if (ensure_capacity(input, input->undo_stack[idx].text_len + 1) < 0)
        return;
    memcpy(input->text, input->undo_stack[idx].text,
           input->undo_stack[idx].text_len + 1);
    input->text_len = input->undo_stack[idx].text_len;
    input->cursor_byte = input->undo_stack[idx].cursor_byte;
    if (input->cursor_byte > input->text_len)
        input->cursor_byte = input->text_len;
    recalculate_cursor_position(input);

    free(input->undo_stack[idx].text);
    input->undo_count--;
}

/* Free the undo stack */
static void undo_free(TuiTextInput *input)
{
    for (int i = 0; i < input->undo_count; i++) {
        free(input->undo_stack[i].text);
    }
    free(input->undo_stack);
    input->undo_stack = NULL;
    input->undo_count = 0;
    input->undo_cap = 0;
}

/* Check if a history entry matches the current prefix filter */
static int history_matches_prefix(const char *entry, const char *prefix,
                                  size_t prefix_len)
{
    if (prefix_len == 0)
        return 1;
    return strncmp(entry, prefix, prefix_len) == 0;
}

/* Navigate to previous history entry (Up arrow / Ctrl+P)
 * When the user has typed a prefix before navigating, only visit
 * history entries that start with that prefix. */
static void history_prev(TuiTextInput *input)
{
    if (!input->history || input->history_count == 0)
        return;

    undo_free(input);
    input->snap_valid = 0;

    /* Save current input if we're at position -1 */
    if (input->history_pos == -1) {
        free(input->saved_input);
        input->saved_input = strdup(input->text);
    }

    /* Search for next matching entry */
    const char *prefix = input->saved_input ? input->saved_input : "";
    size_t prefix_len = strlen(prefix);

    for (int i = input->history_pos + 1; i < input->history_count; i++) {
        if (history_matches_prefix(input->history[i], prefix, prefix_len)) {
            input->history_pos = i;
            tui_textinput_set_text(input, input->history[i]);
            return;
        }
    }
}

/* Navigate to next history entry (Down arrow / Ctrl+N)
 * Respects the same prefix filter as history_prev. */
static void history_next(TuiTextInput *input)
{
    if (input->history_pos < 0)
        return;

    undo_free(input);
    input->snap_valid = 0;

    const char *prefix = input->saved_input ? input->saved_input : "";
    size_t prefix_len = strlen(prefix);

    /* Search for previous matching entry (toward more recent) */
    for (int i = input->history_pos - 1; i >= 0; i--) {
        if (history_matches_prefix(input->history[i], prefix, prefix_len)) {
            input->history_pos = i;
            tui_textinput_set_text(input, input->history[i]);
            return;
        }
    }

    /* No more matches — restore saved input */
    input->history_pos = -1;
    if (input->saved_input) {
        tui_textinput_set_text(input, input->saved_input);
        free(input->saved_input);
        input->saved_input = NULL;
    } else {
        tui_textinput_clear(input);
    }
}

/* Submit current text: clear, reset history, return command */
static TuiUpdateResult line_submit(TuiTextInput *input)
{
    char *line = strdup(input->text);
    tui_textinput_clear(input);
    input->history_pos = -1;
    free(input->saved_input);
    input->saved_input = NULL;
    return tui_update_result(tui_cmd_line_submit(line));
}

/* Replace bytes in text buffer from [start, start+old_len) with new_word */
static void replace_word(TuiTextInput *input, int start, int old_len,
                         const char *new_word)
{
    int new_len = (int)strlen(new_word);
    int tail_start = start + old_len;
    int tail_len = (int)input->text_len - tail_start;
    size_t new_text_len = (size_t)(start + new_len + tail_len);

    if (ensure_capacity(input, new_text_len + 1) < 0)
        return;

    /* Shift tail to make room (or shrink) */
    memmove(input->text + start + new_len, input->text + tail_start,
            (size_t)tail_len);
    /* Copy new word in */
    memcpy(input->text + start, new_word, (size_t)new_len);

    input->text_len = new_text_len;
    input->text[input->text_len] = '\0';
    input->cursor_byte = (size_t)(start + new_len);
    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Create a new text input component */
TuiTextInput *tui_textinput_create(const TuiTextInputConfig *config)
{
    TuiTextInput *input = (TuiTextInput *)malloc(sizeof(TuiTextInput));
    if (!input)
        return NULL;

    memset(input, 0, sizeof(TuiTextInput));
    input->base.type = TEXTINPUT_TYPE_ID;

    /* Allocate initial text buffer */
    input->text_cap = TEXTINPUT_INITIAL_CAP;
    input->text = (char *)malloc(input->text_cap);
    if (!input->text) {
        free(input);
        return NULL;
    }
    input->text[0] = '\0';
    input->text_len = 0;
    input->focused = 1; /* Focused by default */
    input->multiline = 0;
    input->show_prompt = 1;  /* Show prompt by default */
    input->history_pos = -1; /* -1 means we're at current input */

    /* Lipgloss-shaped focused/blurred prompt styles default to plain (no styling). */
    input->focused_prompt_style = tui_style_new();
    input->blurred_prompt_style = tui_style_new();

    /* No word_chars set by default — is_word_char falls back to non-whitespace */
    input->word_chars = NULL;

    /* Apply config if provided */
    if (config) {
        input->prompt = config->prompt;
        if (input->prompt) {
            input->prompt_len = tui_utf8_codepoint_count(input->prompt, strlen(input->prompt));
        }
        input->width = config->width;
        input->height = config->height;
        input->multiline = config->multiline;
    }

    return input;
}

/* Free text input component */
void tui_textinput_free(TuiTextInput *input)
{
    if (!input)
        return;
    if (input->text)
        free(input->text);
    if (input->saved_input)
        free(input->saved_input);
    if (input->history) {
        for (int i = 0; i < input->history_count; i++) {
            free(input->history[i]);
        }
        free(input->history);
    }
    free(input->kill_buf);
    free(input->word_chars);
    free(input->snap_buf);
    undo_free(input);
    free(input);
}

/* Update text input with message */
TuiUpdateResult tui_textinput_update(TuiTextInput *input, TuiMsg msg)
{
    if (!input)
        return tui_update_result_none();

    if (msg.type == TUI_MSG_FOCUS) {
        input->focused = 1;
        return tui_update_result_none();
    }

    if (msg.type == TUI_MSG_BLUR) {
        input->focused = 0;
        return tui_update_result_none();
    }

    if (!input->focused)
        return tui_update_result_none();

    if (msg.type != TUI_MSG_KEY_PRESS)
        return tui_update_result_none();

    /* Ignore key release events (only emitted under Kitty kbd protocol). */
    if (msg.data.key.action == TUI_KEY_ACTION_RELEASE)
        return tui_update_result_none();

    TuiKeyMsg key = msg.data.key;

    /* Handle C-x prefix: if set, check for C-x C-u (undo) */
    if (input->ctrl_x_prefix) {
        input->ctrl_x_prefix = 0;
        if ((key.mods & TUI_MOD_CTRL) && (key.rune == 'u' || key.rune == 'U') &&
            key.key == TUI_KEY_NONE) {
            undo_pop(input);
            return tui_update_result_none();
        }
        /* Not C-u after C-x: fall through to normal handling */
    }

    /* Snapshot pre-edit state; will be pushed to undo stack only if text changes
     */
    undo_snapshot(input);

    /* Track consecutive kill commands for append behavior */
    int was_kill = input->last_was_kill;
    input->last_was_kill = 0;

    /* Pending clipboard command — set by kill operations and M-w, returned
     * via tui_update_result at the function tail. */
    TuiCmd *pending_cmd = NULL;

    /* Handle special keys */
    switch (key.key) {
    case TUI_KEY_LEFT:
        if (key.mods & TUI_MOD_CTRL)
            cursor_word_left(input);
        else
            cursor_left(input);
        break;

    case TUI_KEY_RIGHT:
        if (key.mods & TUI_MOD_CTRL)
            cursor_word_right(input);
        else
            cursor_right(input);
        break;

    case TUI_KEY_UP:
        if (input->multiline && input->cursor_row > 0) {
            cursor_up(input);
        } else {
            history_prev(input);
        }
        break;

    case TUI_KEY_DOWN:
        if (input->multiline) {
            /* Check if there's a next line */
            size_t pos = input->cursor_byte;
            while (pos < input->text_len && input->text[pos] != '\n')
                pos++;
            if (pos < input->text_len) {
                cursor_down(input);
                break;
            }
        }
        history_next(input);
        break;

    case TUI_KEY_TAB:
    {
        /* Shift+Tab is not bound by textinput; let the parent route it. */
        if (key.mods & TUI_MOD_SHIFT)
            return tui_update_result_none();
        /* Find word start by scanning backward from cursor for non-word chars */
        int word_start = (int)input->cursor_byte;
        while (word_start > 0 &&
               is_word_char(input, input->text[word_start - 1])) {
            word_start--;
        }
        int prefix_len = (int)input->cursor_byte - word_start;
        char *prefix = (char *)malloc(prefix_len + 1);
        if (prefix) {
            memcpy(prefix, input->text + word_start, prefix_len);
            prefix[prefix_len] = '\0';
            return tui_update_result(tui_cmd_tab_complete(prefix, word_start));
        }
        break;
    }

    case TUI_KEY_HOME:
        cursor_home(input);
        break;

    case TUI_KEY_END:
        cursor_end(input);
        break;

    case TUI_KEY_BACKSPACE:
        delete_before(input);
        break;

    case TUI_KEY_ESCAPE:
        /* Esc clears an active selection; otherwise no-op. */
        clear_mark_if_set(input);
        break;

    case TUI_KEY_DELETE:
        delete_at(input);
        break;

    case TUI_KEY_ENTER:
        if (input->multiline && (key.mods & TUI_MOD_SHIFT)) {
            /* Shift+Enter in multiline: insert newline */
            insert_codepoint(input, '\n');
        } else {
            return line_submit(input);
        }
        break;

    case TUI_KEY_NONE:
        /* Regular character */
        if (key.rune >= 0x20 || key.rune == '\t') {
            /* Skip control characters with Ctrl modifier (except Tab) */
            if ((key.mods & TUI_MOD_CTRL) && key.rune != '\t') {
                /* Handle Ctrl+key shortcuts */
                if (key.rune == ' ') {
                    /* Ctrl+Space: toggle selection mark at cursor. */
                    if (input->has_mark) {
                        input->has_mark = 0;
                    } else {
                        input->mark_byte = input->cursor_byte;
                        input->has_mark = 1;
                    }
                } else if (key.rune == 'a' || key.rune == 'A') {
                    /* Ctrl+A: Move to start of line */
                    cursor_home(input);
                } else if (key.rune == 'b' || key.rune == 'B') {
                    /* Ctrl+B: Move back one character */
                    cursor_left(input);
                } else if (key.rune == 'd' || key.rune == 'D') {
                    /* Ctrl+D: delete character under cursor.
                     * EOF-on-empty is handled by the component via
                     * TUI_MSG_EOF, not here. The textinput never
                     * decides to quit. */
                    delete_at(input);
                } else if (key.rune == 'e' || key.rune == 'E') {
                    /* Ctrl+E: Move to end of line */
                    cursor_end(input);
                } else if (key.rune == 'f' || key.rune == 'F') {
                    /* Ctrl+F: Move forward one character */
                    cursor_right(input);
                } else if (key.rune == 'g' || key.rune == 'G') {
                    /* Ctrl+G: clear selection mark if set */
                    clear_mark_if_set(input);
                } else if (key.rune == 'h' || key.rune == 'H') {
                    /* Ctrl+H: Delete previous character (backspace) */
                    delete_before(input);
                } else if (key.rune == 'j' || key.rune == 'J') {
                    /* Ctrl+J: Insert newline in multiline, submit in single-line */
                    if (input->multiline) {
                        insert_codepoint(input, '\n');
                    } else {
                        return line_submit(input);
                    }
                } else if (key.rune == 'k' || key.rune == 'K') {
                    /* Ctrl+K: Kill to end of line */
                    size_t end = input->cursor_byte;
                    while (end < input->text_len && input->text[end] != '\n') {
                        end++;
                    }
                    if (end > input->cursor_byte) {
                        pending_cmd =
                            kill_save(input, input->text + input->cursor_byte,
                                      end - input->cursor_byte, was_kill);
                        memmove(input->text + input->cursor_byte, input->text + end,
                                input->text_len - end);
                        input->text_len -= (end - input->cursor_byte);
                        input->text[input->text_len] = '\0';
                        clear_mark_if_set(input);
                    }
                    input->last_was_kill = 1;
                } else if (key.rune == 'n' || key.rune == 'N') {
                    /* Ctrl+N: Next history entry (or cursor down if mid-text) */
                    if (input->multiline) {
                        size_t pos = input->cursor_byte;
                        while (pos < input->text_len && input->text[pos] != '\n')
                            pos++;
                        if (pos < input->text_len) {
                            cursor_down(input);
                            break;
                        }
                    }
                    history_next(input);
                } else if (key.rune == 'p' || key.rune == 'P') {
                    /* Ctrl+P: Previous history entry (or cursor up if mid-text) */
                    if (input->multiline && input->cursor_row > 0) {
                        cursor_up(input);
                        break;
                    }
                    history_prev(input);
                } else if (key.rune == 't' || key.rune == 'T') {
                    /* Ctrl+T: Transpose characters before cursor */
                    transpose_chars(input);
                } else if (key.rune == 'u' || key.rune == 'U') {
                    /* Ctrl+U: Kill to start of line */
                    size_t start = input->cursor_byte;
                    while (start > 0 && input->text[start - 1] != '\n') {
                        start--;
                    }
                    if (start < input->cursor_byte) {
                        size_t del_len = input->cursor_byte - start;
                        pending_cmd =
                            kill_save(input, input->text + start, del_len, 0);
                        memmove(input->text + start, input->text + input->cursor_byte,
                                input->text_len - input->cursor_byte);
                        input->text_len -= del_len;
                        input->cursor_byte = start;
                        input->text[input->text_len] = '\0';
                        clear_mark_if_set(input);
                        recalculate_cursor_position(input);
                    }
                    input->last_was_kill = 1;
                } else if (key.rune == 'w' || key.rune == 'W') {
                    /* Ctrl+W: kill region (when mark is set) or kill word
                     * backward (default). Matches emacs's overload of
                     * kill-region / backward-kill-word. */
                    size_t sel_start, sel_end;
                    if (selection_range(input, &sel_start, &sel_end) &&
                        sel_end > sel_start) {
                        size_t del_len = sel_end - sel_start;
                        pending_cmd = kill_save(input, input->text + sel_start,
                                                del_len, 0);
                        memmove(input->text + sel_start,
                                input->text + sel_end,
                                input->text_len - sel_end);
                        input->text_len -= del_len;
                        if (input->cursor_byte > sel_end)
                            input->cursor_byte -= del_len;
                        else if (input->cursor_byte > sel_start)
                            input->cursor_byte = sel_start;
                        input->text[input->text_len] = '\0';
                        clear_mark_if_set(input);
                        recalculate_cursor_position(input);
                        input->last_was_kill = 1;
                    } else if (input->cursor_byte > 0) {
                        size_t pos = input->cursor_byte;
                        /* Skip non-word characters backward */
                        while (pos > 0 && !is_word_char(input, input->text[pos - 1]))
                            pos--;
                        /* Skip word backward */
                        while (pos > 0 && is_word_char(input, input->text[pos - 1]))
                            pos--;
                        if (pos < input->cursor_byte) {
                            size_t del_len = input->cursor_byte - pos;
                            pending_cmd = kill_save(input, input->text + pos,
                                                    del_len, was_kill);
                            memmove(input->text + pos, input->text + input->cursor_byte,
                                    input->text_len - input->cursor_byte);
                            input->text_len -= del_len;
                            input->cursor_byte = pos;
                            input->text[input->text_len] = '\0';
                            clear_mark_if_set(input);
                            recalculate_cursor_position(input);
                        }
                        input->last_was_kill = 1;
                    }
                } else if (key.rune == 'x' || key.rune == 'X') {
                    /* Ctrl+X: prefix for C-x C-u (undo) */
                    input->ctrl_x_prefix = 1;
                } else if (key.rune == 'y' || key.rune == 'Y') {
                    /* Ctrl+Y: Yank (paste from kill buffer) */
                    if (input->kill_buf && input->kill_buf_len > 0) {
                        insert_text(input, input->kill_buf, input->kill_buf_len);
                    }
                } else if (key.rune == '_') {
                    /* Ctrl+_: Undo */
                    undo_pop(input);
                    return tui_update_result_none();
                }
            } else if ((key.mods & TUI_MOD_ALT) && !(key.mods & TUI_MOD_CTRL)) {
                /* Alt-modified emacs keys. Currently only M-w (copy
                 * region or whole input). Other Alt+key combinations are
                 * intentionally swallowed rather than inserted as text. */
                if (key.rune == 'w' || key.rune == 'W') {
                    size_t sel_start, sel_end;
                    const char *src = NULL;
                    size_t src_len = 0;
                    if (selection_range(input, &sel_start, &sel_end) &&
                        sel_end > sel_start) {
                        src = input->text + sel_start;
                        src_len = sel_end - sel_start;
                    } else if (input->text_len > 0) {
                        /* No mark: copy the whole current input, matching
                         * the viewport's "no-mark = current line" rule. */
                        src = input->text;
                        src_len = input->text_len;
                    }
                    if (src && src_len > 0)
                        pending_cmd = tui_cmd_clipboard_copy(src, src_len);
                    clear_mark_if_set(input);
                }
            } else {
                insert_codepoint(input, key.rune);
            }
        }
        break;

    default:
        break;
    }

    undo_commit(input);
    return tui_update_result(pending_cmd);
}

/* True if a TuiStyle carries any user-visible styling (color or attribute). */
static int style_has_styling(const TuiStyle *s)
{
    if (!s)
        return 0;
    return s->fg.type != TUI_COLOR_NONE || s->bg.type != TUI_COLOR_NONE || s->bold || s->italic || s->underline || s->strikethrough || s->reverse || s->blink || s->faint;
}

/* Pick the right TuiStyle for the input's current focus state. */
static const TuiStyle *prompt_style_for(const TuiTextInput *input)
{
    return input->focused ? &input->focused_prompt_style
                          : &input->blurred_prompt_style;
}

/* Append `content` wrapped in a TuiStyle, or in a legacy raw ANSI prefix,
 * or unstyled. Resolves in that order: TuiStyle > legacy_color > plain. */
static void emit_styled_or_legacy(DynamicBuffer *out, const TuiStyle *style,
                                  const char *legacy_color,
                                  const char *content)
{
    if (style_has_styling(style)) {
        TuiStyle inline_style = *style;
        inline_style.inline_ = 1;
        char *rendered = tui_style_render(&inline_style, content);
        if (rendered) {
            dynamic_buffer_append_str(out, rendered);
            free(rendered);
        }
    } else if (legacy_color && legacy_color[0] != '\0') {
        dynamic_buffer_append_str(out, legacy_color);
        dynamic_buffer_append_str(out, content);
        dynamic_buffer_append_str(out, SGR_RESET);
    } else {
        dynamic_buffer_append_str(out, content);
    }
}

/* Render continuation prompt or space-padding for lines after the first */
static void render_continuation_prompt(const TuiTextInput *input,
                                       DynamicBuffer *out)
{
    if (input->continuation_prompt && input->continuation_prompt_len > 0) {
        emit_styled_or_legacy(out, prompt_style_for(input),
                              input->prompt_color, input->continuation_prompt);
    } else {
        for (int j = 0; j < input->prompt_len; j++)
            dynamic_buffer_append(out, " ", 1);
    }
}

/* Render input->text[byte_start..byte_end) to `out`, applying SGR_REVERSE
 * over any portion that falls inside the active selection (when has_mark
 * is set). Honors echo_mode by emitting '*' per codepoint while still
 * tracking selection toggles at codepoint boundaries. */
static void render_text_range(const TuiTextInput *input, DynamicBuffer *out,
                              size_t byte_start, size_t byte_end)
{
    /* When a highlight callback is set, delegate the entire text range
     * to the callback. It returns a malloc'd styled string that we
     * append and free. Selection/echo handling is skipped — the callback
     * owns all styling. */
    if (input->highlight_fn && byte_end > byte_start) {
        char *styled = input->highlight_fn(input->text + byte_start,
                                           byte_end - byte_start,
                                           input->highlight_userdata);
        if (styled) {
            dynamic_buffer_append_str(out, styled);
            free(styled);
        }
        return;
    }

    size_t sel_start = 0, sel_end = 0;
    int has_sel = selection_range(input, &sel_start, &sel_end);

    int reversed = 0;
    size_t i = byte_start;
    while (i < byte_end) {
        int clen = tui_utf8_char_len(input->text + i);
        if (clen < 1)
            clen = 1;
        if (i + (size_t)clen > byte_end)
            clen = (int)(byte_end - i);

        if (has_sel) {
            int in_sel = (i >= sel_start && i < sel_end);
            if (in_sel && !reversed) {
                dynamic_buffer_append_str(out, SGR_REVERSE);
                reversed = 1;
            } else if (!in_sel && reversed) {
                dynamic_buffer_append_str(out, SGR_REVERSE_OFF);
                reversed = 0;
            }
        }

        if (input->echo_mode == 1)
            dynamic_buffer_append(out, "*", 1);
        else
            dynamic_buffer_append(out, input->text + i, clen);

        i += clen;
    }

    if (reversed)
        dynamic_buffer_append_str(out, SGR_REVERSE_OFF);
}

/* Compute byte range to render for a logical line.
 * Cursor's line: applies horizontal scroll window (offset/offset_right).
 * Other lines: clipped to fit within terminal_width minus the line's prompt. */
static void get_line_render_range(const TuiTextInput *input, size_t line_start,
                                  size_t line_end, int line_index,
                                  size_t *out_start, size_t *out_end)
{
    size_t line_bytes = line_end - line_start;

    if (line_index == (int)input->cursor_row && input->terminal_width > 0 &&
        input->offset_right > input->offset) {
        size_t s = tui_utf8_byte_offset(input->text + line_start, line_bytes,
                                        input->offset);
        size_t e = tui_utf8_byte_offset(input->text + line_start, line_bytes,
                                        input->offset_right);
        *out_start = line_start + s;
        *out_end = line_start + e;
        return;
    }

    if (input->terminal_width > 0) {
        int prompt_width = line_prompt_width(input, line_index);
        int content_width = input->terminal_width - prompt_width;
        if (content_width <= 0) {
            *out_start = line_start;
            *out_end = line_start;
        } else {
            size_t e = tui_utf8_byte_offset(input->text + line_start, line_bytes,
                                            content_width);
            *out_start = line_start;
            *out_end = line_start + e;
        }
        return;
    }

    *out_start = line_start;
    *out_end = line_end;
}

/* Render prompt and visible text slice to output buffer */
static void render_prompt_and_text(const TuiTextInput *input, DynamicBuffer *out)
{
    if (input->show_prompt && input->prompt && input->prompt_len > 0) {
        emit_styled_or_legacy(out, prompt_style_for(input),
                              input->prompt_color, input->prompt);
    }
    if (input->text_len > 0) {
        size_t byte_start = 0;
        size_t byte_end = input->text_len;
        if (input->terminal_width > 0 && input->offset_right > input->offset) {
            byte_start = tui_utf8_byte_offset(input->text, input->text_len,
                                              input->offset);
            byte_end = tui_utf8_byte_offset(input->text, input->text_len,
                                            input->offset_right);
        }
        render_text_range(input, out, byte_start, byte_end);
    }
}

/* Render text input to output buffer.
 *
 * When terminal_row is set (> 0), uses absolute cursor positioning:
 * positions the cursor with CSI row;col H, then emits the input line(s)
 * starting at terminal_row. No relative cursor movements.
 *
 * When terminal_row is 0 (not set), uses legacy relative positioning.
 *
 * Surrounding decoration (border lines, status bars) is the parent's
 * responsibility — see tui_border_render_horizontal() for the lipgloss
 * idiom.
 */
void tui_textinput_view(const TuiTextInput *input, DynamicBuffer *out)
{
    if (!input || !out)
        return;

    char pos_buf[32];

    if (!input->multiline) {
        /* Single-line mode */

        if (input->terminal_row > 0) {
            /* Absolute positioning mode */
            int input_row = input->terminal_row;

            /* Input line */
            snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", input_row);
            dynamic_buffer_append_str(out, pos_buf);
            dynamic_buffer_append_str(out, EL_TO_END);

            render_prompt_and_text(input, out);
            /* Cursor placement is handled by the runtime via cursor(). */
        } else {
            /* Legacy relative positioning mode (terminal_row not set) */

            /* Just clear and render on current line */
            dynamic_buffer_append_str(out, "\r");
            dynamic_buffer_append_str(out, EL_TO_END);

            render_prompt_and_text(input, out);
            /* Cursor placement: relative mode has no absolute coordinate
             * frame; cursor() abstains and the cursor naturally lands at
             * the end of what we just wrote. */
        }
    } else if (input->terminal_row > 0) {
        /* Multi-line mode with absolute positioning */
        int content_start_row = input->terminal_row;

        /* Render each line with absolute positioning */
        int current_line = 0;
        size_t line_start = 0;
        for (size_t i = 0; i <= input->text_len; i++) {
            if (i == input->text_len || input->text[i] == '\n') {
                int row = content_start_row + current_line;
                snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", row);
                dynamic_buffer_append_str(out, pos_buf);
                dynamic_buffer_append_str(out, EL_TO_END);

                /* Prompt or indentation */
                if (current_line == 0 && input->show_prompt && input->prompt &&
                    input->prompt_len > 0) {
                    if (input->prompt_color[0] != '\0')
                        dynamic_buffer_append_str(out, input->prompt_color);
                    dynamic_buffer_append_str(out, input->prompt);
                    if (input->prompt_color[0] != '\0')
                        dynamic_buffer_append_str(out, SGR_RESET);
                } else if (current_line > 0 && input->show_prompt && input->prompt &&
                           input->prompt_len > 0) {
                    render_continuation_prompt(input, out);
                }

                /* Line content (selection-aware), with scroll window / clip */
                size_t r_start, r_end;
                get_line_render_range(input, line_start, i, current_line,
                                      &r_start, &r_end);
                if (r_end > r_start)
                    render_text_range(input, out, r_start, r_end);

                current_line++;
                line_start = i + 1;
            }
        }
        /* Cursor placement is handled by the runtime via cursor(). */
    } else {
        /* Multi-line mode: relative positioning (legacy) */
        /* Clear current line, then render prompt + text per line */
        dynamic_buffer_append_str(out, "\r");
        dynamic_buffer_append_str(out, EL_TO_END);

        /* Output prompt if set and shown */
        if (input->show_prompt && input->prompt && input->prompt_len > 0) {
            if (input->prompt_color[0] != '\0')
                dynamic_buffer_append_str(out, input->prompt_color);
            dynamic_buffer_append_str(out, input->prompt);
            if (input->prompt_color[0] != '\0')
                dynamic_buffer_append_str(out, SGR_RESET);
        }

        /* Output text content per line (selection-aware), with scroll window / clip */
        if (input->text_len > 0) {
            int current_line = 0;
            size_t line_start = 0;
            for (size_t i = 0; i <= input->text_len; i++) {
                if (i == input->text_len || input->text[i] == '\n') {
                    size_t r_start, r_end;
                    get_line_render_range(input, line_start, i, current_line,
                                          &r_start, &r_end);
                    if (r_end > r_start)
                        render_text_range(input, out, r_start, r_end);
                    if (i < input->text_len) {
                        /* Found a real newline — emit row break + continuation */
                        dynamic_buffer_append_str(out, "\r\n");
                        dynamic_buffer_append_str(out, EL_TO_END);
                        if (input->show_prompt && input->prompt &&
                            input->prompt_len > 0) {
                            render_continuation_prompt(input, out);
                        }
                    }
                    current_line++;
                    line_start = i + 1;
                }
            }
        }
    }
}

/* Get current text content */
const char *tui_textinput_text(const TuiTextInput *input)
{
    return input ? input->text : NULL;
}

/* Get text length */
size_t tui_textinput_len(const TuiTextInput *input)
{
    return input ? input->text_len : 0;
}

/* Set text content */
void tui_textinput_set_text(TuiTextInput *input, const char *text)
{
    if (!input)
        return;

    if (!text || *text == '\0') {
        tui_textinput_clear(input);
        return;
    }

    size_t len = strlen(text);
    if (ensure_capacity(input, len + 1) < 0)
        return;

    memcpy(input->text, text, len);
    input->text[len] = '\0';
    input->text_len = len;
    input->cursor_byte = len;
    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Clear text content */
void tui_textinput_clear(TuiTextInput *input)
{
    if (!input)
        return;
    input->text_len = 0;
    input->cursor_byte = 0;
    input->cursor_row = 0;
    input->cursor_col = 0;
    input->offset = 0;
    input->offset_right = 0;
    if (input->text)
        input->text[0] = '\0';
    clear_mark_if_set(input);
    undo_free(input);
}

/* Set focus state */
void tui_textinput_set_focus(TuiTextInput *input, int focused)
{
    if (input)
        input->focused = focused;
}

/* Check if focused */
int tui_textinput_is_focused(const TuiTextInput *input)
{
    return input ? input->focused : 0;
}

/* Get cursor position */
size_t tui_textinput_cursor(const TuiTextInput *input)
{
    return input ? input->cursor_byte : 0;
}

/* Set cursor position */
void tui_textinput_set_cursor(TuiTextInput *input, size_t pos)
{
    if (!input)
        return;
    if (pos > input->text_len)
        pos = input->text_len;
    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Get number of lines */
int tui_textinput_line_count(const TuiTextInput *input)
{
    if (!input || input->text_len == 0)
        return 1;

    int count = 1;
    for (size_t i = 0; i < input->text_len; i++) {
        if (input->text[i] == '\n')
            count++;
    }
    return count;
}

/* Count visual rows for a single logical line (soft-wrap aware).
 * When soft_wrap is off, always 1. When on, ceil(codepoints / content_width). */
static int line_visual_rows(const TuiTextInput *input, size_t line_start,
                            size_t line_end, int line_index)
{
    if (!input || !input->soft_wrap || input->terminal_width <= 0)
        return 1;

    int prompt_w = line_prompt_width(input, line_index);
    int content_w = input->terminal_width - prompt_w;
    if (content_w <= 0)
        return 1;

    int cps = tui_utf8_codepoint_count(input->text + line_start,
                                       line_end - line_start);
    if (cps == 0)
        return 1;
    return (cps + content_w - 1) / content_w; /* ceil division */
}

/* Count total visual rows across all logical lines (soft-wrap aware). */
static int total_visual_rows(const TuiTextInput *input)
{
    if (!input || input->text_len == 0)
        return 1;

    int total = 0;
    size_t line_start = 0;
    int line_idx = 0;
    for (size_t i = 0; i <= input->text_len; i++) {
        if (i == input->text_len || input->text[i] == '\n') {
            total += line_visual_rows(input, line_start, i, line_idx);
            line_start = i + 1;
            line_idx++;
        }
    }
    return total ? total : 1;
}

/* Set maximum history size */
void tui_textinput_set_history_size(TuiTextInput *input, int size)
{
    if (!input || size < 0)
        return;

    /* Free existing history if shrinking */
    if (input->history && size < input->history_count) {
        for (int i = size; i < input->history_count; i++) {
            free(input->history[i]);
        }
        input->history_count = size;
    }

    /* Reallocate or allocate history array */
    if (size == 0) {
        free(input->history);
        input->history = NULL;
    } else {
        char **new_history =
            (char **)realloc(input->history, size * sizeof(char *));
        if (new_history) {
            input->history = new_history;
        }
    }
    input->history_size = size;
    input->history_pos = -1;
}

/* Add a line to history */
void tui_textinput_history_add(TuiTextInput *input, const char *line)
{
    if (!input || !line || !line[0] || input->history_size == 0)
        return;

    /* Allocate history array if needed */
    if (!input->history) {
        input->history = (char **)calloc(input->history_size, sizeof(char *));
        if (!input->history)
            return;
    }

    /* Remove any existing duplicate entry (squash) */
    for (int i = 0; i < input->history_count; i++) {
        if (input->history[i] && strcmp(input->history[i], line) == 0) {
            free(input->history[i]);
            /* Shift entries up to fill the gap */
            for (int j = i; j < input->history_count - 1; j++) {
                input->history[j] = input->history[j + 1];
            }
            input->history[input->history_count - 1] = NULL;
            input->history_count--;
            break;
        }
    }

    /* Make room for new entry at position 0 */
    if (input->history_count >= input->history_size) {
        /* Free oldest entry */
        free(input->history[input->history_size - 1]);
        input->history_count = input->history_size - 1;
    }

    /* Shift existing entries down */
    for (int i = input->history_count; i > 0; i--) {
        input->history[i] = input->history[i - 1];
    }

    /* Add new entry at position 0 */
    input->history[0] = strdup(line);
    if (input->history[0]) {
        input->history_count++;
    }

    /* Reset history position */
    input->history_pos = -1;
}

/* Insert a completion word, replacing the current word from word_start to cursor */
void tui_textinput_insert_completion(TuiTextInput *input, int word_start,
                                     const char *word)
{
    if (!input || !word)
        return;
    int old_len = (int)input->cursor_byte - word_start;
    if (old_len < 0)
        old_len = 0;
    replace_word(input, word_start, old_len, word);
}

/* Set characters that form words for tab completion and word movement */
void tui_textinput_set_word_chars(TuiTextInput *input, const char *chars)
{
    if (!input)
        return;
    free(input->word_chars);
    input->word_chars = chars ? strdup(chars) : NULL;
}

/* Set whether to show the prompt */
void tui_textinput_set_show_prompt(TuiTextInput *input, int show)
{
    if (input)
        input->show_prompt = show;
}

/* Set the prompt string */
void tui_textinput_set_prompt(TuiTextInput *input, const char *prompt)
{
    if (!input)
        return;
    input->prompt = prompt;
    input->prompt_len = prompt ? tui_utf8_codepoint_count(prompt, strlen(prompt)) : 0;
}

/* Set the continuation prompt string */
void tui_textinput_set_continuation_prompt(TuiTextInput *input,
                                           const char *prompt)
{
    if (!input)
        return;
    input->continuation_prompt = prompt;
    input->continuation_prompt_len =
        prompt ? tui_utf8_codepoint_count(prompt, strlen(prompt)) : 0;
}

/* Set echo mode */
void tui_textinput_set_echo_mode(TuiTextInput *input, int mode)
{
    if (input)
        input->echo_mode = mode;
}

/* Set soft wrap mode */
void tui_textinput_set_soft_wrap(TuiTextInput *input, int enable)
{
    if (input)
        input->soft_wrap = enable ? 1 : 0;
}

/* Get echo mode */
int tui_textinput_get_echo_mode(const TuiTextInput *input)
{
    return input ? input->echo_mode : 0;
}

/* Set styled-text highlight callback */
void tui_textinput_set_text_renderer(TuiTextInput *input,
                                     char *(*fn)(const char *text,
                                                 size_t len,
                                                 void *userdata),
                                     void *userdata)
{
    if (input) {
        input->highlight_fn = fn;
        input->highlight_userdata = userdata;
    }
}

void tui_textinput_set_prompt_color(TuiTextInput *input, const char *color)
{
    if (!input)
        return;
    if (color && color[0] != '\0') {
        snprintf(input->prompt_color, sizeof(input->prompt_color), "%s", color);
    } else {
        input->prompt_color[0] = '\0';
    }
}

void tui_textinput_set_focused_prompt_style(TuiTextInput *input, TuiStyle s)
{
    if (input)
        input->focused_prompt_style = s;
}

void tui_textinput_set_blurred_prompt_style(TuiTextInput *input, TuiStyle s)
{
    if (input)
        input->blurred_prompt_style = s;
}

/* Set width used for line wrapping / horizontal overflow */
void tui_textinput_set_terminal_width(TuiTextInput *input, int width)
{
    if (input) {
        input->terminal_width = width;
        recalculate_cursor_position(input);
    }
}

/* Set terminal row for absolute positioning */
void tui_textinput_set_terminal_row(TuiTextInput *input, int row)
{
    if (input)
        input->terminal_row = row;
}

/* Get render height in rows */
int tui_textinput_get_height(const TuiTextInput *input)
{
    if (!input)
        return 1;
    if (!input->multiline)
        return input->soft_wrap ? total_visual_rows(input) : 1;
    return input->soft_wrap ? total_visual_rows(input)
                            : tui_textinput_line_count(input);
}

/* Report cursor position for the runtime. Mirrors the arithmetic the old
 * view() used internally before cursor placement was hoisted out. */
TuiCursor tui_textinput_cursor_pos(const TuiTextInput *input)
{
    if (!input || !input->focused)
        return tui_cursor_hidden();

    int base_row = input->terminal_row > 0 ? input->terminal_row : 1;

    /* Soft-wrap mode: compute visual row/col across wrapped lines */
    if (input->soft_wrap && input->terminal_width > 0) {
        int prompt_w = (input->show_prompt && input->prompt)
                           ? input->prompt_len
                           : 0;
        int content_w = input->terminal_width - prompt_w;
        if (content_w <= 0)
            content_w = 1;

        /* Walk through logical lines, counting visual rows */
        int visual_row = 0;
        size_t line_start = 0;
        int line_idx = 0;
        for (size_t i = 0; i <= input->text_len; i++) {
            if (i == input->text_len || input->text[i] == '\n') {
                int line_prompt_w = line_prompt_width(input, line_idx);
                int line_content_w = input->terminal_width - line_prompt_w;
                if (line_content_w <= 0)
                    line_content_w = 1;

                int line_cps = tui_utf8_codepoint_count(
                    input->text + line_start, i - line_start);

                if (line_idx == (int)input->cursor_row) {
                    /* Found cursor's logical line */
                    int cursor_cp = tui_utf8_cp_index(
                        input->text + line_start,
                        input->cursor_byte - line_start);
                    int wrap_row = cursor_cp / line_content_w;
                    int wrap_col = cursor_cp % line_content_w;
                    int row = base_row + visual_row + wrap_row;
                    int col = line_prompt_w + wrap_col + 1;
                    return tui_cursor_at(row, col);
                }

                visual_row += (line_cps == 0)
                                  ? 1
                                  : (line_cps + line_content_w - 1) /
                                        line_content_w;
                line_start = i + 1;
                line_idx++;
            }
        }
        return tui_cursor_at(base_row, prompt_w + 1);
    }

    if (!input->multiline) {
        int prompt_width =
            (input->show_prompt && input->prompt) ? input->prompt_len : 0;
        int cursor_cp = tui_utf8_cp_index(input->text, input->cursor_byte);
        int col = prompt_width + (cursor_cp - input->offset) + 1;
        return tui_cursor_at(base_row, col);
    }

    int prompt_width = line_prompt_width(input, (int)input->cursor_row);
    int col = (int)input->cursor_col - input->offset + prompt_width + 1;
    int row = base_row + (int)input->cursor_row;
    return tui_cursor_at(row, col);
}

/* Component interface wrappers */
static TuiInitResult textinput_init(void *config)
{
    TuiModel *model =
        (TuiModel *)tui_textinput_create((const TuiTextInputConfig *)config);
    return tui_init_result_none(model);
}

static TuiUpdateResult textinput_update(TuiModel *model, TuiMsg msg)
{
    return tui_textinput_update((TuiTextInput *)model, msg);
}

static TuiView textinput_view_slot(const TuiModel *model, DynamicBuffer *out)
{
    const TuiTextInput *input = (const TuiTextInput *)model;
    tui_textinput_view(input, out);
    TuiView v = tui_view_default(out);
    v.cursor = tui_textinput_cursor_pos(input);
    return v;
}

static void textinput_free(TuiModel *model)
{
    tui_textinput_free((TuiTextInput *)model);
}

/* Static component interface instance */
static const TuiComponent textinput_component = {
    .init = textinput_init,
    .update = textinput_update,
    .view = textinput_view_slot,
    .free = textinput_free,
};

/* Get component interface for text input */
const TuiComponent *tui_textinput_component(void)
{
    return &textinput_component;
}
