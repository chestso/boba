/* statusline.c - One laid-out status row (app chrome)
 *
 * The layout, in one place:
 *
 *   w      = the width the row lays out against (0 -> 80, clamped to
 *            TUI_STATUSLINE_MAX_COLS)
 *   cols_i = each segment's current column count (its text's display
 *            width; a FILL starts at its min_cols reserve)
 *   want   = the sum of cols_i plus every declared pad_left
 *
 *   over = want - w. When over > 0 the row does not fit, and the
 *   declarations say what gives: shrink to `min_cols` in descending
 *   priority (0 = FIXED, never shrunk), then below the floor in the same
 *   order, down to 0 — a segment at 0 columns is dropped whole, pad
 *   included. Anything still over overflows into the terminal's EL (the
 *   fixed chrome is the row; it is never cut).
 *
 *   The slack (w - placed) then goes to the FILL segments. Columns:
 *   LEFT segments pack from column 0 in declaration order; the first
 *   non-LEFT segment starts the right-packed group, whose last segment
 *   ends at w-1. Emission pads each piece up to its column, so a
 *   right-packed group is really right-packed even when the row carries
 *   no fill.
 *
 * Widths are DISPLAY COLUMNS, measured with boba's own grapheme walk
 * (tui_next_cluster / tui_utf8_display_width_n) — the same measurement the
 * renderer paints with, so the row's right edge is exactly w.
 *
 * The layout runs in the setters (eager), so view() is a pure emit.
 */

#include <boba/ansi_sequences.h>
#include <boba/components/statusline.h>
#include <boba/unicode.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STATUSLINE_TYPE_ID (TUI_COMPONENT_TYPE_BASE + 21)

/* The ellipsis an elided segment ends with (U+2026, one column). */
#define SL_ELLIPSIS "\xe2\x80\xa6"

/* ---------------------------------------------------------------- */
/* Small helpers                                                    */
/* ---------------------------------------------------------------- */

/* Whether a style asks for anything at all (the textinput's rule: an
 * unstyled piece is emitted as bare bytes rather than through
 * tui_style_render, which would wrap it in SGR it does not need). */
static int style_has_styling(const TuiStyle *s)
{
    if (!s)
        return 0;
    return s->fg.type != TUI_COLOR_NONE || s->bg.type != TUI_COLOR_NONE ||
           s->bold || s->italic || s->underline || s->strikethrough ||
           s->reverse || s->blink || s->faint;
}

/* Grow the composed-row buffer to at least `need` bytes. 0 on success. */
static int row_reserve(TuiStatusLine *sl, size_t need)
{
    if (need <= sl->row_cap)
        return 0;
    size_t cap = sl->row_cap ? sl->row_cap : 128;
    while (cap < need)
        cap *= 2;
    char *n = (char *)realloc(sl->row, cap);
    if (!n)
        return -1;
    sl->row = n;
    sl->row_cap = cap;
    return 0;
}

static int piece_reserve(TuiStatusLine *sl, size_t need)
{
    if (need <= sl->cap_pieces)
        return 0;
    size_t cap = sl->cap_pieces ? sl->cap_pieces : 8;
    while (cap < need)
        cap *= 2;
    void *n = realloc(sl->pieces, cap * sizeof(*sl->pieces));
    if (!n)
        return -1;
    sl->pieces = n;
    sl->cap_pieces = cap;
    return 0;
}

/* Append `n` blanks to the composed row. */
static void row_blanks(TuiStatusLine *sl, int n)
{
    if (n <= 0 || row_reserve(sl, sl->row_len + (size_t)n) != 0)
        return;
    memset(sl->row + sl->row_len, ' ', (size_t)n);
    sl->row_len += (size_t)n;
}

/* Append `n` copies of a fill's unit, then blanks for the remainder. */
static void row_fill(TuiStatusLine *sl, const char *unit, size_t unit_len,
                     int unit_cols, int cols)
{
    if (cols <= 0)
        return;
    if (unit_cols <= 0 || unit_len == 0) { /* no measurable unit: blanks */
        row_blanks(sl, cols);
        return;
    }
    int reps = cols / unit_cols;
    int rest = cols - reps * unit_cols;
    size_t need = sl->row_len + (size_t)reps * unit_len + (size_t)rest + 1;
    if (row_reserve(sl, need) != 0)
        return;
    for (int i = 0; i < reps; i++) {
        memcpy(sl->row + sl->row_len, unit, unit_len);
        sl->row_len += unit_len;
    }
    row_blanks(sl, rest);
}

/* How much of a segment's text survives a fit into `cols` columns: the
 * byte count of the longest grapheme-cluster prefix that fits in `cols - 1`
 * columns (one column is reserved for the `…`), and that prefix's width.
 * `cols` must be >= 1. */
static size_t cluster_prefix(const char *text, size_t tlen, int cols,
                             int *out_cols)
{
    int limit = cols - 1; /* one column for the ellipsis */
    size_t i = 0;
    int have = 0;
    while (i < tlen) {
        size_t bytes = 0;
        int cw = tui_next_cluster(text + i, tlen - i, &bytes);
        if (bytes == 0 || have + cw > limit)
            break;
        have += cw;
        i += bytes;
    }
    *out_cols = have;
    return i;
}

/* The width a segment's text occupies when fitted into `cols` display
 * columns: `cols` when the text fits, otherwise the head plus the `…` —
 * which is `cols` itself except where a wide cluster could not be split,
 * and is then ONE column short. The layout needs this up front: a segment
 * that ends up narrower than its columns hands the difference back to the
 * row (the fills), so the composed row is still exactly the width. */
static int fitted_cols(const char *text, size_t tlen, int cols)
{
    int w = tui_utf8_display_width_n(text, tlen);
    if (w <= cols)
        return w;
    if (cols <= 0)
        return 0;
    int have = 0;
    (void)cluster_prefix(text, tlen, cols, &have);
    return have + 1;
}

/* Fit the NUL-terminated string at `text` into `cols` display columns, in
 * place: whole grapheme clusters only, the cut marked with `…`. Returns the
 * resulting width — what fitted_cols predicted for the same text. */
static int fit_cols(char *text, int cols)
{
    int w = tui_utf8_display_width(text);
    if (w <= cols)
        return w;
    if (cols <= 0) {
        text[0] = '\0';
        return 0;
    }
    int have = 0;
    size_t i = cluster_prefix(text, strlen(text), cols, &have);
    memcpy(text + i, SL_ELLIPSIS, sizeof(SL_ELLIPSIS)); /* + NUL */
    return have + 1;
}

/* ---------------------------------------------------------------- */
/* Lifecycle                                                        */
/* ---------------------------------------------------------------- */

TuiStatusLine *tui_statusline_create(void)
{
    TuiStatusLine *sl = (TuiStatusLine *)calloc(1, sizeof(*sl));
    if (!sl)
        return NULL;
    sl->base.type = STATUSLINE_TYPE_ID;
    return sl;
}

static void segments_clear(TuiStatusLine *sl)
{
    for (size_t i = 0; i < sl->n_segs; i++)
        free(sl->seg_text[i]);
    free(sl->segs);
    free(sl->seg_text);
    sl->segs = NULL;
    sl->seg_text = NULL;
    sl->n_segs = 0;
    sl->cap_segs = 0;
}

void tui_statusline_free(TuiStatusLine *sl)
{
    if (!sl)
        return;
    segments_clear(sl);
    free(sl->row);
    free(sl->pieces);
    free(sl->slots);
    free(sl);
}

/* ---------------------------------------------------------------- */
/* Layout                                                           */
/* ---------------------------------------------------------------- */

/* The width segments lay out against. */
static int layout_width(const TuiStatusLine *sl)
{
    int w = sl->terminal_width > 0 ? sl->terminal_width
                                   : TUI_STATUSLINE_DEFAULT_COLS;
    if (w > TUI_STATUSLINE_MAX_COLS)
        w = TUI_STATUSLINE_MAX_COLS;
    return w;
}

/* Grow the layout scratch to at least `n` slots. 0 on success.
 *
 * The slots are the model's, not the layout's: they are grown once and
 * then reused, so a relayout costs no allocation at all (the declaration
 * count is the same size every time, which is what made the old
 * per-layout calloc/free a pure allocator round-trip). */
static int slots_reserve(TuiStatusLine *sl, size_t n)
{
    if (n <= sl->cap_slots)
        return 0;
    if (n > SIZE_MAX / sizeof(*sl->slots))
        return -1;
    size_t cap = sl->cap_slots ? sl->cap_slots : 8;
    while (cap < n)
        cap *= 2;
    void *p = realloc(sl->slots, cap * sizeof(*sl->slots));
    if (!p)
        return -1;
    sl->slots = p;
    sl->cap_slots = cap;
    return 0;
}

/* The pad a segment still reserves: a dropped (0-column) segment takes its
 * pad with it. */
static int slot_pad(const struct TuiStatusLineSlot *slot)
{
    return slot->cols > 0 ? slot->pad : 0;
}

/* Rebuild the composed row from the current declarations. */
static void statusline_layout(TuiStatusLine *sl)
{
    sl->row_len = 0;
    sl->n_pieces = 0;

    size_t n = sl->n_segs;
    if (n == 0)
        return;

    if (slots_reserve(sl, n) != 0)
        return; /* no memory: no row (rather than a wrong one) */
    struct TuiStatusLineSlot *S = sl->slots;

    const int w = layout_width(sl);

    int want = 0;
    for (size_t i = 0; i < n; i++) {
        const TuiSegment *s = &sl->segs[i];
        size_t tlen = strlen(sl->seg_text[i]);
        S[i].pad = s->pad_left > 0 ? s->pad_left : 0;
        if (s->align == TUI_SEGMENT_FILL) {
            int floor = s->min_cols > 0 ? s->min_cols : 0;
            if (floor > w)
                floor = w;
            S[i].cols = floor;
            S[i].floor = floor;
        } else {
            int text_cols = tui_utf8_display_width_n(sl->seg_text[i], tlen);
            S[i].cols = text_cols;
            S[i].floor = s->min_cols > 0 ? s->min_cols : 0;
            if (S[i].floor > S[i].cols)
                S[i].floor = S[i].cols; /* a floor above the text is the text */
        }
        want += S[i].pad;
        want += S[i].cols;
    }

    /* What gives when the row does not fit: (a) every shrinkable segment
     * down to its floor, then (b) below the floor, to 0 — both in
     * descending priority, declaration order breaking ties. */
    int over = want - w;
    for (int pass = 0; pass < 2 && over > 0; pass++) {
        for (;;) {
            int best = -1;
            for (size_t i = 0; i < n; i++) {
                const TuiSegment *s = &sl->segs[i];
                if (s->priority <= 0)
                    continue;
                int room = pass == 0 ? S[i].cols - S[i].floor : S[i].cols;
                if (room <= 0)
                    continue;
                if (best < 0 || s->priority > sl->segs[best].priority)
                    best = (int)i;
            }
            if (best < 0)
                break;
            int room =
                pass == 0 ? S[best].cols - S[best].floor : S[best].cols;
            int give = room < over ? room : over;
            S[best].cols -= give;
            over -= give;
            if (over <= 0)
                break;
        }
    }

    /* An elided segment can end one column short of the columns it was
     * given (a wide cluster is never split), so the columns are the FITTED
     * width before the row is composed: the difference goes back to the
     * fills below, and the row stays exactly as wide as it was declared. */
    if (over <= 0) {
        for (size_t i = 0; i < n; i++) {
            if (sl->segs[i].align == TUI_SEGMENT_FILL || S[i].cols <= 0)
                continue;
            size_t tlen = strlen(sl->seg_text[i]);
            S[i].cols = fitted_cols(sl->seg_text[i], tlen, S[i].cols);
        }
    }

    /* The slack (a row that fits) goes to the FILL segments: evenly, the
     * remainder to the last declared one. */
    if (over <= 0) {
        int placed = 0, n_fills = 0;
        for (size_t i = 0; i < n; i++) {
            placed += slot_pad(&S[i]);
            placed += S[i].cols;
            if (sl->segs[i].align == TUI_SEGMENT_FILL)
                n_fills++;
        }
        int slack = w - placed;
        if (slack > 0 && n_fills > 0) {
            int each = slack / n_fills;
            int rest = slack - each * n_fills;
            int seen = 0;
            for (size_t i = 0; i < n; i++) {
                if (sl->segs[i].align != TUI_SEGMENT_FILL)
                    continue;
                seen++;
                S[i].cols += each + (seen == n_fills ? rest : 0);
            }
        }
    }

    /* Columns. The LEFT prefix packs from column 0; the first non-LEFT
     * segment starts the right-packed group, which ends at w-1. */
    size_t first_suffix = n;
    for (size_t i = 0; i < n; i++) {
        if (sl->segs[i].align != TUI_SEGMENT_LEFT) {
            first_suffix = i;
            break;
        }
    }
    int cursor = 0;
    for (size_t i = 0; i < first_suffix; i++) {
        S[i].start = cursor;
        cursor += slot_pad(&S[i]) + S[i].cols;
    }
    int suffix_w = 0;
    for (size_t i = first_suffix; i < n; i++)
        suffix_w += slot_pad(&S[i]) + S[i].cols;
    int suffix_start = w - suffix_w;
    if (suffix_start < cursor)
        suffix_start = cursor; /* overlapping declarations: overflow */
    cursor = suffix_start;
    for (size_t i = first_suffix; i < n; i++) {
        S[i].start = cursor;
        cursor += slot_pad(&S[i]) + S[i].cols;
    }

    /* Emit: each segment's gap blanks, its pad blanks and its (elided)
     * text, in declaration order — the same order the columns above assume.
     * A segment laid out to 0 columns is dropped whole, pad included.
     * Placement is tracked in COLUMNS, never in bytes: the row's bytes and
     * its columns only coincide while the text is ASCII, and a wide glyph
     * is exactly the case this component exists to measure. */
    int emitted = 0;
    for (size_t i = 0; i < n; i++) {
        const TuiSegment *s = &sl->segs[i];
        if (S[i].cols <= 0)
            continue;
        size_t tlen = strlen(sl->seg_text[i]);
        if (row_reserve(sl, sl->row_len + tlen + 4) != 0)
            break;
        size_t off = sl->row_len;
        if (S[i].start > emitted) /* the right-packed group's gap */
            row_blanks(sl, S[i].start - emitted);
        row_blanks(sl, slot_pad(&S[i]));
        if (s->align == TUI_SEGMENT_FILL) {
            row_fill(sl, sl->seg_text[i], tlen,
                     tui_utf8_display_width_n(sl->seg_text[i], tlen),
                     S[i].cols);
        } else {
            memcpy(sl->row + sl->row_len, sl->seg_text[i], tlen + 1);
            if (tui_utf8_display_width_n(sl->seg_text[i], tlen) > S[i].cols)
                fit_cols(sl->row + sl->row_len, S[i].cols);
            sl->row_len += strlen(sl->row + sl->row_len);
        }
        emitted = S[i].start + slot_pad(&S[i]) + S[i].cols;
        if (piece_reserve(sl, sl->n_pieces + 1) != 0)
            break;
        sl->pieces[sl->n_pieces].off = off;
        sl->pieces[sl->n_pieces].len = sl->row_len - off;
        sl->pieces[sl->n_pieces].style = s->style;
        sl->n_pieces++;
    }
}

/* ---------------------------------------------------------------- */
/* Setters                                                          */
/* ---------------------------------------------------------------- */

void tui_statusline_set_terminal_width(TuiStatusLine *sl, int width)
{
    if (!sl)
        return;
    int w = width > 0 ? width : 0;
    if (w == sl->terminal_width)
        return;
    sl->terminal_width = w;
    if (sl->n_segs > 0)
        statusline_layout(sl);
}

/* Whether a declaration set equals what the model already holds. */
static int segments_equal(const TuiStatusLine *sl, const TuiSegment *segs,
                          size_t n)
{
    if (n != sl->n_segs)
        return 0;
    for (size_t i = 0; i < n; i++) {
        const TuiSegment *a = &sl->segs[i];
        const TuiSegment *b = &segs[i];
        const char *bt = b->text ? b->text : "";
        size_t la = strlen(sl->seg_text[i]);
        if (la != strlen(bt) || memcmp(sl->seg_text[i], bt, la) != 0)
            return 0;
        if (a->align != b->align || a->priority != b->priority ||
            a->min_cols != b->min_cols || a->pad_left != b->pad_left)
            return 0;
        if (memcmp(&a->style, &b->style, sizeof(a->style)) != 0)
            return 0; /* worst case a redundant re-copy, never a stale row */
    }
    return 1;
}

void tui_statusline_set_segments(TuiStatusLine *sl, const TuiSegment *segs,
                                 size_t n)
{
    if (!sl)
        return;
    if (!segs || n == 0) {
        if (sl->n_segs == 0)
            return;
        segments_clear(sl);
        sl->row_len = 0;
        sl->n_pieces = 0;
        return;
    }
    if (segments_equal(sl, segs, n))
        return; /* unchanged: no copy, no alloc, no relayout */

    segments_clear(sl);
    sl->segs = (TuiSegment *)calloc(n, sizeof(*sl->segs));
    sl->seg_text = (char **)calloc(n, sizeof(char *));
    if (!sl->segs || !sl->seg_text) {
        segments_clear(sl);
        sl->row_len = 0;
        sl->n_pieces = 0;
        return;
    }
    sl->cap_segs = n;
    for (size_t i = 0; i < n; i++) {
        const char *t = segs[i].text ? segs[i].text : "";
        size_t len = segs[i].len ? segs[i].len : strlen(t);
        char *copy = (char *)malloc(len + 1);
        if (!copy)
            break;
        memcpy(copy, t, len);
        copy[len] = '\0';
        sl->segs[i] = segs[i];
        sl->segs[i].text = copy;
        sl->segs[i].len = strlen(copy);
        sl->seg_text[i] = copy;
        sl->n_segs = i + 1;
    }
    statusline_layout(sl);
}

/* ---------------------------------------------------------------- */
/* View                                                             */
/* ---------------------------------------------------------------- */

int tui_statusline_get_height(const TuiStatusLine *sl)
{
    return (!sl || sl->n_segs == 0) ? 0 : 1;
}

/* Emit one piece: styled through boba's inline style path, or as bare
 * bytes when the style asks for nothing. */
static void emit_piece(const TuiStatusLine *sl, size_t i, DynamicBuffer *out)
{
    const char *text = sl->row + sl->pieces[i].off;
    size_t len = sl->pieces[i].len;
    if (len == 0)
        return;
    if (style_has_styling(&sl->pieces[i].style)) {
        TuiStyle s = sl->pieces[i].style;
        s.inline_ = 1; /* pieces are inline: box model ignored */
        char *content = (char *)malloc(len + 1);
        if (!content)
            return;
        memcpy(content, text, len);
        content[len] = '\0';
        char *rendered = tui_style_render(&s, content);
        free(content);
        if (rendered) {
            dynamic_buffer_append_str(out, rendered);
            free(rendered);
        }
    } else {
        dynamic_buffer_append(out, text, len);
    }
}

void tui_statusline_view(const TuiStatusLine *sl, DynamicBuffer *out)
{
    if (!sl || !out)
        return;
    /* The row a frame paints where the cursor is: clear it, then the
     * laid-out bytes. No trailing newline — the composer owns the frame's
     * separators (see the header). */
    dynamic_buffer_append_str(out, "\r");
    dynamic_buffer_append_str(out, EL_TO_END);
    for (size_t i = 0; i < sl->n_pieces; i++)
        emit_piece(sl, i, out);
}

/* ---------------------------------------------------------------- */
/* Component                                                        */
/* ---------------------------------------------------------------- */

static TuiInitResult statusline_init(void *config)
{
    (void)config;
    return tui_init_result_none((TuiModel *)tui_statusline_create());
}

TuiUpdateResult tui_statusline_update(TuiStatusLine *sl, TuiMsg msg)
{
    if (sl && msg.type == TUI_MSG_WINDOW_SIZE)
        tui_statusline_set_terminal_width(sl, msg.data.size.width);
    return tui_update_result_none();
}

static TuiUpdateResult statusline_update_slot(TuiModel *model, TuiMsg msg)
{
    return tui_statusline_update((TuiStatusLine *)model, msg);
}

static TuiView statusline_view_slot(const TuiModel *model, DynamicBuffer *out)
{
    tui_statusline_view((const TuiStatusLine *)model, out);
    /* No cursor of our own (hidden by default) and no terminal modes: the
     * row is chrome, and the app that composes it owns the cursor. */
    return tui_view_default(out);
}

static void statusline_free_slot(TuiModel *model)
{
    tui_statusline_free((TuiStatusLine *)model);
}

static const TuiComponent statusline_component = {
    .init = statusline_init,
    .update = statusline_update_slot,
    .view = statusline_view_slot,
    .free = statusline_free_slot,
};

const TuiComponent *tui_statusline_component(void)
{
    return &statusline_component;
}
