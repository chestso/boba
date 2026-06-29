/* style.h - Color, style, and layout primitives (Lipgloss-equivalent)
 *
 * boba folds Charm's three v2 libraries — Bubbletea (runtime),
 * Bubbles (components), and Lipgloss (style/layout) — into one C
 * library. This header is the Lipgloss half: TuiColor for color values,
 * TuiStyle for declarative styled text rendering, and a small set of
 * layout primitives (Place / Join / Border).
 */

#ifndef BOBA_STYLE_H
#define BOBA_STYLE_H

#include <stddef.h>
#include <stdint.h>

/* ----- Colors ---------------------------------------------------------- */

typedef enum
{
    TUI_COLOR_NONE = 0, /* Inherit / no color */
    TUI_COLOR_ANSI16,   /* Standard 16-color palette (0..15) */
    TUI_COLOR_ANSI256,  /* Extended 256-color palette (0..255) */
    TUI_COLOR_RGB,      /* Truecolor (24-bit) */
} TuiColorType;

typedef struct
{
    TuiColorType type;
    union
    {
        int ansi; /* 0..15 for ANSI16, 0..255 for ANSI256 */
        struct
        {
            uint8_t r, g, b;
        } rgb;
    } v;
} TuiColor;

/* Constructors. All return values; no allocation. */
TuiColor tui_color_none(void);

/* Auto-pick: 0..15 → ANSI16, 16..255 → ANSI256. Out-of-range clamps. */
TuiColor tui_color_ansi(int n);

TuiColor tui_color_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Parse "#rrggbb" or "rrggbb". Returns TUI_COLOR_NONE on parse failure. */
TuiColor tui_color_hex(const char *hex);

/* Adaptive: resolves immediately based on detected terminal background.
 * Returns `dark` on dark backgrounds, `light` otherwise. */
TuiColor tui_color_adaptive(TuiColor light, TuiColor dark);

/* Detect whether the terminal has a dark background.
 *
 * Resolution order:
 *   1. TUI_DARK env var ("1" / "0").
 *   2. COLORFGBG env var (parsed for bg index).
 *   3. Default: 1 (assume dark — by far the most common terminal default).
 *
 * Result is cached for the process. */
int tui_has_dark_background(void);

/* Format an SGR fg/bg sequence for this color, including the leading
 * CSI and trailing 'm'. Writes up to bufsize bytes (always null-terminates
 * if bufsize > 0). Returns the number of bytes written (excluding null),
 * or 0 if color is NONE. */
size_t tui_color_format_fg(TuiColor c, char *buf, size_t bufsize);
size_t tui_color_format_bg(TuiColor c, char *buf, size_t bufsize);

/* ----- Borders --------------------------------------------------------- */

typedef struct
{
    const char *top;
    const char *bottom;
    const char *left;
    const char *right;
    const char *top_left;
    const char *top_right;
    const char *bottom_left;
    const char *bottom_right;
} TuiBorder;

/* Prefab borders. Each is one display column wide for the side
 * characters. Strings are NUL-terminated UTF-8. */
extern const TuiBorder TUI_BORDER_NORMAL;  /* ┌─┐│└─┘ */
extern const TuiBorder TUI_BORDER_ROUNDED; /* ╭─╮│╰─╯ */
extern const TuiBorder TUI_BORDER_THICK;   /* ┏━┓┃┗━┛ */
extern const TuiBorder TUI_BORDER_DOUBLE;  /* ╔═╗║╚═╝ */
extern const TuiBorder TUI_BORDER_HIDDEN;  /* spaces, occupies space */

/* Title alignment within a horizontal border edge. */
typedef enum
{
    TUI_BORDER_TITLE_LEFT = 0,
    TUI_BORDER_TITLE_CENTER = 1,
    TUI_BORDER_TITLE_RIGHT = 2,
} TuiBorderTitleAlign;

/* tui_border_render_horizontal is declared below the TuiStyle definition. */

/* ----- Style ----------------------------------------------------------- */

typedef enum
{
    TUI_ALIGN_LEFT = 0,
    TUI_ALIGN_CENTER = 1,
    TUI_ALIGN_RIGHT = 2,
    TUI_ALIGN_TOP = 0,
    TUI_ALIGN_MIDDLE = 1,
    TUI_ALIGN_BOTTOM = 2,
} TuiAlign;

typedef struct
{
    TuiColor fg;
    TuiColor bg;

    int bold;
    int italic;
    int underline;
    int strikethrough;
    int reverse;
    int blink;
    int faint;

    /* Box model. -1 = unset (inherit / use natural size). */
    int width;
    int height;
    int padding_top, padding_right, padding_bottom, padding_left;
    int margin_top, margin_right, margin_bottom, margin_left;

    int align_h; /* TUI_ALIGN_LEFT/CENTER/RIGHT */
    int align_v; /* TUI_ALIGN_TOP/MIDDLE/BOTTOM */

    /* Border. NULL = no border. Use prefab pointers (TUI_BORDER_NORMAL etc). */
    const TuiBorder *border;
    TuiColor border_fg;
    TuiColor border_bg;

    /* When set, render() ignores width/height/padding/margin/border and
     * just emits SGR-wrapped content. */
    int inline_;
} TuiStyle;

/* Default-initialized style: no colors, no attrs, no constraints. */
TuiStyle tui_style_new(void);

/* Builder setters — each returns a modified copy. Chainable:
 *
 *     TuiStyle s = tui_style_bold(
 *         tui_style_foreground(tui_style_new(), tui_color_hex("#ff8800")),
 *         1);
 */
TuiStyle tui_style_foreground(TuiStyle s, TuiColor c);
TuiStyle tui_style_background(TuiStyle s, TuiColor c);
TuiStyle tui_style_bold(TuiStyle s, int on);
TuiStyle tui_style_italic(TuiStyle s, int on);
TuiStyle tui_style_underline(TuiStyle s, int on);
TuiStyle tui_style_strikethrough(TuiStyle s, int on);
TuiStyle tui_style_reverse(TuiStyle s, int on);
TuiStyle tui_style_blink(TuiStyle s, int on);
TuiStyle tui_style_faint(TuiStyle s, int on);

TuiStyle tui_style_width(TuiStyle s, int w);
TuiStyle tui_style_height(TuiStyle s, int h);

TuiStyle tui_style_padding(TuiStyle s, int top, int right, int bottom,
                           int left);
TuiStyle tui_style_padding_all(TuiStyle s, int n);
TuiStyle tui_style_padding_x(TuiStyle s, int n); /* left + right */
TuiStyle tui_style_padding_y(TuiStyle s, int n); /* top + bottom */

TuiStyle tui_style_margin(TuiStyle s, int top, int right, int bottom, int left);
TuiStyle tui_style_margin_all(TuiStyle s, int n);
TuiStyle tui_style_margin_x(TuiStyle s, int n);
TuiStyle tui_style_margin_y(TuiStyle s, int n);

TuiStyle tui_style_align_h(TuiStyle s, int align);
TuiStyle tui_style_align_v(TuiStyle s, int align);

TuiStyle tui_style_border(TuiStyle s, const TuiBorder *border);
TuiStyle tui_style_border_foreground(TuiStyle s, TuiColor c);
TuiStyle tui_style_border_background(TuiStyle s, TuiColor c);

TuiStyle tui_style_inline(TuiStyle s, int on);

/* Render content through this style. Returns a malloc'd, NUL-terminated
 * string. Caller frees. Returns NULL on alloc failure or if style is NULL.
 *
 * The result includes ANSI SGR sequences for colors/attrs, padding,
 * borders, alignment, and margins. */
char *tui_style_render(const TuiStyle *style, const char *content);

/* Render one horizontal border edge as a styled string sized to `width`
 * display columns. `top` selects the edge: 1 = border->top, 0 = border->bottom.
 *
 * The chosen edge string tiles cyclically across the available width
 * (matches lipgloss's renderHorizontalEdge). If `style` is non-NULL, it
 * is applied inline (SGR-only — no padding/margin/box).
 *
 * If `title` is non-NULL and non-empty, it is embedded inside the line
 * surrounded by `title_pad_left`/`title_pad_right` spaces, positioned
 * per `align`. The remaining columns on each side are tiled with the
 * edge string. If title + paddings exceed `width`, the title is emitted
 * in full and the line overflows beyond `width` — matches lipgloss's
 * no-Width-set behaviour; caller pre-truncates if needed.
 *
 * Returns a malloc'd, NUL-terminated string. Caller frees. NULL on
 * alloc failure or invalid arguments (border==NULL, width<=0). */
char *tui_border_render_horizontal(const TuiBorder *border, int top, int width,
                                   const TuiStyle *style, const char *title,
                                   TuiBorderTitleAlign align,
                                   int title_pad_left, int title_pad_right);

/* Compute rendered display width (terminal columns) without doing the
 * full render. Includes padding/border/margin contributions. */
int tui_style_get_width(const TuiStyle *style, const char *content);

/* Compute rendered display height (lines) without doing the full render. */
int tui_style_get_height(const TuiStyle *style, const char *content);

/* ----- Layout primitives ---------------------------------------------- */

/* Place a string within a (width, height) box at the given alignment.
 * Pads with spaces (horizontal) or blank lines (vertical) to reach the
 * box dimensions. Content longer than the box is truncated.
 *
 * halign / valign: TUI_ALIGN_LEFT/CENTER/RIGHT and TUI_ALIGN_TOP/MIDDLE/
 * BOTTOM respectively.
 *
 * Returns a malloc'd, NUL-terminated string. NULL on alloc failure. */
char *tui_place(int width, int height, int halign, int valign,
                const char *content);

/* Join a list of multi-line blocks side-by-side. Each block can have a
 * different number of lines; valign positions them along the vertical
 * axis. Block widths are taken as the natural max-line-width per block;
 * shorter lines within a block are right-padded with spaces.
 *
 * Returns a malloc'd, NUL-terminated string. NULL on alloc failure or
 * empty input. */
char *tui_join_horizontal(int valign, const char *const *blocks,
                          int n_blocks);

/* Join a list of multi-line blocks vertically. Each block becomes one
 * row in the result; halign positions narrower blocks horizontally.
 *
 * Returns a malloc'd, NUL-terminated string. NULL on alloc failure or
 * empty input. */
char *tui_join_vertical(int halign, const char *const *blocks, int n_blocks);

#endif /* BOBA_STYLE_H */
