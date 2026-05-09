/* textview.h - Scrolling text view component for bloom-boba
 *
 * A text display widget supporting:
 * - Append-only text content (like a terminal output)
 * - Automatic scrolling to bottom on new content
 * - Fixed height display area
 * - ANSI escape sequence passthrough
 */

#ifndef BLOOM_BOBA_TEXTVIEW_H
#define BLOOM_BOBA_TEXTVIEW_H

#include "../component.h"
#include "../dynamic_buffer.h"
#include <stddef.h>

/* Text view configuration */
typedef struct
{
    int height; /* Display height in lines (0 = auto-size to terminal) */
} TuiTextViewConfig;

/* Text view model */
typedef struct TuiTextView
{
    TuiModel base;      /* Component base type */
    char *content;      /* Text content buffer */
    size_t content_len; /* Current content length */
    size_t content_cap; /* Allocated capacity */

    int height;          /* Display height in lines (0 = use terminal height) */
    int terminal_height; /* Terminal height for calculating display area */
    int terminal_width;  /* Terminal width */

    int scroll_offset; /* Lines scrolled from bottom (0 = at bottom) */
    int auto_scroll;   /* Auto-scroll to bottom on new content (default: 1) */
} TuiTextView;

/* Create a new text view component
 *
 * Parameters:
 *   height: Display height in lines (0 = auto-size to terminal)
 *
 * Returns: New text view, or NULL on failure
 */
TuiTextView *tui_textview_create(int height);

/* Free text view component */
void tui_textview_free(TuiTextView *view);

/* Append text to the view
 *
 * Parameters:
 *   view: Text view
 *   text: Text to append
 *   len: Length of text
 */
void tui_textview_append(TuiTextView *view, const char *text, size_t len);

/* Append a null-terminated string */
void tui_textview_append_str(TuiTextView *view, const char *text);

/* Clear all content */
void tui_textview_clear(TuiTextView *view);

/* Set terminal dimensions */
void tui_textview_set_terminal_size(TuiTextView *view, int width, int height);

/* Render the text view to output buffer
 *
 * This outputs the visible portion of the text content.
 * Does NOT include any framing or positioning - caller handles that.
 */
void tui_textview_view(const TuiTextView *view, DynamicBuffer *out);

/* Get content length */
size_t tui_textview_len(const TuiTextView *view);

/* Get content */
const char *tui_textview_content(const TuiTextView *view);

/* Update the text view with a message
 * Currently handles window size messages to update terminal dimensions
 */
TuiUpdateResult tui_textview_update(TuiTextView *view, TuiMsg msg);

/* Get component interface for text view */
const TuiComponent *tui_textview_component(void);

#endif /* BLOOM_BOBA_TEXTVIEW_H */
