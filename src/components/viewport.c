/* viewport.c - Software-scrolling viewport implementation
 *
 * Implements Bubbletea-style software scrolling:
 * - Lines stored in memory
 * - Visible lines calculated from y_offset (visual line index)
 * - Rendering uses absolute cursor positioning
 * - No ANSI scroll regions
 * - Supports wrap mode (long lines wrap) and clip mode (truncate at width)
 */

#include <bloom-boba/ansi_sequences.h>
#include <bloom-boba/cmd.h>
#include <bloom-boba/components/viewport.h>
#include <bloom-boba/unicode.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEWPORT_TYPE_ID      (TUI_COMPONENT_TYPE_BASE + 20)
#define INITIAL_LINE_CAPACITY 64
#define SGR_STATE_BUF_SIZE    256

/* Emit up to max_cols display columns from text[*pos..len) into out.
 *
 * If sel_end > sel_start, applies SGR_REVERSE to display columns in the
 * range [sel_start, sel_end) (local 0-indexed). The selection range is in
 * the same display-column space as the returned column count.
 *
 * Returns number of display columns emitted. */
static int emit_cols(const char *text, size_t len, size_t *pos, int max_cols,
                     DynamicBuffer *out, int sel_start, int sel_end)
{
    int col = 0;
    int in_escape = 0;
    int reversed = 0;
    int has_sel = sel_end > sel_start;

    for (; *pos < len; (*pos)++) {
        unsigned char ch = (unsigned char)text[*pos];
        if (in_escape) {
            dynamic_buffer_append(out, &text[*pos], 1);
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
                in_escape = 0;
        } else if (ch == '\033' && *pos + 1 < len && text[*pos + 1] == '[') {
            in_escape = 1;
            dynamic_buffer_append(out, &text[*pos], 1); /* ESC */
        } else if (ch >= 0x20) {
            int clen = tui_utf8_char_len(&text[*pos]);
            /* Clamp to remaining bytes */
            if (*pos + clen > len)
                clen = (int)(len - *pos);
            uint32_t cp = tui_utf8_decode(&text[*pos], clen);
            int w = tui_codepoint_width(cp);
            if (w > 0 && col + w > max_cols)
                break;

            if (has_sel) {
                int in_sel = (col >= sel_start && col < sel_end);
                if (in_sel && !reversed) {
                    dynamic_buffer_append_str(out, SGR_REVERSE);
                    reversed = 1;
                } else if (!in_sel && reversed) {
                    dynamic_buffer_append_str(out, SGR_REVERSE_OFF);
                    reversed = 0;
                }
            }

            col += w;
            dynamic_buffer_append(out, &text[*pos], clen);
            *pos += clen - 1; /* -1 because loop increments */
        } else {
            dynamic_buffer_append(out, &text[*pos], 1);
        }
    }

    if (reversed)
        dynamic_buffer_append_str(out, SGR_REVERSE_OFF);

    return col;
}

/* Calculate how many visual (screen) rows a line occupies */
static int calc_visual_line_count(size_t display_width, int viewport_width,
                                  int wrap_mode)
{
    if (!wrap_mode || viewport_width <= 0 || display_width == 0)
        return 1;
    return (int)((display_width + viewport_width - 1) / viewport_width);
}

/* Recompute visual_lines for all lines and total_visual_lines */
static void recompute_all_visual_lines(TuiViewport *vp)
{
    vp->total_visual_lines = 0;
    for (size_t i = 0; i < vp->line_count; i++) {
        vp->lines[i].visual_lines = calc_visual_line_count(
            vp->lines[i].display_width, vp->width, vp->wrap_mode);
        vp->total_visual_lines += vp->lines[i].visual_lines;
    }
}

/* Clamp y_offset to valid range based on total_visual_lines */
static void clamp_y_offset(TuiViewport *vp)
{
    if (vp->total_visual_lines > (size_t)vp->height) {
        size_t max_offset = vp->total_visual_lines - vp->height;
        if (vp->y_offset > max_offset)
            vp->y_offset = max_offset;
    } else {
        vp->y_offset = 0;
    }
}

/* Add a line to the viewport */
static int add_line(TuiViewport *vp, const char *text, size_t len)
{
    /* Grow array if needed */
    if (vp->line_count >= vp->line_capacity) {
        size_t new_cap = vp->line_capacity * 2;
        TuiViewportLine *new_lines =
            realloc(vp->lines, new_cap * sizeof(TuiViewportLine));
        if (!new_lines)
            return -1;
        vp->lines = new_lines;
        vp->line_capacity = new_cap;
    }

    /* Allocate and copy line text */
    char *line_text = malloc(len + 1);
    if (!line_text)
        return -1;
    memcpy(line_text, text, len);
    line_text[len] = '\0';

    /* Add to array */
    TuiViewportLine *line = &vp->lines[vp->line_count];
    line->text = line_text;
    line->len = len;
    line->display_width = tui_utf8_display_width_ansi(text, len);
    line->visual_lines =
        calc_visual_line_count(line->display_width, vp->width, vp->wrap_mode);
    vp->total_visual_lines += line->visual_lines;
    vp->line_count++;

    return 0;
}

/* Append text to the last line in-place, creating one if needed */
static int append_to_last_line(TuiViewport *vp, const char *text, size_t len)
{
    if (vp->line_count == 0) {
        if (add_line(vp, "", 0) < 0)
            return -1;
    }
    TuiViewportLine *last = &vp->lines[vp->line_count - 1];
    char *new_text = realloc(last->text, last->len + len + 1);
    if (!new_text)
        return -1;
    memcpy(new_text + last->len, text, len);
    last->len += len;
    new_text[last->len] = '\0';
    last->text = new_text;
    last->display_width = tui_utf8_display_width_ansi(last->text, last->len);

    /* Update visual line count */
    vp->total_visual_lines -= last->visual_lines;
    last->visual_lines =
        calc_visual_line_count(last->display_width, vp->width, vp->wrap_mode);
    vp->total_visual_lines += last->visual_lines;

    return 0;
}

/* Trim old lines if exceeding max_lines */
static void trim_old_lines(TuiViewport *vp)
{
    if (vp->max_lines <= 0 || vp->line_count <= (size_t)vp->max_lines)
        return;

    size_t to_remove = vp->line_count - vp->max_lines;

    /* Free old lines and subtract their visual lines */
    size_t visual_removed = 0;
    for (size_t i = 0; i < to_remove; i++) {
        visual_removed += vp->lines[i].visual_lines;
        vp->total_visual_lines -= vp->lines[i].visual_lines;
        free(vp->lines[i].text);
    }

    /* Shift remaining lines */
    memmove(vp->lines, vp->lines + to_remove,
            (vp->line_count - to_remove) * sizeof(TuiViewportLine));
    vp->line_count -= to_remove;

    /* Adjust y_offset for removed content, then clamp */
    if (vp->y_offset > visual_removed) {
        vp->y_offset -= visual_removed;
    } else {
        vp->y_offset = 0;
    }
    clamp_y_offset(vp);
}

/* Find which content line contains the given visual line offset.
 * Returns the content line index and sets *sub_line to the visual row
 * offset within that content line (0-indexed). */
static size_t find_content_line_for_visual(const TuiViewport *vp,
                                           size_t visual_offset,
                                           int *sub_line)
{
    size_t accumulated = 0;
    for (size_t i = 0; i < vp->line_count; i++) {
        size_t next = accumulated + vp->lines[i].visual_lines;
        if (visual_offset < next) {
            *sub_line = (int)(visual_offset - accumulated);
            return i;
        }
        accumulated = next;
    }
    /* Past end — return last line */
    *sub_line = 0;
    return vp->line_count;
}

/* Render the sub_line-th visual row of a content line to the output buffer.
 * Handles ANSI state replay for wrapped continuation rows.
 *
 * If sel_end > sel_start, the [sel_start, sel_end) range (in local display
 * columns within this row) is rendered with SGR_REVERSE. */
static void render_line_segment(const TuiViewportLine *line, int viewport_width,
                                int sub_line, int wrap_mode,
                                DynamicBuffer *out, int sel_start, int sel_end)
{
    const char *text = line->text;
    size_t len = line->len;

    if (!wrap_mode || sub_line == 0) {
        /* Clip mode or first visual row: emit up to viewport_width display cols */
        size_t pos = 0;
        emit_cols(text, len, &pos, viewport_width, out, sel_start, sel_end);
        /* Reset at end to prevent color bleeding */
        dynamic_buffer_append_str(out, SGR_RESET);
    } else {
        /* Wrap mode, sub_line > 0: skip first sub_line * viewport_width display
         * columns, collecting ANSI SGR state, then emit next viewport_width cols */
        int skip_target = sub_line * viewport_width;
        int skipped = 0;
        int in_escape = 0;

        /* SGR state buffer — accumulates active SGR sequences */
        char sgr_buf[SGR_STATE_BUF_SIZE];
        size_t sgr_len = 0;

        /* Track start of current CSI sequence for SGR capture */
        size_t csi_start = 0;
        int in_csi = 0;

        size_t i = 0;
        /* Phase 1: skip display columns, collecting ANSI state */
        for (; i < len && skipped < skip_target; i++) {
            if (in_escape) {
                if ((text[i] >= 'A' && text[i] <= 'Z') ||
                    (text[i] >= 'a' && text[i] <= 'z')) {
                    in_escape = 0;
                    /* Check if this was an SGR sequence (ends with 'm') */
                    if (text[i] == 'm' && in_csi) {
                        size_t seq_len = i - csi_start + 1;
                        /* Check for reset: ESC[0m or ESC[m */
                        int is_reset = 0;
                        if (seq_len == 4 && text[csi_start + 2] == '0')
                            is_reset = 1; /* ESC[0m */
                        if (seq_len == 3)
                            is_reset = 1; /* ESC[m */
                        if (is_reset) {
                            sgr_len = 0;
                        } else if (sgr_len + seq_len < SGR_STATE_BUF_SIZE) {
                            memcpy(sgr_buf + sgr_len, text + csi_start, seq_len);
                            sgr_len += seq_len;
                        }
                    }
                    in_csi = 0;
                }
            } else if (text[i] == '\033' && i + 1 < len && text[i + 1] == '[') {
                in_escape = 1;
                in_csi = 1;
                csi_start = i;
                i++; /* Skip '[' */
            } else if ((unsigned char)text[i] >= 0x20) {
                int clen = tui_utf8_char_len(&text[i]);
                if (i + clen > len)
                    clen = (int)(len - i);
                uint32_t cp = tui_utf8_decode(&text[i], clen);
                skipped += tui_codepoint_width(cp);
                i += clen - 1; /* -1 because loop increments */
            }
        }

        /* Emit accumulated SGR state before visible content */
        if (sgr_len > 0) {
            dynamic_buffer_append(out, sgr_buf, sgr_len);
        }

        /* Phase 2: emit next viewport_width display columns */
        emit_cols(text, len, &i, viewport_width, out, sel_start, sel_end);
        /* Reset at end to prevent color bleeding */
        dynamic_buffer_append_str(out, SGR_RESET);
    }
}

/* Create a new viewport */
TuiViewport *tui_viewport_create(void)
{
    TuiViewport *vp = calloc(1, sizeof(TuiViewport));
    if (!vp)
        return NULL;

    vp->base.type = VIEWPORT_TYPE_ID;

    /* Allocate line array */
    vp->lines = malloc(INITIAL_LINE_CAPACITY * sizeof(TuiViewportLine));
    if (!vp->lines) {
        free(vp);
        return NULL;
    }
    vp->line_capacity = INITIAL_LINE_CAPACITY;
    vp->line_count = 0;

    /* Defaults */
    vp->width = 80;
    vp->height = 24;
    vp->y_offset = 0;
    vp->auto_scroll = 1;
    vp->max_lines = 10000; /* Reasonable default */
    vp->render_row = 1;
    vp->render_col = 1;
    vp->wrap_mode = 1; /* Wrap by default */
    vp->total_visual_lines = 0;

    return vp;
}

/* Free viewport */
void tui_viewport_free(TuiViewport *vp)
{
    if (!vp)
        return;

    /* Free all lines */
    for (size_t i = 0; i < vp->line_count; i++) {
        free(vp->lines[i].text);
    }
    free(vp->lines);
    free(vp);
}

/* Append text to the viewport */
void tui_viewport_append(TuiViewport *vp, const char *text, size_t len)
{
    if (!vp || !text || len == 0)
        return;

    int was_at_bottom = tui_viewport_at_bottom(vp);

    const char *start = text;
    const char *end = text + len;

    while (start < end) {
        /* Find next newline */
        const char *nl = memchr(start, '\n', end - start);

        if (nl) {
            /* Found newline - append segment to last line, then start a new line */
            size_t seg_len = nl - start;

            /* Handle \r\n by stripping \r */
            if (seg_len > 0 && start[seg_len - 1] == '\r') {
                seg_len--;
            }

            /* Append this segment to the current (last) line */
            if (seg_len > 0) {
                append_to_last_line(vp, start, seg_len);
            }

            /* Start a new empty line */
            add_line(vp, "", 0);

            start = nl + 1;
            /* Skip \r after \n (handle \n\r line endings) */
            if (start < end && *start == '\r') {
                start++;
            }
        } else {
            /* No newline - append to last line */
            size_t seg_len = end - start;
            append_to_last_line(vp, start, seg_len);
            break;
        }
    }

    /* Trim old lines if needed */
    trim_old_lines(vp);

    /* Auto-scroll to bottom if enabled and was at bottom */
    if (vp->auto_scroll && was_at_bottom) {
        tui_viewport_scroll_to_bottom(vp);
    }
}

/* Clear all content */
void tui_viewport_clear(TuiViewport *vp)
{
    if (!vp)
        return;

    for (size_t i = 0; i < vp->line_count; i++) {
        free(vp->lines[i].text);
    }
    vp->line_count = 0;
    vp->y_offset = 0;
    vp->total_visual_lines = 0;
}

/* Scroll up by N lines */
void tui_viewport_scroll_up(TuiViewport *vp, int lines)
{
    if (!vp || lines <= 0)
        return;

    if ((size_t)lines > vp->y_offset) {
        vp->y_offset = 0;
    } else {
        vp->y_offset -= lines;
    }
}

/* Scroll down by N lines */
void tui_viewport_scroll_down(TuiViewport *vp, int lines)
{
    if (!vp || lines <= 0)
        return;

    vp->y_offset += lines;

    /* Clamp to valid range */
    size_t max_offset = 0;
    if (vp->total_visual_lines > (size_t)vp->height) {
        max_offset = vp->total_visual_lines - vp->height;
    }
    if (vp->y_offset > max_offset) {
        vp->y_offset = max_offset;
    }
}

/* Page up */
void tui_viewport_page_up(TuiViewport *vp)
{
    if (vp) {
        tui_viewport_scroll_up(vp, vp->height);
    }
}

/* Page down */
void tui_viewport_page_down(TuiViewport *vp)
{
    if (vp) {
        tui_viewport_scroll_down(vp, vp->height);
    }
}

/* Scroll to bottom */
void tui_viewport_scroll_to_bottom(TuiViewport *vp)
{
    if (!vp)
        return;

    if (vp->total_visual_lines > (size_t)vp->height) {
        vp->y_offset = vp->total_visual_lines - vp->height;
    } else {
        vp->y_offset = 0;
    }
}

/* Check if at bottom */
int tui_viewport_at_bottom(const TuiViewport *vp)
{
    if (!vp)
        return 1;

    if (vp->total_visual_lines <= (size_t)vp->height) {
        return 1;
    }

    return vp->y_offset >= vp->total_visual_lines - vp->height;
}

/* Set viewport size */
void tui_viewport_set_size(TuiViewport *vp, int width, int height)
{
    if (!vp)
        return;

    vp->width = width > 0 ? width : 1;
    vp->height = height > 0 ? height : 1;

    /* Recompute visual lines (width may have changed) */
    recompute_all_visual_lines(vp);
    clamp_y_offset(vp);
}

/* Set render position */
void tui_viewport_set_render_position(TuiViewport *vp, int row, int col)
{
    if (!vp)
        return;

    vp->render_row = row > 0 ? row : 1;
    vp->render_col = col > 0 ? col : 1;
}

/* Set max lines */
void tui_viewport_set_max_lines(TuiViewport *vp, int max)
{
    if (!vp)
        return;

    vp->max_lines = max;
    trim_old_lines(vp);
}

/* Set auto scroll */
void tui_viewport_set_auto_scroll(TuiViewport *vp, int enabled)
{
    if (vp) {
        vp->auto_scroll = enabled ? 1 : 0;
    }
}

/* Set wrap mode */
void tui_viewport_set_wrap_mode(TuiViewport *vp, int wrap)
{
    if (!vp)
        return;

    vp->wrap_mode = wrap ? 1 : 0;
    recompute_all_visual_lines(vp);
    clamp_y_offset(vp);
}

/* Get line count */
size_t tui_viewport_line_count(const TuiViewport *vp)
{
    return vp ? vp->line_count : 0;
}

/* Set focus state */
void tui_viewport_set_focused(TuiViewport *vp, int focused)
{
    if (vp)
        vp->focused = focused ? 1 : 0;
}

/* --- Copy-mode helpers --- */

/* Display width (in columns) of the visual line at index v. Wrapped lines
 * report viewport_width for non-final rows and the remainder for the final
 * row; non-wrapping (clip-mode) lines report the full content width capped
 * to viewport_width. */
static int visual_line_width(const TuiViewport *vp, size_t v)
{
    if (vp->line_count == 0)
        return 0;
    int sub_line = 0;
    size_t cidx = find_content_line_for_visual(vp, v, &sub_line);
    if (cidx >= vp->line_count)
        return 0;
    const TuiViewportLine *line = &vp->lines[cidx];
    if (!vp->wrap_mode || line->visual_lines <= 1) {
        int w = (int)line->display_width;
        if (w > vp->width)
            w = vp->width;
        return w;
    }
    if (sub_line < line->visual_lines - 1)
        return vp->width;
    int rem = (int)line->display_width - sub_line * vp->width;
    if (rem < 0)
        rem = 0;
    return rem;
}

/* Clamp cursor to valid scrollback + visual-line-width bounds. */
static void clamp_cursor(TuiViewport *vp)
{
    if (vp->total_visual_lines == 0) {
        vp->cursor_visual_line = 0;
        vp->cursor_col = 0;
        return;
    }
    if (vp->cursor_visual_line >= vp->total_visual_lines)
        vp->cursor_visual_line = vp->total_visual_lines - 1;

    int w = visual_line_width(vp, vp->cursor_visual_line);
    if ((int)vp->cursor_col > w)
        vp->cursor_col = (size_t)w;
}

/* Adjust y_offset so the cursor sits inside the visible viewport. */
static void scroll_to_cursor(TuiViewport *vp)
{
    if (!vp->copy_mode || vp->total_visual_lines == 0)
        return;
    if (vp->cursor_visual_line < vp->y_offset) {
        vp->y_offset = vp->cursor_visual_line;
    } else if (vp->cursor_visual_line >= vp->y_offset + (size_t)vp->height) {
        vp->y_offset = vp->cursor_visual_line - (size_t)vp->height + 1;
    }
    clamp_y_offset(vp);
}

/* Move cursor by N visual lines (signed). */
static void cursor_move_lines(TuiViewport *vp, int delta)
{
    if (vp->total_visual_lines == 0)
        return;
    if (delta < 0) {
        size_t n = (size_t)(-delta);
        vp->cursor_visual_line =
            (vp->cursor_visual_line > n) ? vp->cursor_visual_line - n : 0;
    } else {
        size_t n = (size_t)delta;
        size_t maxv = vp->total_visual_lines - 1;
        vp->cursor_visual_line = (vp->cursor_visual_line + n > maxv)
                                     ? maxv
                                     : vp->cursor_visual_line + n;
    }
    clamp_cursor(vp);
    scroll_to_cursor(vp);
}

/* Move cursor by N columns within the current visual line (signed). */
static void cursor_move_cols(TuiViewport *vp, int delta)
{
    int w = visual_line_width(vp, vp->cursor_visual_line);
    if (delta < 0) {
        size_t n = (size_t)(-delta);
        vp->cursor_col = (vp->cursor_col > n) ? vp->cursor_col - n : 0;
    } else {
        size_t target = vp->cursor_col + (size_t)delta;
        if ((int)target > w)
            target = (size_t)w;
        vp->cursor_col = target;
    }
}

/* Place cursor at the top-left of the currently visible region. */
void tui_viewport_enter_copy_mode(TuiViewport *vp)
{
    if (!vp)
        return;
    vp->copy_mode = 1;
    vp->has_mark = 0;
    vp->cursor_visual_line = vp->y_offset;
    vp->cursor_col = 0;
    clamp_cursor(vp);
}

void tui_viewport_exit_copy_mode(TuiViewport *vp)
{
    if (!vp)
        return;
    vp->copy_mode = 0;
    vp->has_mark = 0;
    vp->mouse_dragging = 0;
}

int tui_viewport_contains(const TuiViewport *vp, int row, int col)
{
    if (!vp)
        return 0;
    int local_row = row - vp->render_row;
    int local_col = col - vp->render_col;
    return local_row >= 0 && local_row < vp->height && local_col >= 0 &&
           local_col < vp->width;
}

/* --- Selection extraction --- */

/* Return the byte offset in `text` where display column `target` begins.
 * ANSI CSI sequences are skipped (count as zero width). If target is past
 * the end of the displayable text, returns `len`. */
static size_t byte_offset_for_display_col(const char *text, size_t len,
                                          int target)
{
    if (target <= 0)
        return 0;
    size_t i = 0;
    int col = 0;
    int in_escape = 0;
    while (i < len) {
        unsigned char ch = (unsigned char)text[i];
        if (in_escape) {
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
                in_escape = 0;
            i++;
        } else if (ch == '\033' && i + 1 < len && text[i + 1] == '[') {
            in_escape = 1;
            i++; /* ESC */
        } else if (ch >= 0x20) {
            int clen = tui_utf8_char_len(&text[i]);
            if (i + clen > len)
                clen = (int)(len - i);
            uint32_t cp = tui_utf8_decode(&text[i], clen);
            int w = tui_codepoint_width(cp);
            if (col + w > target)
                break;
            col += w;
            i += clen;
        } else {
            i++;
        }
    }
    return i;
}

/* Append text[start..end), with ANSI CSI sequences stripped, to `out`. */
static void append_stripped(DynamicBuffer *out, const char *text, size_t start,
                            size_t end)
{
    int in_escape = 0;
    for (size_t i = start; i < end; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (in_escape) {
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
                in_escape = 0;
        } else if (ch == '\033' && i + 1 < end && text[i + 1] == '[') {
            in_escape = 1;
        } else {
            dynamic_buffer_append(out, &text[i], 1);
        }
    }
}

void tui_viewport_extract_selection(const TuiViewport *vp, char **out_text,
                                    size_t *out_len)
{
    if (out_text)
        *out_text = NULL;
    if (out_len)
        *out_len = 0;
    if (!vp || !out_text || !out_len)
        return;
    if (vp->line_count == 0)
        return;

    /* Determine selection range in (visual_line, col) coords. */
    size_t start_v, end_v;
    int start_col, end_col;
    int whole_content_line = 0;

    if (vp->has_mark) {
        int cursor_first = (vp->cursor_visual_line < vp->mark_visual_line) ||
                           (vp->cursor_visual_line == vp->mark_visual_line &&
                            vp->cursor_col <= vp->mark_col);
        if (cursor_first) {
            start_v = vp->cursor_visual_line;
            start_col = (int)vp->cursor_col;
            end_v = vp->mark_visual_line;
            end_col = (int)vp->mark_col + 1;
        } else {
            start_v = vp->mark_visual_line;
            start_col = (int)vp->mark_col;
            end_v = vp->cursor_visual_line;
            end_col = (int)vp->cursor_col + 1;
        }
    } else {
        /* No mark: copy the entire content line containing the cursor. */
        start_v = end_v = vp->cursor_visual_line;
        start_col = 0;
        end_col = INT_MAX;
        whole_content_line = 1;
    }

    /* Translate (visual_line, col) -> (content_idx, display_col). */
    int start_sub = 0, end_sub = 0;
    size_t start_cidx = find_content_line_for_visual(vp, start_v, &start_sub);
    size_t end_cidx = find_content_line_for_visual(vp, end_v, &end_sub);

    int start_disp = start_sub * vp->width + start_col;
    int end_disp;
    if (end_col == INT_MAX) {
        end_disp = INT_MAX;
    } else {
        end_disp = end_sub * vp->width + end_col;
    }

    if (whole_content_line) {
        start_disp = 0;
        end_disp = INT_MAX;
    }

    DynamicBuffer *buf = dynamic_buffer_create(64);
    if (!buf)
        return;

    for (size_t cidx = start_cidx; cidx <= end_cidx && cidx < vp->line_count;
         cidx++) {
        const TuiViewportLine *line = &vp->lines[cidx];
        int from_disp = (cidx == start_cidx) ? start_disp : 0;
        int to_disp = (cidx == end_cidx) ? end_disp : INT_MAX;

        size_t from_byte = byte_offset_for_display_col(line->text, line->len,
                                                       from_disp);
        size_t to_byte;
        if (to_disp == INT_MAX || to_disp >= (int)line->display_width) {
            to_byte = line->len;
        } else {
            to_byte =
                byte_offset_for_display_col(line->text, line->len, to_disp);
        }

        if (cidx > start_cidx)
            dynamic_buffer_append(buf, "\n", 1);
        if (to_byte > from_byte)
            append_stripped(buf, line->text, from_byte, to_byte);
    }

    size_t blen = dynamic_buffer_len(buf);
    if (blen > 0) {
        char *p = (char *)malloc(blen);
        if (p) {
            memcpy(p, dynamic_buffer_data(buf), blen);
            *out_text = p;
            *out_len = blen;
        }
    }
    dynamic_buffer_destroy(buf);
}

/* Compute the local (per-row) selection range for the visual line at index
 * `v` (in scrollback coords). Sets *out_start = *out_end = 0 if no selection
 * applies to this row. The result is in local display-column space
 * (0-indexed within the row). */
static void compute_row_selection(const TuiViewport *vp, size_t v,
                                  int *out_start, int *out_end)
{
    *out_start = 0;
    *out_end = 0;

    if (!vp->copy_mode || !vp->focused)
        return;

    size_t start_v, end_v;
    int start_col, end_col;

    if (vp->has_mark) {
        /* Order mark and cursor */
        int cursor_first = (vp->cursor_visual_line < vp->mark_visual_line) ||
                           (vp->cursor_visual_line == vp->mark_visual_line &&
                            vp->cursor_col <= vp->mark_col);
        if (cursor_first) {
            start_v = vp->cursor_visual_line;
            start_col = (int)vp->cursor_col;
            end_v = vp->mark_visual_line;
            end_col = (int)vp->mark_col + 1;
        } else {
            start_v = vp->mark_visual_line;
            start_col = (int)vp->mark_col;
            end_v = vp->cursor_visual_line;
            end_col = (int)vp->cursor_col + 1;
        }
    } else {
        /* No mark: highlight just the cursor cell */
        start_v = end_v = vp->cursor_visual_line;
        start_col = (int)vp->cursor_col;
        end_col = start_col + 1;
    }

    if (v < start_v || v > end_v)
        return;

    if (v == start_v && v == end_v) {
        *out_start = start_col;
        *out_end = end_col;
    } else if (v == start_v) {
        *out_start = start_col;
        *out_end = vp->width;
    } else if (v == end_v) {
        *out_start = 0;
        *out_end = end_col;
    } else {
        *out_start = 0;
        *out_end = vp->width;
    }

    if (*out_start < 0)
        *out_start = 0;
    if (*out_end > vp->width)
        *out_end = vp->width;
}

/* Report cursor position for the runtime. Visible only when focused AND
 * in copy-mode (the only mode where the viewport tracks a cursor). */
TuiCursor tui_viewport_cursor_pos(const TuiViewport *vp)
{
    if (!vp || !vp->focused || !vp->copy_mode)
        return tui_cursor_hidden();

    /* scroll_to_cursor() keeps cursor_visual_line within the visible window
     * after every motion, so the subtraction is non-negative. */
    int row = vp->render_row + (int)(vp->cursor_visual_line - vp->y_offset);
    int col = vp->render_col + (int)vp->cursor_col;
    return tui_cursor_at(row, col);
}

/* Render viewport to output buffer */
void tui_viewport_view(const TuiViewport *vp, DynamicBuffer *out)
{
    if (!vp || !out)
        return;

    char buf[64];

    /* Find the content line corresponding to y_offset */
    int sub_line = 0;
    size_t content_idx =
        find_content_line_for_visual(vp, vp->y_offset, &sub_line);

    for (int row = 0; row < vp->height; row++) {
        int screen_row = vp->render_row + row;
        snprintf(buf, sizeof(buf), CSI "%d;%dH", screen_row, vp->render_col);
        dynamic_buffer_append_str(out, buf);
        dynamic_buffer_append_str(out, CSI "K");

        int sel_start = 0, sel_end = 0;
        compute_row_selection(vp, vp->y_offset + (size_t)row, &sel_start,
                              &sel_end);

        if (content_idx < vp->line_count) {
            render_line_segment(&vp->lines[content_idx], vp->width, sub_line,
                                vp->wrap_mode, out, sel_start, sel_end);
            sub_line++;
            if (sub_line >= vp->lines[content_idx].visual_lines) {
                content_idx++;
                sub_line = 0;
            }
        }
    }
}

/* Component interface wrappers */
static TuiInitResult viewport_init(void *config)
{
    (void)config;
    TuiModel *model = (TuiModel *)tui_viewport_create();
    return tui_init_result_none(model);
}

/* Test if a key message matches a Ctrl+<lower-letter> chord (e.g., C-n). */
static int is_ctrl_letter(const TuiKeyMsg *k, char letter)
{
    return k->key == TUI_KEY_NONE && (k->mods & TUI_MOD_CTRL) &&
           !(k->mods & TUI_MOD_ALT) && k->rune == (uint32_t)letter;
}

/* Test if a key message matches an Alt+<char> chord (e.g., M-w, M-<). */
static int is_alt_char(const TuiKeyMsg *k, char letter)
{
    return k->key == TUI_KEY_NONE && (k->mods & TUI_MOD_ALT) &&
           !(k->mods & TUI_MOD_CTRL) && k->rune == (uint32_t)letter;
}

/* Handle a key message in copy-mode. Returns a command (M-w copies) or NULL. */
static TuiCmd *handle_copy_mode_key(TuiViewport *vp, const TuiKeyMsg *k)
{
    /* Toggle mark with C-SPC */
    if (is_ctrl_letter(k, ' ')) {
        if (vp->has_mark) {
            vp->has_mark = 0;
        } else {
            vp->mark_visual_line = vp->cursor_visual_line;
            vp->mark_col = vp->cursor_col;
            vp->has_mark = 1;
        }
        return NULL;
    }

    /* Exit / cancel: C-g or Escape */
    if (is_ctrl_letter(k, 'g') || k->key == TUI_KEY_ESCAPE) {
        if (vp->has_mark) {
            vp->has_mark = 0;
        } else {
            tui_viewport_exit_copy_mode(vp);
        }
        return NULL;
    }

    /* Motion */
    if (k->key == TUI_KEY_DOWN || is_ctrl_letter(k, 'n')) {
        cursor_move_lines(vp, +1);
        return NULL;
    }
    if (k->key == TUI_KEY_UP || is_ctrl_letter(k, 'p')) {
        cursor_move_lines(vp, -1);
        return NULL;
    }
    if (k->key == TUI_KEY_RIGHT || is_ctrl_letter(k, 'f')) {
        cursor_move_cols(vp, +1);
        return NULL;
    }
    if (k->key == TUI_KEY_LEFT || is_ctrl_letter(k, 'b')) {
        cursor_move_cols(vp, -1);
        return NULL;
    }
    if (k->key == TUI_KEY_HOME || is_ctrl_letter(k, 'a')) {
        vp->cursor_col = 0;
        return NULL;
    }
    if (k->key == TUI_KEY_END || is_ctrl_letter(k, 'e')) {
        vp->cursor_col = (size_t)visual_line_width(vp, vp->cursor_visual_line);
        return NULL;
    }
    if (k->key == TUI_KEY_PAGE_DOWN || is_ctrl_letter(k, 'v')) {
        cursor_move_lines(vp, vp->height > 0 ? vp->height : 1);
        return NULL;
    }
    if (k->key == TUI_KEY_PAGE_UP || is_alt_char(k, 'v')) {
        cursor_move_lines(vp, vp->height > 0 ? -vp->height : -1);
        return NULL;
    }
    if (is_alt_char(k, '<')) {
        vp->cursor_visual_line = 0;
        vp->cursor_col = 0;
        scroll_to_cursor(vp);
        return NULL;
    }
    if (is_alt_char(k, '>')) {
        if (vp->total_visual_lines > 0)
            vp->cursor_visual_line = vp->total_visual_lines - 1;
        vp->cursor_col = 0;
        scroll_to_cursor(vp);
        return NULL;
    }

    /* Copy: M-w */
    if (is_alt_char(k, 'w')) {
        char *text = NULL;
        size_t len = 0;
        tui_viewport_extract_selection(vp, &text, &len);
        if (text && len > 0) {
            TuiCmd *cmd = tui_cmd_clipboard_copy(text, len);
            free(text);
            /* Clear mark after copy to match emacs behavior. */
            vp->has_mark = 0;
            return cmd;
        }
        free(text);
        return NULL;
    }

    return NULL;
}

/* Handle a mouse message. Updates state in place, returns NULL (no commands
 * are emitted directly from mouse events; M-w is the explicit copy step). */
static void handle_mouse(TuiViewport *vp, const TuiMouseMsg *m)
{
    int local_row = m->row - vp->render_row;
    int local_col = m->col - vp->render_col;
    int inside = local_row >= 0 && local_row < vp->height && local_col >= 0 &&
                 local_col < vp->width;

    if (m->action == TUI_MOUSE_ACTION_PRESS) {
        if (m->button == TUI_MOUSE_LEFT) {
            if (!inside)
                return;
            vp->copy_mode = 1;
            vp->cursor_visual_line = vp->y_offset + (size_t)local_row;
            vp->cursor_col = (size_t)local_col;
            vp->mark_visual_line = vp->cursor_visual_line;
            vp->mark_col = vp->cursor_col;
            vp->has_mark = 1;
            vp->mouse_dragging = 1;
            clamp_cursor(vp);
        } else if (m->button == TUI_MOUSE_WHEEL_UP) {
            if (inside)
                tui_viewport_scroll_up(vp, 3);
        } else if (m->button == TUI_MOUSE_WHEEL_DOWN) {
            if (inside)
                tui_viewport_scroll_down(vp, 3);
        }
        return;
    }

    if (m->action == TUI_MOUSE_ACTION_RELEASE) {
        vp->mouse_dragging = 0;
        return;
    }

    if (m->action == TUI_MOUSE_ACTION_MOTION && vp->mouse_dragging) {
        if (local_row < 0) {
            tui_viewport_scroll_up(vp, 1);
            vp->cursor_visual_line = vp->y_offset;
        } else if (local_row >= vp->height) {
            tui_viewport_scroll_down(vp, 1);
            size_t maxv = vp->total_visual_lines > 0
                              ? vp->total_visual_lines - 1
                              : 0;
            size_t target = vp->y_offset + (size_t)vp->height - 1;
            if (target > maxv)
                target = maxv;
            vp->cursor_visual_line = target;
        } else {
            vp->cursor_visual_line = vp->y_offset + (size_t)local_row;
        }
        if (local_col < 0)
            local_col = 0;
        if (local_col >= vp->width)
            local_col = vp->width - 1;
        vp->cursor_col = (size_t)local_col;
        clamp_cursor(vp);
    }
}

static TuiUpdateResult viewport_update(TuiModel *model, TuiMsg msg)
{
    TuiViewport *vp = (TuiViewport *)model;
    if (!vp)
        return tui_update_result_none();

    if (msg.type == TUI_MSG_FOCUS) {
        vp->focused = 1;
        return tui_update_result_none();
    }
    if (msg.type == TUI_MSG_BLUR) {
        vp->focused = 0;
        vp->mouse_dragging = 0;
        return tui_update_result_none();
    }

    if (!vp->focused)
        return tui_update_result_none();

    if (msg.type == TUI_MSG_KEY_PRESS) {
        const TuiKeyMsg *k = &msg.data.key;

        /* Outside copy-mode: only C-SPC enters it; everything else passes
         * through (parent handles scroll/page keys, focus cycling, etc.). */
        if (!vp->copy_mode) {
            if (is_ctrl_letter(k, ' ')) {
                tui_viewport_enter_copy_mode(vp);
            }
            return tui_update_result_none();
        }

        TuiCmd *cmd = handle_copy_mode_key(vp, k);
        return cmd ? tui_update_result(cmd) : tui_update_result_none();
    }

    if (msg.type == TUI_MSG_MOUSE) {
        handle_mouse(vp, &msg.data.mouse);
        return tui_update_result_none();
    }

    return tui_update_result_none();
}

static TuiView viewport_view_slot(const TuiModel *model, DynamicBuffer *out)
{
    const TuiViewport *vp = (const TuiViewport *)model;
    tui_viewport_view(vp, out);
    TuiView v = tui_view_default(out);
    v.cursor = tui_viewport_cursor_pos(vp);
    return v;
}

static void viewport_free(TuiModel *model)
{
    tui_viewport_free((TuiViewport *)model);
}

static const TuiComponent viewport_component_instance = {
    .init = viewport_init,
    .update = viewport_update,
    .view = viewport_view_slot,
    .free = viewport_free,
};

const TuiComponent *tui_viewport_component(void)
{
    return &viewport_component_instance;
}
