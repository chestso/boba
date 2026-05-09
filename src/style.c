/* style.c - Color, style, and layout primitives implementation
 *
 * The bloom-boba analogue to Charm's Lipgloss v2. See style.h for the
 * full public API.
 */

#include <bloom-boba/ansi_sequences.h>
#include <bloom-boba/dynamic_buffer.h>
#include <bloom-boba/style.h>
#include <bloom-boba/unicode.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * Colors
 * ===================================================================== */

TuiColor tui_color_none(void)
{
    TuiColor c;
    memset(&c, 0, sizeof(c));
    c.type = TUI_COLOR_NONE;
    return c;
}

TuiColor tui_color_ansi(int n)
{
    TuiColor c;
    memset(&c, 0, sizeof(c));
    if (n < 0)
        n = 0;
    if (n > 255)
        n = 255;
    c.type = (n < 16) ? TUI_COLOR_ANSI16 : TUI_COLOR_ANSI256;
    c.v.ansi = n;
    return c;
}

TuiColor tui_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    TuiColor c;
    memset(&c, 0, sizeof(c));
    c.type = TUI_COLOR_RGB;
    c.v.rgb.r = r;
    c.v.rgb.g = g;
    c.v.rgb.b = b;
    return c;
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

TuiColor tui_color_hex(const char *hex)
{
    if (!hex)
        return tui_color_none();
    if (hex[0] == '#')
        hex++;
    if (strlen(hex) != 6)
        return tui_color_none();
    int v = 0;
    for (int i = 0; i < 6; i++) {
        int d = hex_digit(hex[i]);
        if (d < 0)
            return tui_color_none();
        v = (v << 4) | d;
    }
    return tui_color_rgb((uint8_t)((v >> 16) & 0xff),
                         (uint8_t)((v >> 8) & 0xff), (uint8_t)(v & 0xff));
}

int tui_has_dark_background(void)
{
    static int cached = -1;
    if (cached >= 0)
        return cached;

    /* Explicit override. */
    const char *force = getenv("TUI_DARK");
    if (force) {
        cached = (force[0] == '1') ? 1 : 0;
        return cached;
    }

    /* COLORFGBG = "fg;bg" with bg as 0..15 ANSI index. Indices 0-7 are
     * the dark half of the palette; bg in {0,1,2,3,4,5,6,8} is dark. */
    const char *fgbg = getenv("COLORFGBG");
    if (fgbg) {
        const char *semi = strchr(fgbg, ';');
        if (semi && semi[1]) {
            int bg = atoi(semi + 1);
            cached = (bg >= 0 && bg <= 6) || bg == 8;
            return cached;
        }
    }

    /* Default to dark — overwhelmingly the most common terminal preset. */
    cached = 1;
    return cached;
}

TuiColor tui_color_adaptive(TuiColor light, TuiColor dark)
{
    return tui_has_dark_background() ? dark : light;
}

size_t tui_color_format_fg(TuiColor c, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0)
        return 0;
    int n = 0;
    switch (c.type) {
    case TUI_COLOR_NONE:
        buf[0] = '\0';
        return 0;
    case TUI_COLOR_ANSI16:
        if (c.v.ansi < 8)
            n = snprintf(buf, bufsize, "\033[%dm", 30 + c.v.ansi);
        else
            n = snprintf(buf, bufsize, "\033[%dm", 90 + (c.v.ansi - 8));
        break;
    case TUI_COLOR_ANSI256:
        n = snprintf(buf, bufsize, "\033[38;5;%dm", c.v.ansi);
        break;
    case TUI_COLOR_RGB:
        n = snprintf(buf, bufsize, "\033[38;2;%d;%d;%dm", c.v.rgb.r,
                     c.v.rgb.g, c.v.rgb.b);
        break;
    }
    return (n > 0 && (size_t)n < bufsize) ? (size_t)n : 0;
}

size_t tui_color_format_bg(TuiColor c, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0)
        return 0;
    int n = 0;
    switch (c.type) {
    case TUI_COLOR_NONE:
        buf[0] = '\0';
        return 0;
    case TUI_COLOR_ANSI16:
        if (c.v.ansi < 8)
            n = snprintf(buf, bufsize, "\033[%dm", 40 + c.v.ansi);
        else
            n = snprintf(buf, bufsize, "\033[%dm", 100 + (c.v.ansi - 8));
        break;
    case TUI_COLOR_ANSI256:
        n = snprintf(buf, bufsize, "\033[48;5;%dm", c.v.ansi);
        break;
    case TUI_COLOR_RGB:
        n = snprintf(buf, bufsize, "\033[48;2;%d;%d;%dm", c.v.rgb.r,
                     c.v.rgb.g, c.v.rgb.b);
        break;
    }
    return (n > 0 && (size_t)n < bufsize) ? (size_t)n : 0;
}

/* =====================================================================
 * Borders (UTF-8 box-drawing characters)
 * ===================================================================== */

const TuiBorder TUI_BORDER_NORMAL = {
    .top = "─",
    .bottom = "─",
    .left = "│",
    .right = "│",
    .top_left = "┌",
    .top_right = "┐",
    .bottom_left = "└",
    .bottom_right = "┘",
};

const TuiBorder TUI_BORDER_ROUNDED = {
    .top = "─",
    .bottom = "─",
    .left = "│",
    .right = "│",
    .top_left = "╭",
    .top_right = "╮",
    .bottom_left = "╰",
    .bottom_right = "╯",
};

const TuiBorder TUI_BORDER_THICK = {
    .top = "━",
    .bottom = "━",
    .left = "┃",
    .right = "┃",
    .top_left = "┏",
    .top_right = "┓",
    .bottom_left = "┗",
    .bottom_right = "┛",
};

const TuiBorder TUI_BORDER_DOUBLE = {
    .top = "═",
    .bottom = "═",
    .left = "║",
    .right = "║",
    .top_left = "╔",
    .top_right = "╗",
    .bottom_left = "╚",
    .bottom_right = "╝",
};

const TuiBorder TUI_BORDER_HIDDEN = {
    .top = " ",
    .bottom = " ",
    .left = " ",
    .right = " ",
    .top_left = " ",
    .top_right = " ",
    .bottom_left = " ",
    .bottom_right = " ",
};

/* =====================================================================
 * Style builders
 * ===================================================================== */

TuiStyle tui_style_new(void)
{
    TuiStyle s;
    memset(&s, 0, sizeof(s));
    s.fg = tui_color_none();
    s.bg = tui_color_none();
    s.border_fg = tui_color_none();
    s.border_bg = tui_color_none();
    s.width = -1;
    s.height = -1;
    s.align_h = TUI_ALIGN_LEFT;
    s.align_v = TUI_ALIGN_TOP;
    return s;
}

TuiStyle tui_style_foreground(TuiStyle s, TuiColor c)
{
    s.fg = c;
    return s;
}
TuiStyle tui_style_background(TuiStyle s, TuiColor c)
{
    s.bg = c;
    return s;
}
TuiStyle tui_style_bold(TuiStyle s, int on)
{
    s.bold = on ? 1 : 0;
    return s;
}
TuiStyle tui_style_italic(TuiStyle s, int on)
{
    s.italic = on ? 1 : 0;
    return s;
}
TuiStyle tui_style_underline(TuiStyle s, int on)
{
    s.underline = on ? 1 : 0;
    return s;
}
TuiStyle tui_style_strikethrough(TuiStyle s, int on)
{
    s.strikethrough = on ? 1 : 0;
    return s;
}
TuiStyle tui_style_reverse(TuiStyle s, int on)
{
    s.reverse = on ? 1 : 0;
    return s;
}
TuiStyle tui_style_blink(TuiStyle s, int on)
{
    s.blink = on ? 1 : 0;
    return s;
}
TuiStyle tui_style_faint(TuiStyle s, int on)
{
    s.faint = on ? 1 : 0;
    return s;
}

TuiStyle tui_style_width(TuiStyle s, int w)
{
    s.width = w;
    return s;
}
TuiStyle tui_style_height(TuiStyle s, int h)
{
    s.height = h;
    return s;
}

TuiStyle tui_style_padding(TuiStyle s, int top, int right, int bottom, int left)
{
    s.padding_top = top;
    s.padding_right = right;
    s.padding_bottom = bottom;
    s.padding_left = left;
    return s;
}
TuiStyle tui_style_padding_all(TuiStyle s, int n)
{
    return tui_style_padding(s, n, n, n, n);
}
TuiStyle tui_style_padding_x(TuiStyle s, int n)
{
    s.padding_left = n;
    s.padding_right = n;
    return s;
}
TuiStyle tui_style_padding_y(TuiStyle s, int n)
{
    s.padding_top = n;
    s.padding_bottom = n;
    return s;
}

TuiStyle tui_style_margin(TuiStyle s, int top, int right, int bottom, int left)
{
    s.margin_top = top;
    s.margin_right = right;
    s.margin_bottom = bottom;
    s.margin_left = left;
    return s;
}
TuiStyle tui_style_margin_all(TuiStyle s, int n)
{
    return tui_style_margin(s, n, n, n, n);
}
TuiStyle tui_style_margin_x(TuiStyle s, int n)
{
    s.margin_left = n;
    s.margin_right = n;
    return s;
}
TuiStyle tui_style_margin_y(TuiStyle s, int n)
{
    s.margin_top = n;
    s.margin_bottom = n;
    return s;
}

TuiStyle tui_style_align_h(TuiStyle s, int align)
{
    s.align_h = align;
    return s;
}
TuiStyle tui_style_align_v(TuiStyle s, int align)
{
    s.align_v = align;
    return s;
}

TuiStyle tui_style_border(TuiStyle s, const TuiBorder *border)
{
    s.border = border;
    return s;
}
TuiStyle tui_style_border_foreground(TuiStyle s, TuiColor c)
{
    s.border_fg = c;
    return s;
}
TuiStyle tui_style_border_background(TuiStyle s, TuiColor c)
{
    s.border_bg = c;
    return s;
}

TuiStyle tui_style_inline(TuiStyle s, int on)
{
    s.inline_ = on ? 1 : 0;
    return s;
}

/* =====================================================================
 * Render helpers
 * ===================================================================== */

/* Append `n` copies of a single byte to buf. */
static void append_repeat(DynamicBuffer *buf, char c, int n)
{
    for (int i = 0; i < n; i++)
        dynamic_buffer_append(buf, &c, 1);
}

/* Append `n` spaces (display columns). */
static void append_spaces(DynamicBuffer *buf, int n)
{
    append_repeat(buf, ' ', n);
}

/* Build the style's "open" SGR sequence. Concatenates all parameters
 * into a single CSI ... m. Returns bytes written (0 if no style). */
static size_t format_style_open(const TuiStyle *s, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0)
        return 0;
    /* Collect params. */
    char params[256];
    size_t pos = 0;
#define ADD_PARAM(p)                                                 \
    do {                                                             \
        int n = snprintf(params + pos, sizeof(params) - pos, "%s%s", \
                         pos ? ";" : "", p);                         \
        if (n > 0 && (size_t)n < sizeof(params) - pos)               \
            pos += n;                                                \
    } while (0)

    if (s->bold)
        ADD_PARAM("1");
    if (s->faint)
        ADD_PARAM("2");
    if (s->italic)
        ADD_PARAM("3");
    if (s->underline)
        ADD_PARAM("4");
    if (s->blink)
        ADD_PARAM("5");
    if (s->reverse)
        ADD_PARAM("7");
    if (s->strikethrough)
        ADD_PARAM("9");

    /* fg */
    switch (s->fg.type) {
    case TUI_COLOR_NONE:
        break;
    case TUI_COLOR_ANSI16:
    {
        char p[8];
        snprintf(p, sizeof(p), "%d",
                 s->fg.v.ansi < 8 ? 30 + s->fg.v.ansi : 90 + s->fg.v.ansi - 8);
        ADD_PARAM(p);
        break;
    }
    case TUI_COLOR_ANSI256:
    {
        char p[16];
        snprintf(p, sizeof(p), "38;5;%d", s->fg.v.ansi);
        ADD_PARAM(p);
        break;
    }
    case TUI_COLOR_RGB:
    {
        char p[24];
        snprintf(p, sizeof(p), "38;2;%d;%d;%d", s->fg.v.rgb.r, s->fg.v.rgb.g,
                 s->fg.v.rgb.b);
        ADD_PARAM(p);
        break;
    }
    }

    /* bg */
    switch (s->bg.type) {
    case TUI_COLOR_NONE:
        break;
    case TUI_COLOR_ANSI16:
    {
        char p[8];
        snprintf(p, sizeof(p), "%d",
                 s->bg.v.ansi < 8 ? 40 + s->bg.v.ansi
                                  : 100 + s->bg.v.ansi - 8);
        ADD_PARAM(p);
        break;
    }
    case TUI_COLOR_ANSI256:
    {
        char p[16];
        snprintf(p, sizeof(p), "48;5;%d", s->bg.v.ansi);
        ADD_PARAM(p);
        break;
    }
    case TUI_COLOR_RGB:
    {
        char p[24];
        snprintf(p, sizeof(p), "48;2;%d;%d;%d", s->bg.v.rgb.r, s->bg.v.rgb.g,
                 s->bg.v.rgb.b);
        ADD_PARAM(p);
        break;
    }
    }

#undef ADD_PARAM

    if (pos == 0) {
        buf[0] = '\0';
        return 0;
    }
    int n = snprintf(buf, bufsize, "\033[%sm", params);
    return (n > 0 && (size_t)n < bufsize) ? (size_t)n : 0;
}

/* Format an SGR for the border (only fg/bg, no attrs). */
static size_t format_border_sgr(const TuiStyle *s, char *buf, size_t bufsize)
{
    /* Reuse format_style_open with a temp style holding only border fg/bg. */
    TuiStyle tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.fg = s->border_fg;
    tmp.bg = s->border_bg;
    return format_style_open(&tmp, buf, bufsize);
}

/* Compute display width of one logical line (skipping ANSI sequences). */
static int line_visual_width(const char *line, size_t len)
{
    return (int)tui_utf8_display_width_ansi(line, len);
}

/* Append `n` display columns worth of `glyph` (a UTF-8 string of width 1). */
static void append_glyph_repeat(DynamicBuffer *buf, const char *glyph, int n)
{
    size_t glen = strlen(glyph);
    for (int i = 0; i < n; i++)
        dynamic_buffer_append(buf, glyph, glen);
}

/* Truncate or pad the given line to exactly target_cols display columns
 * (skipping ANSI). Honors align_h for padding distribution. */
static void emit_line_padded(DynamicBuffer *buf, const char *line, size_t len,
                             int target_cols, int align_h)
{
    int actual = line_visual_width(line, len);
    if (actual >= target_cols) {
        /* No padding needed; emit as-is (truncation NYI, content fits). */
        dynamic_buffer_append(buf, line, len);
        return;
    }
    int extra = target_cols - actual;
    int left_pad = 0, right_pad = extra;
    if (align_h == TUI_ALIGN_CENTER) {
        left_pad = extra / 2;
        right_pad = extra - left_pad;
    } else if (align_h == TUI_ALIGN_RIGHT) {
        left_pad = extra;
        right_pad = 0;
    }
    append_spaces(buf, left_pad);
    dynamic_buffer_append(buf, line, len);
    append_spaces(buf, right_pad);
}

/* Linear list of (start, end) byte offsets for each \n-separated line. */
typedef struct
{
    size_t start;
    size_t end; /* exclusive */
} LineRef;

static LineRef *split_lines(const char *content, int *out_n)
{
    int n = 1;
    for (const char *p = content; *p; p++)
        if (*p == '\n')
            n++;
    LineRef *lines = (LineRef *)calloc((size_t)n, sizeof(LineRef));
    if (!lines) {
        *out_n = 0;
        return NULL;
    }
    int idx = 0;
    size_t start = 0;
    for (size_t i = 0;; i++) {
        if (content[i] == '\n' || content[i] == '\0') {
            lines[idx].start = start;
            lines[idx].end = i;
            idx++;
            if (content[i] == '\0')
                break;
            start = i + 1;
        }
    }
    *out_n = idx;
    return lines;
}

/* =====================================================================
 * tui_style_render
 * ===================================================================== */

static char *finalize_buf(DynamicBuffer *buf)
{
    const char *data = dynamic_buffer_data(buf);
    size_t len = dynamic_buffer_len(buf);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        dynamic_buffer_destroy(buf);
        return NULL;
    }
    memcpy(out, data, len);
    out[len] = '\0';
    dynamic_buffer_destroy(buf);
    return out;
}

char *tui_style_render(const TuiStyle *style, const char *content)
{
    if (!style)
        return NULL;
    if (!content)
        content = "";

    DynamicBuffer *buf = dynamic_buffer_create(256);
    if (!buf)
        return NULL;

    char open[256];
    size_t open_len = format_style_open(style, open, sizeof(open));
    int has_style = open_len > 0;

    /* Inline mode: just SGR-wrap. */
    if (style->inline_) {
        if (has_style)
            dynamic_buffer_append(buf, open, open_len);
        dynamic_buffer_append_str(buf, content);
        if (has_style)
            dynamic_buffer_append_str(buf, SGR_RESET);
        return finalize_buf(buf);
    }

    /* Split content into logical lines. */
    int n_lines = 0;
    LineRef *lines = split_lines(content, &n_lines);
    if (!lines) {
        dynamic_buffer_destroy(buf);
        return NULL;
    }

    /* Natural content width = max display width of any line. */
    int natural_width = 0;
    for (int i = 0; i < n_lines; i++) {
        int w = line_visual_width(content + lines[i].start,
                                  lines[i].end - lines[i].start);
        if (w > natural_width)
            natural_width = w;
    }

    int has_border = style->border != NULL;
    int border_cols = has_border ? 2 : 0;
    int border_rows = has_border ? 2 : 0;

    /* Inner content width (between padding edges). */
    int inner_w;
    if (style->width >= 0) {
        inner_w = style->width - style->padding_left - style->padding_right - border_cols;
        if (inner_w < 0)
            inner_w = 0;
    } else {
        inner_w = natural_width;
    }

    /* Inner content height (between padding edges). */
    int inner_h;
    if (style->height >= 0) {
        inner_h = style->height - style->padding_top - style->padding_bottom - border_rows;
        if (inner_h < 0)
            inner_h = 0;
    } else {
        inner_h = n_lines;
    }

    /* Distribute extra vertical space per align_v. */
    int v_extra = inner_h - n_lines;
    int v_top = 0, v_bottom = 0;
    if (v_extra > 0) {
        if (style->align_v == TUI_ALIGN_MIDDLE) {
            v_top = v_extra / 2;
            v_bottom = v_extra - v_top;
        } else if (style->align_v == TUI_ALIGN_BOTTOM) {
            v_top = v_extra;
        } else {
            v_bottom = v_extra;
        }
    }

    /* Total content rows after vertical alignment but before padding. */
    int padded_box_w = inner_w + style->padding_left + style->padding_right;

    char border_open[64];
    size_t border_open_len = 0;
    if (has_border)
        border_open_len = format_border_sgr(style, border_open,
                                            sizeof(border_open));

#define EMIT_MARGIN_LEFT()  append_spaces(buf, style->margin_left)
#define EMIT_MARGIN_RIGHT() append_spaces(buf, style->margin_right)
#define EMIT_NL()           dynamic_buffer_append(buf, "\n", 1)

    /* Top margin. */
    for (int i = 0; i < style->margin_top; i++)
        EMIT_NL();

    /* Top border row. */
    if (has_border) {
        EMIT_MARGIN_LEFT();
        if (border_open_len > 0)
            dynamic_buffer_append(buf, border_open, border_open_len);
        dynamic_buffer_append_str(buf, style->border->top_left);
        append_glyph_repeat(buf, style->border->top, padded_box_w);
        dynamic_buffer_append_str(buf, style->border->top_right);
        if (border_open_len > 0)
            dynamic_buffer_append_str(buf, SGR_RESET);
        EMIT_MARGIN_RIGHT();
        EMIT_NL();
    }

    /* Top padding rows (bg-colored, full-width spaces). */
    for (int i = 0; i < style->padding_top; i++) {
        EMIT_MARGIN_LEFT();
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->left);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        if (has_style)
            dynamic_buffer_append(buf, open, open_len);
        append_spaces(buf, padded_box_w);
        if (has_style)
            dynamic_buffer_append_str(buf, SGR_RESET);
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->right);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        EMIT_MARGIN_RIGHT();
        EMIT_NL();
    }

    /* Vertical-alignment top blanks (inside padding). */
    for (int i = 0; i < v_top; i++) {
        EMIT_MARGIN_LEFT();
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->left);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        if (has_style)
            dynamic_buffer_append(buf, open, open_len);
        append_spaces(buf, padded_box_w);
        if (has_style)
            dynamic_buffer_append_str(buf, SGR_RESET);
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->right);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        EMIT_MARGIN_RIGHT();
        EMIT_NL();
    }

    /* Content rows. Truncate the visible row count to inner_h - v_top - v_bottom. */
    int max_content_rows = inner_h - v_top - v_bottom;
    if (max_content_rows > n_lines)
        max_content_rows = n_lines;
    if (max_content_rows < 0)
        max_content_rows = 0;
    for (int i = 0; i < max_content_rows; i++) {
        EMIT_MARGIN_LEFT();
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->left);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        if (has_style)
            dynamic_buffer_append(buf, open, open_len);
        append_spaces(buf, style->padding_left);
        emit_line_padded(buf, content + lines[i].start,
                         lines[i].end - lines[i].start, inner_w,
                         style->align_h);
        append_spaces(buf, style->padding_right);
        if (has_style)
            dynamic_buffer_append_str(buf, SGR_RESET);
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->right);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        EMIT_MARGIN_RIGHT();
        EMIT_NL();
    }

    /* Vertical-alignment bottom blanks. */
    for (int i = 0; i < v_bottom; i++) {
        EMIT_MARGIN_LEFT();
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->left);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        if (has_style)
            dynamic_buffer_append(buf, open, open_len);
        append_spaces(buf, padded_box_w);
        if (has_style)
            dynamic_buffer_append_str(buf, SGR_RESET);
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->right);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        EMIT_MARGIN_RIGHT();
        EMIT_NL();
    }

    /* Bottom padding rows. */
    for (int i = 0; i < style->padding_bottom; i++) {
        EMIT_MARGIN_LEFT();
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->left);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        if (has_style)
            dynamic_buffer_append(buf, open, open_len);
        append_spaces(buf, padded_box_w);
        if (has_style)
            dynamic_buffer_append_str(buf, SGR_RESET);
        if (has_border) {
            if (border_open_len > 0)
                dynamic_buffer_append(buf, border_open, border_open_len);
            dynamic_buffer_append_str(buf, style->border->right);
            if (border_open_len > 0)
                dynamic_buffer_append_str(buf, SGR_RESET);
        }
        EMIT_MARGIN_RIGHT();
        EMIT_NL();
    }

    /* Bottom border row. */
    if (has_border) {
        EMIT_MARGIN_LEFT();
        if (border_open_len > 0)
            dynamic_buffer_append(buf, border_open, border_open_len);
        dynamic_buffer_append_str(buf, style->border->bottom_left);
        append_glyph_repeat(buf, style->border->bottom, padded_box_w);
        dynamic_buffer_append_str(buf, style->border->bottom_right);
        if (border_open_len > 0)
            dynamic_buffer_append_str(buf, SGR_RESET);
        EMIT_MARGIN_RIGHT();
        EMIT_NL();
    }

    /* Bottom margin. */
    for (int i = 0; i < style->margin_bottom; i++)
        EMIT_NL();

#undef EMIT_MARGIN_LEFT
#undef EMIT_MARGIN_RIGHT
#undef EMIT_NL

    /* Strip the trailing \n we just emitted (Lipgloss convention: result
     * doesn't end with a newline). */
    size_t blen = dynamic_buffer_len(buf);
    if (blen > 0 && dynamic_buffer_data(buf)[blen - 1] == '\n') {
        /* DynamicBuffer doesn't expose pop, so we copy out len-1 bytes. */
        const char *data = dynamic_buffer_data(buf);
        char *out = (char *)malloc(blen);
        if (!out) {
            free(lines);
            dynamic_buffer_destroy(buf);
            return NULL;
        }
        memcpy(out, data, blen - 1);
        out[blen - 1] = '\0';
        dynamic_buffer_destroy(buf);
        free(lines);
        return out;
    }

    free(lines);
    return finalize_buf(buf);
}

/* =====================================================================
 * Sizing helpers
 * ===================================================================== */

int tui_style_get_width(const TuiStyle *style, const char *content)
{
    if (!style)
        return 0;
    if (!content)
        content = "";
    if (style->inline_)
        return (int)tui_utf8_display_width_ansi(content, strlen(content));

    int n_lines = 0;
    LineRef *lines = split_lines(content, &n_lines);
    int natural = 0;
    if (lines) {
        for (int i = 0; i < n_lines; i++) {
            int w = line_visual_width(content + lines[i].start,
                                      lines[i].end - lines[i].start);
            if (w > natural)
                natural = w;
        }
        free(lines);
    }
    int border_cols = style->border ? 2 : 0;
    int inner_w = (style->width >= 0)
                      ? (style->width - style->padding_left - style->padding_right - border_cols)
                      : natural;
    if (inner_w < 0)
        inner_w = 0;
    return inner_w + style->padding_left + style->padding_right + border_cols + style->margin_left + style->margin_right;
}

int tui_style_get_height(const TuiStyle *style, const char *content)
{
    if (!style)
        return 0;
    if (!content)
        content = "";
    if (style->inline_)
        return 1;

    int n_lines = 0;
    LineRef *lines = split_lines(content, &n_lines);
    if (lines)
        free(lines);
    int border_rows = style->border ? 2 : 0;
    int inner_h = (style->height >= 0)
                      ? (style->height - style->padding_top - style->padding_bottom - border_rows)
                      : n_lines;
    if (inner_h < 0)
        inner_h = 0;
    return inner_h + style->padding_top + style->padding_bottom + border_rows + style->margin_top + style->margin_bottom;
}

/* =====================================================================
 * Layout primitives
 * ===================================================================== */

char *tui_place(int width, int height, int halign, int valign,
                const char *content)
{
    if (!content)
        content = "";

    int n_lines = 0;
    LineRef *lines = split_lines(content, &n_lines);
    if (!lines)
        return NULL;

    if (n_lines > height)
        n_lines = height;

    int v_extra = height - n_lines;
    int v_top = 0;
    if (v_extra > 0) {
        if (valign == TUI_ALIGN_MIDDLE)
            v_top = v_extra / 2;
        else if (valign == TUI_ALIGN_BOTTOM)
            v_top = v_extra;
    }

    DynamicBuffer *buf = dynamic_buffer_create(256);
    if (!buf) {
        free(lines);
        return NULL;
    }

    for (int i = 0; i < height; i++) {
        if (i < v_top) {
            append_spaces(buf, width);
        } else if (i - v_top < n_lines) {
            int idx = i - v_top;
            emit_line_padded(buf, content + lines[idx].start,
                             lines[idx].end - lines[idx].start, width, halign);
        } else {
            append_spaces(buf, width);
        }
        if (i < height - 1)
            dynamic_buffer_append(buf, "\n", 1);
    }
    free(lines);
    return finalize_buf(buf);
}

/* Compute (n_lines, max_visual_width) for a block string. */
static void block_dims(const char *block, int *out_lines, int *out_width)
{
    int n = 0, w = 0;
    LineRef *lr = split_lines(block, &n);
    if (lr) {
        for (int i = 0; i < n; i++) {
            int lw = line_visual_width(block + lr[i].start,
                                       lr[i].end - lr[i].start);
            if (lw > w)
                w = lw;
        }
        free(lr);
    }
    *out_lines = n;
    *out_width = w;
}

char *tui_join_horizontal(int valign, const char *const *blocks, int n_blocks)
{
    if (!blocks || n_blocks <= 0)
        return NULL;

    int *bw = (int *)calloc((size_t)n_blocks, sizeof(int));
    int *bn = (int *)calloc((size_t)n_blocks, sizeof(int));
    if (!bw || !bn) {
        free(bw);
        free(bn);
        return NULL;
    }
    int max_h = 0;
    for (int i = 0; i < n_blocks; i++) {
        block_dims(blocks[i] ? blocks[i] : "", &bn[i], &bw[i]);
        if (bn[i] > max_h)
            max_h = bn[i];
    }

    DynamicBuffer *buf = dynamic_buffer_create(256);
    if (!buf) {
        free(bw);
        free(bn);
        return NULL;
    }

    /* For each output row, append each block's contribution. */
    for (int row = 0; row < max_h; row++) {
        for (int i = 0; i < n_blocks; i++) {
            int v_extra = max_h - bn[i];
            int v_top = 0;
            if (v_extra > 0) {
                if (valign == TUI_ALIGN_MIDDLE)
                    v_top = v_extra / 2;
                else if (valign == TUI_ALIGN_BOTTOM)
                    v_top = v_extra;
            }
            if (row < v_top || row >= v_top + bn[i]) {
                append_spaces(buf, bw[i]);
            } else {
                int idx = row - v_top;
                int line_n = 0;
                LineRef *lr = split_lines(blocks[i] ? blocks[i] : "", &line_n);
                if (lr) {
                    emit_line_padded(buf, (blocks[i] ? blocks[i] : "") + lr[idx].start,
                                     lr[idx].end - lr[idx].start, bw[i],
                                     TUI_ALIGN_LEFT);
                    free(lr);
                }
            }
        }
        if (row < max_h - 1)
            dynamic_buffer_append(buf, "\n", 1);
    }
    free(bw);
    free(bn);
    return finalize_buf(buf);
}

char *tui_join_vertical(int halign, const char *const *blocks, int n_blocks)
{
    if (!blocks || n_blocks <= 0)
        return NULL;

    int max_w = 0;
    for (int i = 0; i < n_blocks; i++) {
        int n = 0, w = 0;
        block_dims(blocks[i] ? blocks[i] : "", &n, &w);
        if (w > max_w)
            max_w = w;
    }

    DynamicBuffer *buf = dynamic_buffer_create(256);
    if (!buf)
        return NULL;

    int first_row = 1;
    for (int i = 0; i < n_blocks; i++) {
        int n = 0;
        LineRef *lr = split_lines(blocks[i] ? blocks[i] : "", &n);
        if (!lr)
            continue;
        for (int j = 0; j < n; j++) {
            if (!first_row)
                dynamic_buffer_append(buf, "\n", 1);
            first_row = 0;
            emit_line_padded(buf, (blocks[i] ? blocks[i] : "") + lr[j].start,
                             lr[j].end - lr[j].start, max_w, halign);
        }
        free(lr);
    }
    return finalize_buf(buf);
}
