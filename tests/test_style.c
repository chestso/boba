/* test_style.c - Tests for color, style, render, and layout primitives. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/style.h>
#include <bloom-boba/unicode.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

/* ----- Colors ---------------------------------------------------------- */

static void test_color_none(void)
{
    TuiColor c = tui_color_none();
    assert(c.type == TUI_COLOR_NONE);
}

static void test_color_ansi_low(void)
{
    TuiColor c = tui_color_ansi(3);
    assert(c.type == TUI_COLOR_ANSI16);
    assert(c.v.ansi == 3);
}

static void test_color_ansi_high(void)
{
    TuiColor c = tui_color_ansi(200);
    assert(c.type == TUI_COLOR_ANSI256);
    assert(c.v.ansi == 200);
}

static void test_color_rgb(void)
{
    TuiColor c = tui_color_rgb(255, 128, 0);
    assert(c.type == TUI_COLOR_RGB);
    assert(c.v.rgb.r == 255 && c.v.rgb.g == 128 && c.v.rgb.b == 0);
}

static void test_color_hex_with_hash(void)
{
    TuiColor c = tui_color_hex("#ff8800");
    assert(c.type == TUI_COLOR_RGB);
    assert(c.v.rgb.r == 0xff && c.v.rgb.g == 0x88 && c.v.rgb.b == 0x00);
}

static void test_color_hex_without_hash(void)
{
    TuiColor c = tui_color_hex("00ff00");
    assert(c.type == TUI_COLOR_RGB);
    assert(c.v.rgb.r == 0 && c.v.rgb.g == 0xff && c.v.rgb.b == 0);
}

static void test_color_hex_invalid(void)
{
    assert(tui_color_hex(NULL).type == TUI_COLOR_NONE);
    assert(tui_color_hex("xyz").type == TUI_COLOR_NONE);
    assert(tui_color_hex("#fff").type == TUI_COLOR_NONE); /* short */
}

static void test_color_format_fg_ansi(void)
{
    char buf[32];
    /* ANSI 16 normal range (0-7) → 30..37 */
    size_t n = tui_color_format_fg(tui_color_ansi(1), buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf, "\033[31m") == 0);
    /* ANSI 16 bright range (8-15) → 90..97 */
    n = tui_color_format_fg(tui_color_ansi(9), buf, sizeof(buf));
    assert(strcmp(buf, "\033[91m") == 0);
}

static void test_color_format_fg_rgb(void)
{
    char buf[32];
    tui_color_format_fg(tui_color_rgb(255, 128, 0), buf, sizeof(buf));
    assert(strcmp(buf, "\033[38;2;255;128;0m") == 0);
}

static void test_color_format_bg_256(void)
{
    char buf[32];
    tui_color_format_bg(tui_color_ansi(200), buf, sizeof(buf));
    assert(strcmp(buf, "\033[48;5;200m") == 0);
}

static void test_color_adaptive_dark(void)
{
    /* TUI_DARK=1 forces dark detection in tui_has_dark_background. */
    /* The function caches its result, so this only works if no prior
     * test triggered detection. We keep this as a smoke test rather
     * than an exact assertion. */
    TuiColor light = tui_color_rgb(0xff, 0xff, 0xff);
    TuiColor dark = tui_color_rgb(0x10, 0x10, 0x10);
    TuiColor c = tui_color_adaptive(light, dark);
    /* Either resolution is valid depending on env; just check it picked
     * one of the two. */
    assert(c.type == TUI_COLOR_RGB);
    assert((c.v.rgb.r == 0xff && c.v.rgb.g == 0xff) || (c.v.rgb.r == 0x10 && c.v.rgb.g == 0x10));
}

/* ----- Style attributes ----------------------------------------------- */

static void test_render_inline_no_attrs(void)
{
    TuiStyle s = tui_style_inline(tui_style_new(), 1);
    char *out = tui_style_render(&s, "hi");
    assert(out != NULL);
    assert(strcmp(out, "hi") == 0);
    free(out);
}

static void test_render_inline_bold(void)
{
    TuiStyle s = tui_style_inline(tui_style_bold(tui_style_new(), 1), 1);
    char *out = tui_style_render(&s, "hi");
    assert(out != NULL);
    assert(strcmp(out, "\033[1mhi\033[0m") == 0);
    free(out);
}

static void test_render_inline_combined_attrs(void)
{
    TuiStyle s = tui_style_inline(
        tui_style_underline(
            tui_style_bold(
                tui_style_foreground(tui_style_new(), tui_color_ansi(1)), 1),
            1),
        1);
    char *out = tui_style_render(&s, "x");
    /* Bold + underline + fg-red, emitted as a single CSI ... m. */
    assert(strstr(out, "\033[") != NULL);
    assert(strstr(out, "1") != NULL);  /* bold */
    assert(strstr(out, "4") != NULL);  /* underline */
    assert(strstr(out, "31") != NULL); /* fg red */
    assert(strstr(out, "x") != NULL);
    assert(strstr(out, "\033[0m") != NULL);
    free(out);
}

/* ----- Width / padding / alignment ------------------------------------ */

static void test_render_width_pads_right(void)
{
    TuiStyle s = tui_style_width(tui_style_new(), 10);
    char *out = tui_style_render(&s, "hi");
    /* "hi" + 8 spaces = 10 visible columns; no SGR needed (no attrs). */
    assert((int)tui_utf8_display_width_ansi(out, strlen(out)) == 10);
    free(out);
}

static void test_render_align_center(void)
{
    TuiStyle s = tui_style_align_h(tui_style_width(tui_style_new(), 10),
                                   TUI_ALIGN_CENTER);
    char *out = tui_style_render(&s, "hi");
    /* "  hi      " — 2 left, 2 hi, 6 right = 10 cols. */
    assert(out != NULL);
    assert((int)tui_utf8_display_width_ansi(out, strlen(out)) == 10);
    /* First char should be space; "hi" should be present. */
    assert(out[0] == ' ');
    assert(strstr(out, "hi") != NULL);
    free(out);
}

static void test_render_align_right(void)
{
    TuiStyle s = tui_style_align_h(tui_style_width(tui_style_new(), 5),
                                   TUI_ALIGN_RIGHT);
    char *out = tui_style_render(&s, "hi");
    assert(strcmp(out, "   hi") == 0);
    free(out);
}

static void test_render_padding(void)
{
    TuiStyle s = tui_style_padding_all(tui_style_new(), 1);
    char *out = tui_style_render(&s, "hi");
    /* 3 lines: blank, " hi ", blank. Each 4 cols wide. */
    /* Count newlines: 2 (between 3 rows). */
    int nl = 0;
    for (const char *p = out; *p; p++)
        if (*p == '\n')
            nl++;
    assert(nl == 2);
    /* Middle row should contain "hi". */
    assert(strstr(out, " hi ") != NULL);
    free(out);
}

static void test_render_margin(void)
{
    TuiStyle s = tui_style_margin(tui_style_new(), 1, 2, 1, 2);
    char *out = tui_style_render(&s, "hi");
    /* Top blank line + content row + bottom blank line: 2 newlines.
     * Content row has 2 left-margin spaces, "hi", 2 right-margin spaces. */
    assert(out != NULL);
    int nl = 0;
    for (const char *p = out; *p; p++)
        if (*p == '\n')
            nl++;
    assert(nl == 2);
    assert(strstr(out, "  hi  ") != NULL);
    free(out);
}

/* ----- Borders -------------------------------------------------------- */

static void test_render_border_normal(void)
{
    TuiStyle s = tui_style_border(tui_style_new(), &TUI_BORDER_NORMAL);
    char *out = tui_style_render(&s, "hi");
    /* Should contain corners and horizontals. */
    assert(strstr(out, "┌") != NULL);
    assert(strstr(out, "┐") != NULL);
    assert(strstr(out, "└") != NULL);
    assert(strstr(out, "┘") != NULL);
    assert(strstr(out, "│hi│") != NULL);
    free(out);
}

static void test_render_border_rounded(void)
{
    TuiStyle s = tui_style_border(tui_style_new(), &TUI_BORDER_ROUNDED);
    char *out = tui_style_render(&s, "x");
    assert(strstr(out, "╭") != NULL);
    assert(strstr(out, "╮") != NULL);
    assert(strstr(out, "╰") != NULL);
    assert(strstr(out, "╯") != NULL);
    free(out);
}

/* ----- Sizing helpers -------------------------------------------------- */

static void test_get_width_natural(void)
{
    TuiStyle s = tui_style_new();
    assert(tui_style_get_width(&s, "hello") == 5);
}

static void test_get_width_with_padding_margin_border(void)
{
    TuiStyle s = tui_style_border(
        tui_style_margin_all(
            tui_style_padding_all(tui_style_new(), 1), 2),
        &TUI_BORDER_NORMAL);
    /* Natural=5 + padding(1+1) + border(2) + margin(2+2) = 13 */
    assert(tui_style_get_width(&s, "hello") == 13);
}

/* ----- Layout: Place -------------------------------------------------- */

static void test_place_center_center(void)
{
    char *out = tui_place(11, 3, TUI_ALIGN_CENTER, TUI_ALIGN_MIDDLE, "hi");
    assert(out != NULL);
    /* 3 rows, 11 cols. Middle row centered: "    hi     " (4 + 2 + 5). */
    int nl = 0;
    for (const char *p = out; *p; p++)
        if (*p == '\n')
            nl++;
    assert(nl == 2);
    assert(strstr(out, "    hi     ") != NULL);
    free(out);
}

static void test_place_top_left(void)
{
    char *out = tui_place(5, 2, TUI_ALIGN_LEFT, TUI_ALIGN_TOP, "x");
    assert(out != NULL);
    /* Row 0: "x    "  Row 1: "     " */
    assert(strncmp(out, "x    \n     ", 11) == 0);
    free(out);
}

/* ----- Layout: Join ---------------------------------------------------- */

static void test_join_horizontal_simple(void)
{
    const char *blocks[] = { "a", "b", "c" };
    char *out = tui_join_horizontal(TUI_ALIGN_TOP, blocks, 3);
    assert(out != NULL);
    assert(strcmp(out, "abc") == 0);
    free(out);
}

static void test_join_horizontal_uneven_heights(void)
{
    const char *blocks[] = { "a\nb\nc", "1" };
    char *out = tui_join_horizontal(TUI_ALIGN_TOP, blocks, 2);
    assert(out != NULL);
    /* Row 0: a1   Row 1: b<space>   Row 2: c<space>
     * Block 2 is height 1; missing rows pad with spaces of width 1. */
    assert(strstr(out, "a1") != NULL);
    assert(strstr(out, "b ") != NULL);
    assert(strstr(out, "c ") != NULL);
    free(out);
}

static void test_join_vertical_simple(void)
{
    const char *blocks[] = { "abc", "de" };
    char *out = tui_join_vertical(TUI_ALIGN_LEFT, blocks, 2);
    /* Block 1 = "abc" (width 3). Block 2 = "de" → padded to "de ". */
    assert(strcmp(out, "abc\nde ") == 0);
    free(out);
}

static void test_join_vertical_center(void)
{
    const char *blocks[] = { "abcde", "x" };
    char *out = tui_join_vertical(TUI_ALIGN_CENTER, blocks, 2);
    assert(strcmp(out, "abcde\n  x  ") == 0);
    free(out);
}

int main(void)
{
    printf("Running style tests...\n");

    RUN_TEST(test_color_none);
    RUN_TEST(test_color_ansi_low);
    RUN_TEST(test_color_ansi_high);
    RUN_TEST(test_color_rgb);
    RUN_TEST(test_color_hex_with_hash);
    RUN_TEST(test_color_hex_without_hash);
    RUN_TEST(test_color_hex_invalid);
    RUN_TEST(test_color_format_fg_ansi);
    RUN_TEST(test_color_format_fg_rgb);
    RUN_TEST(test_color_format_bg_256);
    RUN_TEST(test_color_adaptive_dark);

    RUN_TEST(test_render_inline_no_attrs);
    RUN_TEST(test_render_inline_bold);
    RUN_TEST(test_render_inline_combined_attrs);

    RUN_TEST(test_render_width_pads_right);
    RUN_TEST(test_render_align_center);
    RUN_TEST(test_render_align_right);
    RUN_TEST(test_render_padding);
    RUN_TEST(test_render_margin);

    RUN_TEST(test_render_border_normal);
    RUN_TEST(test_render_border_rounded);

    RUN_TEST(test_get_width_natural);
    RUN_TEST(test_get_width_with_padding_margin_border);

    RUN_TEST(test_place_center_center);
    RUN_TEST(test_place_top_left);

    RUN_TEST(test_join_horizontal_simple);
    RUN_TEST(test_join_horizontal_uneven_heights);
    RUN_TEST(test_join_vertical_simple);
    RUN_TEST(test_join_vertical_center);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
