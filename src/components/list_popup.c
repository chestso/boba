/* list_popup.c - Popup list selector implementation
 *
 * Renders a scrollable, navigable popup list with a rounded border.
 * Designed for inline REPL tab completion — renders below the textinput.
 *
 * The filter is a VIEW over the full item list (filter-as-view): the
 * owned items are never copied or reordered on filtering; rendering,
 * selection, and scrolling run over the filtered view, and
 * selected_text() returns the true item. Matching is scan-only —
 * zero allocations per keystroke (the query is copied once per
 * set_filter call).
 */

#include <boba/ansi_sequences.h>
#include <boba/components/list_popup.h>
#include <boba/unicode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_ITEM_CAPACITY 8
#define DEFAULT_MAX_VISIBLE   8
#define MIN_POPUP_WIDTH       20
#define BORDER_PADDING        1 /* space between border and content */
#define BAR_WIDTH             1 /* left accent bar width */

/* ===== item storage ===== */

static void free_items(TuiListPopup *p)
{
    if (!p->items)
        return;
    for (int i = 0; i < p->item_count; i++)
        free(p->items[i]);
    free(p->items);
    p->items = NULL;
    p->item_count = 0;
    p->capacity = 0;
}

static int ensure_capacity(TuiListPopup *p, int needed)
{
    if (p->capacity >= needed)
        return 0;

    int new_cap = p->capacity > 0 ? p->capacity : INITIAL_ITEM_CAPACITY;
    while (new_cap < needed)
        new_cap *= 2;

    char **new_items = realloc(p->items, sizeof(char *) * new_cap);
    if (!new_items)
        return -1;
    p->items = new_items;
    p->capacity = new_cap;
    return 0;
}

static int str_display_width(const char *s)
{
    return (int)tui_utf8_display_width_ansi(s, strlen(s));
}

/* ===== filter-as-view ===== */

/* Grammar: space/tab-separated tokens, AND semantics; per token,
 * case-insensitive substring match. Char-level scans only — no
 * regex, no allocation at interaction time. */

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/* Does `hay` contain `needle` (byte lengths given) case-insensitively? */
static int ci_contains(const char *hay, size_t hay_len,
                       const char *needle, size_t needle_len)
{
    if (needle_len == 0)
        return 1;
    if (needle_len > hay_len)
        return 0;
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        size_t j = 0;
        while (j < needle_len &&
               ascii_lower((unsigned char)hay[i + j]) ==
                   ascii_lower((unsigned char)needle[j]))
            j++;
        if (j == needle_len)
            return 1;
    }
    return 0;
}

/* Does the item match every token of the (active) filter query? */
static int item_passes_filter(const TuiListPopup *p, const char *item)
{
    const char *q = p->filter;
    if (!q)
        return 1;

    size_t item_len = strlen(item);
    const char *tok = q;
    for (;;) {
        while (*tok == ' ' || *tok == '\t')
            tok++;
        if (!*tok)
            break;
        const char *end = tok;
        while (*end && *end != ' ' && *end != '\t')
            end++;
        if (!ci_contains(item, item_len, tok, (size_t)(end - tok)))
            return 0;
        tok = end;
    }
    return 1;
}

static int filter_active(const TuiListPopup *p)
{
    return p->filter && p->filter[0];
}

/* Number of items in the filtered view (all items when no filter). */
static int filtered_count(const TuiListPopup *p)
{
    if (!filter_active(p))
        return p->item_count;
    int n = 0;
    for (int i = 0; i < p->item_count; i++) {
        if (item_passes_filter(p, p->items[i]))
            n++;
    }
    return n;
}

/* Map filtered-view index -> item index; -1 when out of range. */
static int filtered_to_item(const TuiListPopup *p, int findex)
{
    if (findex < 0)
        return -1;
    if (!filter_active(p))
        return findex < p->item_count ? findex : -1;
    for (int i = 0; i < p->item_count; i++) {
        if (!item_passes_filter(p, p->items[i]))
            continue;
        if (findex-- == 0)
            return i;
    }
    return -1;
}

/* ===== layout (filtered view) ===== */

/* Widest item in the filtered view (drives auto width). */
static int max_item_display_width(const TuiListPopup *p)
{
    int max_w = 0;
    for (int i = 0; i < p->item_count; i++) {
        if (!item_passes_filter(p, p->items[i]))
            continue;
        int w = str_display_width(p->items[i]);
        if (w > max_w)
            max_w = w;
    }
    return max_w;
}

/* Number of rows available for items in the filtered view. */
static int compute_visible_rows(const TuiListPopup *p)
{
    int n = filtered_count(p);
    if (n <= 0)
        return 0;

    int max_vis = p->max_visible > 0 ? p->max_visible : DEFAULT_MAX_VISIBLE;
    int term_h = p->terminal_height > 0 ? p->terminal_height : 24;
    /* Leave room for textinput (at least 1 line) + title (1 row) */
    int available = term_h - 2;
    if (available < 1)
        available = 1;

    int vis = max_vis < available ? max_vis : available;
    if (vis > n)
        vis = n;
    return vis;
}

/* Rendered title line. With an active filter that narrows the list:
 * `title: "query" (shown/total)` — or bare `"query" (shown/total)`
 * when no title is set (the query must always be visible; the popup
 * contents silently changing under a filter is a lie). Otherwise
 * `title (count)`, or nothing at all (no title, no filter). */
static void build_title(const TuiListPopup *p, char *buf, size_t buflen)
{
    if (!filter_active(p)) {
        if (!p->title) {
            buf[0] = '\0';
            return;
        }
        snprintf(buf, buflen, "%s (%d)", p->title, p->item_count);
        return;
    }
    int shown = filtered_count(p);
    if (p->title)
        snprintf(buf, buflen, "%s: \"%s\" (%d/%d)", p->title, p->filter,
                 shown, p->item_count);
    else
        snprintf(buf, buflen, "\"%s\" (%d/%d)", p->filter, shown,
                 p->item_count);
}

static int compute_popup_width(const TuiListPopup *p)
{
    if (p->width > 0)
        return p->width;

    int content_w = max_item_display_width(p);
    /* +1 for bar, +1 for indent after bar */
    int w = content_w + BAR_WIDTH + 1;

    /* Title line can make it wider — compute the actual rendered title */
    if (p->title) {
        char title_buf[256];
        build_title(p, title_buf, sizeof(title_buf));
        int title_w = (int)str_display_width(title_buf);
        if (title_w > w)
            w = title_w;
    }

    int term_w = p->terminal_width > 0 ? p->terminal_width : 80;
    if (w > term_w)
        w = term_w;
    if (w < MIN_POPUP_WIDTH)
        w = MIN_POPUP_WIDTH;
    return w;
}

/* Clamp selected/scroll_offset into the filtered view, keeping the
 * selection visible. Called after any change to items, filter, or
 * geometry — the single sync point for filtered-view state.
 * selected/scroll_offset are ALWAYS filtered-view indexes. */
static void ensure_selection_visible(TuiListPopup *p)
{
    int n = filtered_count(p);
    if (n <= 0) {
        p->selected = -1;
        p->scroll_offset = 0;
        return;
    }
    if (p->selected < 0)
        p->selected = 0;
    if (p->selected >= n)
        p->selected = n - 1;

    int vis = compute_visible_rows(p);
    if (p->selected < p->scroll_offset)
        p->scroll_offset = p->selected;
    else if (p->selected >= p->scroll_offset + vis)
        p->scroll_offset = p->selected - vis + 1;
    if (p->scroll_offset < 0)
        p->scroll_offset = 0;
    if (vis > 0 && p->scroll_offset > n - vis)
        p->scroll_offset = n - vis;
}

/* ===== public API ===== */

TuiListPopup *tui_list_popup_create(void)
{
    TuiListPopup *p = calloc(1, sizeof(TuiListPopup));
    if (!p)
        return NULL;
    p->base.type = TUI_LIST_POPUP_TYPE_ID;
    p->selected = -1;
    p->word_start = -1;
    p->terminal_width = 80;
    p->terminal_height = 24;
    return p;
}

void tui_list_popup_free(TuiListPopup *p)
{
    if (!p)
        return;
    free_items(p);
    free(p->title);
    free(p->filter);
    free(p);
}

void tui_list_popup_set_items(TuiListPopup *p, const char *const *texts,
                              int count)
{
    if (!p)
        return;

    free_items(p);
    p->selected = -1;
    p->scroll_offset = 0;

    if (!texts || count <= 0) {
        ensure_selection_visible(p);
        return;
    }

    if (ensure_capacity(p, count) < 0) {
        ensure_selection_visible(p);
        return;
    }

    for (int i = 0; i < count; i++) {
        p->items[i] = strdup(texts[i]);
        if (!p->items[i]) {
            /* Free what we've got so far */
            for (int j = 0; j < i; j++)
                free(p->items[j]);
            p->item_count = 0;
            ensure_selection_visible(p);
            return;
        }
        p->item_count++;
    }

    p->selected = 0;
    p->scroll_offset = 0;
    /* A new list invalidates the old query — never let a stale filter
     * silently hide items the caller just set. */
    free(p->filter);
    p->filter = NULL;
    ensure_selection_visible(p);
}

void tui_list_popup_clear(TuiListPopup *p)
{
    if (!p)
        return;
    free_items(p);
    p->selected = -1;
    p->scroll_offset = 0;
}

void tui_list_popup_show(TuiListPopup *p, int word_start)
{
    if (!p)
        return;
    p->visible = 1;
    p->word_start = word_start;
    p->selected = 0;
    p->scroll_offset = 0;
    ensure_selection_visible(p);
}

void tui_list_popup_hide(TuiListPopup *p)
{
    if (!p)
        return;
    p->visible = 0;
}

int tui_list_popup_is_visible(const TuiListPopup *p)
{
    return p && p->visible;
}

/* Navigation runs over the filtered view (all items when no filter);
 * Down/Up wrap around. Page moves by visible-row count. */

int tui_list_popup_move_down(TuiListPopup *p)
{
    if (!p)
        return 0;
    int n = filtered_count(p);
    if (n <= 0 || n == 1)
        return 0;
    int old = p->selected;
    p->selected = (p->selected + 1) % n;
    ensure_selection_visible(p);
    return p->selected != old;
}

int tui_list_popup_move_up(TuiListPopup *p)
{
    if (!p)
        return 0;
    int n = filtered_count(p);
    if (n <= 0 || n == 1)
        return 0;
    int old = p->selected;
    p->selected = (p->selected - 1 + n) % n;
    ensure_selection_visible(p);
    return p->selected != old;
}

int tui_list_popup_move_page_down(TuiListPopup *p)
{
    if (!p)
        return 0;
    int n = filtered_count(p);
    if (n <= 0)
        return 0;
    int vis = compute_visible_rows(p);
    int old = p->selected;
    p->selected += vis;
    if (p->selected >= n)
        p->selected = n - 1;
    ensure_selection_visible(p);
    return p->selected != old;
}

int tui_list_popup_move_page_up(TuiListPopup *p)
{
    if (!p)
        return 0;
    int n = filtered_count(p);
    if (n <= 0)
        return 0;
    int vis = compute_visible_rows(p);
    int old = p->selected;
    p->selected -= vis;
    if (p->selected < 0)
        p->selected = 0;
    ensure_selection_visible(p);
    return p->selected != old;
}

int tui_list_popup_move_top(TuiListPopup *p)
{
    if (!p)
        return 0;
    if (filtered_count(p) <= 0)
        return 0;
    int old = p->selected;
    p->selected = 0;
    p->scroll_offset = 0;
    return p->selected != old;
}

int tui_list_popup_move_bottom(TuiListPopup *p)
{
    if (!p)
        return 0;
    int n = filtered_count(p);
    if (n <= 0)
        return 0;
    int old = p->selected;
    p->selected = n - 1;
    ensure_selection_visible(p);
    return p->selected != old;
}

const char *tui_list_popup_selected_text(const TuiListPopup *p)
{
    if (!p)
        return NULL;
    int idx = filtered_to_item(p, p->selected);
    if (idx < 0)
        return NULL;
    return p->items[idx];
}

int tui_list_popup_selected_index(const TuiListPopup *p)
{
    return p ? p->selected : -1;
}

int tui_list_popup_word_start(const TuiListPopup *p)
{
    return p ? p->word_start : -1;
}

void tui_list_popup_set_size(TuiListPopup *p, int width, int max_visible)
{
    if (!p)
        return;
    p->width = width;
    p->max_visible = max_visible;
    ensure_selection_visible(p);
}

void tui_list_popup_set_terminal_size(TuiListPopup *p, int w, int h)
{
    if (!p)
        return;
    p->terminal_width = w;
    p->terminal_height = h;
    ensure_selection_visible(p);
}

void tui_list_popup_set_title(TuiListPopup *p, const char *title)
{
    if (!p)
        return;
    free(p->title);
    p->title = title ? strdup(title) : NULL;
}

void tui_list_popup_set_filter(TuiListPopup *p, const char *query)
{
    if (!p)
        return;
    free(p->filter);
    p->filter = query ? strdup(query) : NULL;
    /* A changed view snaps selection to the top (picker convention:
     * the user is narrowing toward a target). */
    p->selected = 0;
    p->scroll_offset = 0;
    ensure_selection_visible(p);
}

int tui_list_popup_filtered_count(const TuiListPopup *p)
{
    return p ? filtered_count(p) : 0;
}

const char *tui_list_popup_filtered_text(const TuiListPopup *p, int index)
{
    if (!p)
        return NULL;
    int idx = filtered_to_item(p, index);
    return idx >= 0 ? p->items[idx] : NULL;
}

void tui_list_popup_set_colors(TuiListPopup *p, TuiColor border_color,
                               TuiColor title_color, TuiColor selected_bg,
                               TuiColor selected_fg,
                               TuiColor selected_marker_color,
                               TuiColor item_color)
{
    if (!p)
        return;
    p->border_color = border_color;
    p->title_color = title_color;
    p->selected_bg = selected_bg;
    p->selected_fg = selected_fg;
    p->selected_marker_color = selected_marker_color;
    p->item_color = item_color;
}

/* Helper: emit fg color SGR if color is not NONE */
static void emit_fg(DynamicBuffer *out, TuiColor c)
{
    if (c.type == TUI_COLOR_NONE)
        return;
    char buf[32];
    if (tui_color_format_fg(c, buf, sizeof(buf)) > 0)
        dynamic_buffer_append_str(out, buf);
}

/* Helper: emit bg color SGR if color is not NONE */
static void emit_bg(DynamicBuffer *out, TuiColor c)
{
    if (c.type == TUI_COLOR_NONE)
        return;
    char buf[32];
    if (tui_color_format_bg(c, buf, sizeof(buf)) > 0)
        dynamic_buffer_append_str(out, buf);
}

void tui_list_popup_view(const TuiListPopup *p, DynamicBuffer *out)
{
    if (!p || !out || !p->visible)
        return;
    int vis_rows = compute_visible_rows(p);
    if (vis_rows <= 0)
        return; /* no items, or nothing passes the filter */

    int width = compute_popup_width(p);
    int content_width = width - BAR_WIDTH; /* space for text after bar */

    /* Title line (or, when filtering without a title, the query line) */
    char title_buf[256];
    build_title(p, title_buf, sizeof(title_buf));
    if (title_buf[0]) {
        /* Title bg spans full width */
        emit_bg(out, p->selected_bg);
        emit_fg(out, p->title_color);
        dynamic_buffer_append_str(out, title_buf);

        /* Pad to full width */
        int title_w = (int)str_display_width(title_buf);
        for (int i = title_w; i < width; i++)
            dynamic_buffer_append_str(out, " ");

        dynamic_buffer_append_str(out, SGR_RESET);
        dynamic_buffer_append_str(out, EL_TO_END);
        dynamic_buffer_append_str(out, "\r\n");
    }

    /* Item rows — scroll_offset/selected are filtered-view indexes;
     * map each row through the filter to the true item. */
    int scroll = p->scroll_offset;
    for (int row = 0; row < vis_rows; row++) {
        int fidx = scroll + row;
        int idx = filtered_to_item(p, fidx);
        if (idx < 0)
            break;

        int is_selected = (fidx == p->selected);

        /* Left accent bar — colored, 1 column wide */
        if (is_selected) {
            emit_fg(out, p->selected_marker_color);
            emit_bg(out, p->selected_bg);
        } else {
            emit_fg(out, p->border_color);
            emit_bg(out, p->selected_bg);
        }
        dynamic_buffer_append_str(out, "\xe2\x96\x8c"); /* ▌ left-half block */

        /* Row content */
        if (is_selected) {
            emit_bg(out, p->selected_bg);
            emit_fg(out, p->selected_fg);
        } else {
            emit_bg(out, p->selected_bg);
            emit_fg(out, p->item_color);
        }

        /* 1-space indent after bar */
        dynamic_buffer_append_str(out, " ");

        /* Item text, padded to content_width - 1 (indent) */
        int text_max = content_width - 1;
        if (text_max < 1)
            text_max = 1;

        const char *item = p->items[idx];
        int item_w = str_display_width(item);
        if (item_w > text_max) {
            int col = 0;
            for (size_t i = 0; item[i] && col < text_max; i++) {
                if ((unsigned char)item[i] == 0x1b && item[i + 1] == '[') {
                    dynamic_buffer_append(out, &item[i], 1);
                    size_t j = i + 1;
                    while (item[j] && !((unsigned char)item[j] >= 'A' &&
                                        (unsigned char)item[j] <= 'Z')) {
                        dynamic_buffer_append(out, &item[j], 1);
                        j++;
                    }
                    if (item[j]) {
                        dynamic_buffer_append(out, &item[j], 1);
                        i = j;
                    }
                } else {
                    int clen = tui_utf8_char_len(&item[i]);
                    uint32_t cp = tui_utf8_decode(&item[i], clen);
                    int w = tui_codepoint_width(cp);
                    if (col + w > text_max)
                        break;
                    dynamic_buffer_append(out, &item[i], clen);
                    col += w;
                    i += clen - 1;
                }
            }
            for (; col < text_max; col++)
                dynamic_buffer_append_str(out, " ");
        } else {
            dynamic_buffer_append_str(out, item);
            for (int i = item_w; i < text_max; i++)
                dynamic_buffer_append_str(out, " ");
        }

        dynamic_buffer_append_str(out, SGR_RESET);
        dynamic_buffer_append_str(out, EL_TO_END);
        /* No \r\n after the last visible item row — the cursor stays on
         * the final content line so the runtime's stale-line erasure
         * correctly clears lines below when the list shrinks. */
        if (row < vis_rows - 1 && fidx + 1 < filtered_count(p))
            dynamic_buffer_append_str(out, "\r\n");
    }
}

/* ===== component vtable ===== */

static TuiInitResult list_popup_init(void *config)
{
    (void)config;
    TuiListPopup *p = tui_list_popup_create();
    if (!p)
        return tui_init_result_none(NULL);
    return tui_init_result_none((TuiModel *)p);
}

static TuiUpdateResult list_popup_update(TuiModel *model, TuiMsg msg)
{
    (void)model;
    (void)msg;
    /* List popup is purely presentational — parent handles key routing */
    return tui_update_result_none();
}

static TuiView list_popup_view_slot(const TuiModel *model, DynamicBuffer *out)
{
    tui_list_popup_view((const TuiListPopup *)model, out);
    return tui_view_default(out);
}

static void list_popup_free(TuiModel *model)
{
    tui_list_popup_free((TuiListPopup *)model);
}

static const TuiComponent list_popup_component = {
    .init = list_popup_init,
    .update = list_popup_update,
    .view = list_popup_view_slot,
    .free = list_popup_free,
};

const TuiComponent *tui_list_popup_component(void)
{
    return &list_popup_component;
}
