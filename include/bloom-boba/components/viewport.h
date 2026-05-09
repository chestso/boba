/* viewport.h - Software-scrolling viewport component
 *
 * A Bubbletea-style viewport that manages scrollback without ANSI scroll
 * regions. Content is stored as lines in memory, and rendering uses absolute
 * cursor positioning.
 *
 * This approach gives us full control over rendering and avoids the quirks
 * of terminal scroll region handling.
 */

#ifndef TUI_VIEWPORT_H
#define TUI_VIEWPORT_H

#include <bloom-boba/component.h>
#include <bloom-boba/dynamic_buffer.h>
#include <stddef.h>

/* A single line in the viewport */
typedef struct TuiViewportLine
{
    char *text;           /* Line content (owned, null-terminated) */
    size_t len;           /* Byte length */
    size_t display_width; /* Visual width (excludes ANSI sequences) */
    int visual_lines;     /* Number of screen rows this line occupies (>= 1) */
} TuiViewportLine;

/* Viewport model */
typedef struct TuiViewport
{
    TuiModel base;

    /* Line storage */
    TuiViewportLine *lines;
    size_t line_count;
    size_t line_capacity;

    /* Viewport state */
    int width;
    int height;
    size_t y_offset; /* First visible visual line index */
    int auto_scroll; /* Scroll to bottom on new content */
    int max_lines;   /* Max lines to keep (memory limit, 0 = unlimited) */

    /* Wrapping */
    size_t total_visual_lines; /* Sum of all lines' visual_lines */
    int wrap_mode;             /* 0 = clip (truncate at width), 1 = wrap (default) */
    int word_wrap;             /* 0 = column-boundary wrap, 1 = word-boundary wrap */

    /* Render position (absolute, 1-indexed) */
    int render_row; /* Starting row */
    int render_col; /* Starting column */

    /* Focus state */
    int focused; /* 1 if component currently has focus */

    /* Copy-mode (selection + clipboard) state. Active only when both focused
     * and copy_mode are set. Coordinates are in scrollback visual-line space
     * (the same space as y_offset). */
    int copy_mode;             /* 1 = navigating with cursor; 0 = passive */
    int has_mark;              /* 1 = selection active (mark + cursor) */
    size_t cursor_visual_line; /* Cursor's visual-line index */
    size_t cursor_col;         /* Cursor's display column on its visual line */
    size_t mark_visual_line;
    size_t mark_col;
    int mouse_dragging; /* Left button held since last press */
} TuiViewport;

/* Create a new viewport */
TuiViewport *tui_viewport_create(void);

/* Free viewport and all owned memory */
void tui_viewport_free(TuiViewport *vp);

/* Append text to the viewport (handles newlines, partial lines) */
void tui_viewport_append(TuiViewport *vp, const char *text, size_t len);

/* Clear all content */
void tui_viewport_clear(TuiViewport *vp);

/* Scrolling */
void tui_viewport_scroll_up(TuiViewport *vp, int lines);
void tui_viewport_scroll_down(TuiViewport *vp, int lines);
void tui_viewport_page_up(TuiViewport *vp);
void tui_viewport_page_down(TuiViewport *vp);
void tui_viewport_scroll_to_bottom(TuiViewport *vp);
int tui_viewport_at_bottom(const TuiViewport *vp);

/* Configuration */
void tui_viewport_set_size(TuiViewport *vp, int width, int height);
void tui_viewport_set_render_position(TuiViewport *vp, int row, int col);
void tui_viewport_set_max_lines(TuiViewport *vp, int max);
void tui_viewport_set_auto_scroll(TuiViewport *vp, int enabled);
void tui_viewport_set_wrap_mode(TuiViewport *vp, int wrap);

/* Toggle word-boundary wrapping. When on (and wrap_mode is 1), long lines
 * break at the last whitespace before viewport_width; falls back to a
 * column-boundary break if no whitespace is reachable within ~25% of the
 * width. Default is 0 (column-boundary wrap, current behavior). */
void tui_viewport_set_word_wrap(TuiViewport *vp, int word_wrap);

/* Get line count (for testing/debugging) */
size_t tui_viewport_line_count(const TuiViewport *vp);

/* Set focus state directly. Apps usually drive this via TUI_MSG_FOCUS /
 * TUI_MSG_BLUR through the component update path; this is a convenience for
 * apps that manage focus without going through messages. */
BLOOM_BOBA_DEPRECATED(
    "send tui_msg_focus() / tui_msg_blur() through the viewport's update path")
void tui_viewport_set_focused(TuiViewport *vp, int focused);

/* Hit-test: return 1 if the given absolute terminal cell (row, col, both
 * 1-indexed) falls within the viewport's render area. Useful for parents
 * that route mouse events to the right child component. */
int tui_viewport_contains(const TuiViewport *vp, int row, int col);

/* Enter / leave copy-mode programmatically. Entering positions the cursor
 * at the top-left of the visible area and clears any mark. Leaving clears
 * the mark. */
void tui_viewport_enter_copy_mode(TuiViewport *vp);
void tui_viewport_exit_copy_mode(TuiViewport *vp);

/* Extract the current selection as plain text (ANSI sequences stripped).
 *
 * If a mark is set, the selection runs from the mark to the cursor
 * (inclusive at both ends, ordered correctly regardless of which came
 * first). If no mark is set, the entire content line containing the cursor
 * is extracted.
 *
 * On return:
 *   *out_text is a malloc'd buffer (caller frees), or NULL if no content
 *   *out_len is the byte length (no trailing null is appended). */
void tui_viewport_extract_selection(const TuiViewport *vp, char **out_text,
                                    size_t *out_len);

/* Render viewport to output buffer */
void tui_viewport_view(const TuiViewport *vp, DynamicBuffer *out);

/* Report cursor position for the runtime. Visible only when the viewport
 * is focused AND in copy-mode; otherwise abstains. */
TuiCursor tui_viewport_cursor_pos(const TuiViewport *vp);

/* Component interface for generic use */
const TuiComponent *tui_viewport_component(void);

#endif /* TUI_VIEWPORT_H */
