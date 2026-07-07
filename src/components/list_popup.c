/* list_popup.c - Popup list selector implementation
 *
 * Renders a scrollable, navigable popup list with a rounded border.
 * Designed for inline REPL tab completion — renders below the textinput.
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

/* ===== internal helpers ===== */

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

static int max_item_display_width(const TuiListPopup *p)
{
    int max_w = 0;
    for (int i = 0; i < p->item_count; i++) {
        int w = str_display_width(p->items[i]);
        if (w > max_w)
            max_w = w;
    }
    return max_w;
}

static int compute_visible_rows(const TuiListPopup *p)
{
    if (p->item_count <= 0)
        return 0;

    int max_vis = p->max_visible > 0 ? p->max_visible : DEFAULT_MAX_VISIBLE;
    int term_h = p->terminal_height > 0 ? p->terminal_height : 24;
    /* Leave room for textinput (at least 1 line) + border (2 rows) */
    int available = term_h - 3;
    if (available < 1)
        available = 1;

    int vis = max_vis < available ? max_vis : available;
    if (vis > p->item_count)
        vis = p->item_count;
    return vis;
}

static int compute_popup_width(const TuiListPopup *p)
{
    if (p->width > 0)
        return p->width;

    int content_w = max_item_display_width(p);
    /* +2 for marker prefix ("▶ " or "  "), +2 for border sides */
    int w = content_w + 2 + 2 * BORDER_PADDING + 2;

    /* Title can make it wider */
    if (p->title) {
        int title_w = (int)strlen(p->title) + 4; /* "─ " + title + " ─" */
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

static void clamp_scroll_offset(TuiListPopup *p)
{
    int vis = compute_visible_rows(p);
    if (p->scroll_offset < 0)
        p->scroll_offset = 0;
    if (p->item_count > vis) {
        if (p->scroll_offset > p->item_count - vis)
            p->scroll_offset = p->item_count - vis;
    } else {
        p->scroll_offset = 0;
    }
}

static void ensure_selection_visible(TuiListPopup *p)
{
    int vis = compute_visible_rows(p);
    if (p->selected < p->scroll_offset)
        p->scroll_offset = p->selected;
    else if (p->selected >= p->scroll_offset + vis)
        p->scroll_offset = p->selected - vis + 1;
    clamp_scroll_offset(p);
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
    free(p->filter_prefix);
    free(p);
}

void tui_list_popup_set_items(TuiListPopup *p, const char *const *texts,
                              int count)
{
    if (!p)
        return;

    free_items(p);
    p->selected = -1;

    if (!texts || count <= 0)
        return;

    if (ensure_capacity(p, count) < 0)
        return;

    for (int i = 0; i < count; i++) {
        p->items[i] = strdup(texts[i]);
        if (!p->items[i]) {
            /* Free what we've got so far */
            for (int j = 0; j < i; j++)
                free(p->items[j]);
            p->item_count = 0;
            return;
        }
        p->item_count++;
    }

    p->selected = 0;
    p->scroll_offset = 0;
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
    p->selected = p->item_count > 0 ? 0 : -1;
    p->scroll_offset = 0;
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

int tui_list_popup_move_down(TuiListPopup *p)
{
    if (!p || p->item_count <= 0)
        return 0;
    int old = p->selected;
    if (p->item_count == 1)
        return 0;
    p->selected = (p->selected + 1) % p->item_count;
    ensure_selection_visible(p);
    return p->selected != old;
}

int tui_list_popup_move_up(TuiListPopup *p)
{
    if (!p || p->item_count <= 0)
        return 0;
    if (p->item_count == 1)
        return 0;
    int old = p->selected;
    p->selected = (p->selected - 1 + p->item_count) % p->item_count;
    ensure_selection_visible(p);
    return p->selected != old;
}

int tui_list_popup_move_page_down(TuiListPopup *p)
{
    if (!p || p->item_count <= 0)
        return 0;
    int vis = compute_visible_rows(p);
    int old = p->selected;
    p->selected += vis;
    if (p->selected >= p->item_count)
        p->selected = p->item_count - 1;
    ensure_selection_visible(p);
    return p->selected != old;
}

int tui_list_popup_move_page_up(TuiListPopup *p)
{
    if (!p || p->item_count <= 0)
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
    if (!p || p->item_count <= 0)
        return 0;
    int old = p->selected;
    p->selected = 0;
    p->scroll_offset = 0;
    return p->selected != old;
}

int tui_list_popup_move_bottom(TuiListPopup *p)
{
    if (!p || p->item_count <= 0)
        return 0;
    int old = p->selected;
    p->selected = p->item_count - 1;
    ensure_selection_visible(p);
    return p->selected != old;
}

const char *tui_list_popup_selected_text(const TuiListPopup *p)
{
    if (!p || p->selected < 0 || p->selected >= p->item_count)
        return NULL;
    return p->items[p->selected];
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
    clamp_scroll_offset(p);
}

void tui_list_popup_set_terminal_size(TuiListPopup *p, int w, int h)
{
    if (!p)
        return;
    p->terminal_width = w;
    p->terminal_height = h;
    clamp_scroll_offset(p);
}

void tui_list_popup_set_title(TuiListPopup *p, const char *title)
{
    if (!p)
        return;
    free(p->title);
    p->title = title ? strdup(title) : NULL;
}

void tui_list_popup_set_filter(TuiListPopup *p, const char *prefix)
{
    if (!p)
        return;
    free(p->filter_prefix);
    p->filter_prefix = prefix ? strdup(prefix) : NULL;
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
    if (!p || !out || !p->visible || p->item_count <= 0)
        return;

    int width = compute_popup_width(p);
    int vis_rows = compute_visible_rows(p);
    int inner_width = width - 2; /* minus border sides */

    /* Top border with optional title */
    char title_buf[256];
    const char *title = NULL;
    if (p->title) {
        if (p->filter_prefix) {
            snprintf(title_buf, sizeof(title_buf), "%s: \"%s\" (%d)",
                     p->title, p->filter_prefix, p->item_count);
        } else {
            snprintf(title_buf, sizeof(title_buf), "%s (%d)", p->title,
                     p->item_count);
        }
        title = title_buf;
    }

    /* Render top border with border color + title color */
    TuiStyle border_style = tui_style_new();
    if (p->border_color.type != TUI_COLOR_NONE)
        border_style = tui_style_border_foreground(border_style,
                                                   p->border_color);
    TuiStyle title_style = tui_style_new();
    if (p->title_color.type != TUI_COLOR_NONE)
        title_style = tui_style_foreground(title_style, p->title_color);

    char *top_border = tui_border_render_horizontal(
        &TUI_BORDER_ROUNDED, 1, width, &border_style, title,
        TUI_BORDER_TITLE_LEFT, 1, 1);
    if (top_border) {
        dynamic_buffer_append_str(out, top_border);
        dynamic_buffer_append_str(out, EL_TO_END);
        dynamic_buffer_append_str(out, "\r\n");
        free(top_border);
    }

    /* Item rows */
    int scroll = p->scroll_offset;
    for (int row = 0; row < vis_rows; row++) {
        int idx = scroll + row;
        if (idx >= p->item_count)
            break;

        int is_selected = (idx == p->selected);

        /* Left border — colored if border_color set */
        if (p->border_color.type != TUI_COLOR_NONE)
            emit_fg(out, p->border_color);
        dynamic_buffer_append_str(out, TUI_BORDER_ROUNDED.left);
        if (p->border_color.type != TUI_COLOR_NONE)
            dynamic_buffer_append_str(out, SGR_RESET);

        /* Selected row: set bg + fg */
        if (is_selected) {
            emit_bg(out, p->selected_bg);
            emit_fg(out, p->selected_fg);
        } else {
            emit_fg(out, p->item_color);
        }

        /* 2-char marker prefix */
        if (is_selected) {
            if (p->selected_marker_color.type != TUI_COLOR_NONE) {
                dynamic_buffer_append_str(out, SGR_RESET);
                emit_fg(out, p->selected_marker_color);
                emit_bg(out, p->selected_bg);
            }
            dynamic_buffer_append_str(out, "> ");
            if (p->selected_marker_color.type != TUI_COLOR_NONE) {
                dynamic_buffer_append_str(out, SGR_RESET);
                emit_fg(out, p->selected_fg);
                emit_bg(out, p->selected_bg);
            }
        } else {
            dynamic_buffer_append_str(out, "  ");
        }

        /* Item text, padded to inner_width - 2 (marker) - padding */
        int text_max = inner_width - 2 - BORDER_PADDING;
        if (text_max < 1)
            text_max = 1;

        const char *item = p->items[idx];
        int item_w = str_display_width(item);
        if (item_w > text_max) {
            /* Truncate to text_max display columns */
            size_t pos = 0;
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
            /* Pad remaining */
            for (; col < text_max; col++)
                dynamic_buffer_append_str(out, " ");
        } else {
            dynamic_buffer_append_str(out, item);
            for (int p2 = item_w; p2 < text_max; p2++)
                dynamic_buffer_append_str(out, " ");
        }

        /* Reset text colors, keep bg for selected through right border */
        dynamic_buffer_append_str(out, SGR_RESET);

        /* Right padding — for selected, extend bg */
        if (is_selected)
            emit_bg(out, p->selected_bg);
        for (int p2 = 0; p2 < BORDER_PADDING; p2++)
            dynamic_buffer_append_str(out, " ");
        dynamic_buffer_append_str(out, SGR_RESET);

        /* Right border — colored if border_color set */
        if (p->border_color.type != TUI_COLOR_NONE)
            emit_fg(out, p->border_color);
        dynamic_buffer_append_str(out, TUI_BORDER_ROUNDED.right);
        if (p->border_color.type != TUI_COLOR_NONE)
            dynamic_buffer_append_str(out, SGR_RESET);
        dynamic_buffer_append_str(out, EL_TO_END);
        dynamic_buffer_append_str(out, "\r\n");
    }

    /* Bottom border */
    char *bottom_border = tui_border_render_horizontal(
        &TUI_BORDER_ROUNDED, 0, width, &border_style, NULL,
        TUI_BORDER_TITLE_LEFT, 0, 0);
    if (bottom_border) {
        dynamic_buffer_append_str(out, bottom_border);
        dynamic_buffer_append_str(out, EL_TO_END);
        free(bottom_border);
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
