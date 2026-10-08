/* statusline.h - One laid-out status row (app chrome), a component
 *
 * A full-width row of app chrome ABOVE (or below) a frame's content: a
 * spinner + a context gauge + a separator rule + a right-aligned identity
 * block, say. The row is DECLARED, not drawn: the caller hands over an
 * ordered list of segments and the component does the column arithmetic
 * against the terminal width.
 *
 * Why a component of its own (it used to be a field of the text input):
 * the row's content is about the app, not about the text being edited, so
 * a text widget can neither give it a width nor lay it out — and the row
 * ended up inside the input's height and cursor arithmetic instead. Here
 * the row owns its layout and the input owns none of it.
 *
 * The component is placement-agnostic: tui_statusline_view() paints its
 * row where the cursor is and never a trailing newline, so a composing
 * app positions it (frame order in inline mode, a CSI position in
 * alt-screen mode) exactly as it positions any other row painter.
 */

#ifndef BOBA_STATUSLINE_H
#define BOBA_STATUSLINE_H

#include "../component.h"
#include "../dynamic_buffer.h"
#include "../msg.h"
#include "../style.h"

#include <stddef.h>

/* The row's column budget. The composed row is emitted as literal bytes
 * (a fill IS its repeated glyphs), so the budget is what bounds the
 * component's reused buffers. 512 columns covers a full-width 4K terminal
 * at a small font (3840 px / ~8 px per cell = 480); a wider terminal gets
 * a row that stops at the budget — cosmetic, and the terminal's EL clears
 * the tail either way. */
#define TUI_STATUSLINE_MAX_COLS 512

/* The width a row lays out against when terminal_width is 0 (unset). */
#define TUI_STATUSLINE_DEFAULT_COLS 80

/* Where a segment sits in the row.
 *
 * LEFT segments pack from the row's left edge in declaration order; the
 * first non-LEFT segment starts the row's right-packed group, whose last
 * segment ends at the row's last column. Declare LEFT segments first. */
typedef enum
{
    TUI_SEGMENT_LEFT = 0,  /* packs from the row's left edge, rightwards */
    TUI_SEGMENT_FILL = 1,  /* repeats its unit to absorb the slack */
    TUI_SEGMENT_RIGHT = 2, /* packs against the row's right edge */
} TuiSegmentAlign;

/* One declared piece of a status row.
 *
 * A segment describes INTENT (order, side, what may give when the row is
 * narrow) and never columns: the component measures, shrinks, elides and
 * places. The setter copies `text`, so the caller's storage may be
 * transient (a composing app's reused buffers are).
 *
 * Shrinking (only when the declared row does not fit): segments are
 * shrunk to their `min_cols` floor in descending `priority` order, then
 * below it in the same order; a segment that reaches 0 columns is dropped
 * whole, `pad_left` included. `priority` 0 means FIXED — never shrunk,
 * which is what protects a glyph or a gauge. Elision keeps the head, cuts
 * on a grapheme-cluster boundary and marks the cut with `…` (U+2026). */
typedef struct TuiSegment
{
    const char *text; /* UTF-8; the setter copies it (FILL: the unit) */
    size_t len;       /* 0 = strlen(text) */
    TuiStyle style;   /* inline styling (box model ignored) */
    TuiSegmentAlign align;
    int priority; /* shrink order: higher gives first; 0 = fixed */
    int min_cols; /* floor for the text's columns (FILL: its reserve) */
    int pad_left; /* blank columns reserved on the segment's left */
} TuiSegment;

/* Status line model.
 *
 * The declarations and the composed row are owned. The row is laid out
 * EAGERLY by the setters (never in view()), so view() is a pure emit of
 * already-laid-out state — no view-time mutation, no const-cast
 * bookkeeping of the kind a lazy cache would need. */
typedef struct TuiStatusLine
{
    TuiModel base;

    int terminal_width; /* Width the row lays out against (0 = 80) */

    /* Declared segments, in declaration order: segs[i]'s text is the
     * owned copy seg_text[i] (the declaration's `text` is borrowed). */
    TuiSegment *segs;
    char **seg_text;
    size_t n_segs;
    size_t cap_segs;

    /* The composed row: the laid-out bytes plus one piece per emitted
     * segment (off/len index into `row`; a piece covers its pad_left
     * blanks and its text). A segment laid out to 0 columns emits no
     * piece. */
    char *row;
    size_t row_len;
    size_t row_cap;
    struct
    {
        size_t off;
        size_t len;
        TuiStyle style;
    } *pieces;
    size_t n_pieces;
    size_t cap_pieces;
} TuiStatusLine;

/* Create a status line with no segments (height 0: no row). */
TuiStatusLine *tui_statusline_create(void);

/* Free the model and everything it owns. */
void tui_statusline_free(TuiStatusLine *sl);

/* The component vtable (Elm init/update/view/free). update() handles
 * TUI_MSG_WINDOW_SIZE and ignores everything else, so the component works
 * both composed by an app and registered with a runtime directly. */
const TuiComponent *tui_statusline_component(void);

/* Set the width the row lays out against (0 = 80). Relayouts now. */
void tui_statusline_set_terminal_width(TuiStatusLine *sl, int width);

/* Declare the row. Copies the segment texts and styles, then lays the row
 * out — unless the declarations are unchanged, in which case this is a
 * no-op (no copy, no alloc, no relayout). NULL / n == 0 clears the row. */
void tui_statusline_set_segments(TuiStatusLine *sl, const TuiSegment *segs,
                                 size_t n);

/* Rows the row occupies: 0 (no segments) or 1. */
int tui_statusline_get_height(const TuiStatusLine *sl);

/* Paint the row where the cursor is: "\r" + EL + the laid-out row, no
 * trailing newline. The caller owns the position (and the separator after
 * it); `sl` is not modified. */
void tui_statusline_view(const TuiStatusLine *sl, DynamicBuffer *out);

/* Update slot. */
TuiUpdateResult tui_statusline_update(TuiStatusLine *sl, TuiMsg msg);

#endif /* BOBA_STATUSLINE_H */
