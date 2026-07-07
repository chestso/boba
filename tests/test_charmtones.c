/* test_charmtones.c - Unit tests for the CharmTone color palette */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/charmtones.h>
#include <boba/style.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

/* Helper: check that a TuiColor is RGB with the given values */
static void assert_rgb(TuiColor c, uint8_t r, uint8_t g, uint8_t b,
                       const char *name)
{
    if (c.type != TUI_COLOR_RGB) {
        fprintf(stderr, "  FAIL: %s: expected TUI_COLOR_RGB, got type %d\n",
                name, c.type);
        abort();
    }
    if (c.v.rgb.r != r || c.v.rgb.g != g || c.v.rgb.b != b) {
        fprintf(stderr,
                "  FAIL: %s: expected #%02X%02X%02X, got #%02X%02X%02X\n",
                name, r, g, b, c.v.rgb.r, c.v.rgb.g, c.v.rgb.b);
        abort();
    }
}

/* ---------- ANSI base 16 colors ---------- */

static void test_pepper(void)
{
    assert_rgb(tui_ct_pepper(), 0x20, 0x1f, 0x26, "Pepper");
}

static void test_coral(void)
{
    assert_rgb(tui_ct_coral(), 0xff, 0x57, 0x7d, "Coral");
}

static void test_guac(void)
{
    assert_rgb(tui_ct_guac(), 0x12, 0xc7, 0x8f, "Guac");
}

static void test_mustard(void)
{
    assert_rgb(tui_ct_mustard(), 0xf5, 0xef, 0x34, "Mustard");
}

static void test_charple(void)
{
    assert_rgb(tui_ct_charple(), 0x6b, 0x50, 0xff, "Charple");
}

static void test_dolly(void)
{
    assert_rgb(tui_ct_dolly(), 0xff, 0x60, 0xff, "Dolly");
}

static void test_turtle(void)
{
    assert_rgb(tui_ct_turtle(), 0x0a, 0xdc, 0xd9, "Turtle");
}

static void test_smoke(void)
{
    assert_rgb(tui_ct_smoke(), 0xbf, 0xbc, 0xc8, "Smoke");
}

static void test_oyster(void)
{
    assert_rgb(tui_ct_oyster(), 0x60, 0x5f, 0x6b, "Oyster");
}

static void test_salmon(void)
{
    assert_rgb(tui_ct_salmon(), 0xff, 0x7f, 0x90, "Salmon");
}

static void test_julep(void)
{
    assert_rgb(tui_ct_julep(), 0x00, 0xff, 0xb2, "Julep");
}

static void test_zest(void)
{
    assert_rgb(tui_ct_zest(), 0xe8, 0xfe, 0x96, "Zest");
}

static void test_hazy(void)
{
    assert_rgb(tui_ct_hazy(), 0x8b, 0x75, 0xff, "Hazy");
}

static void test_blush(void)
{
    assert_rgb(tui_ct_blush(), 0xff, 0x84, 0xff, "Blush");
}

static void test_bok(void)
{
    assert_rgb(tui_ct_bok(), 0x68, 0xff, 0xd6, "Bok");
}

static void test_butter(void)
{
    assert_rgb(tui_ct_butter(), 0xff, 0xfa, 0xf1, "Butter");
}

/* ---------- Extended palette ---------- */

static void test_flamingo(void)
{
    assert_rgb(tui_ct_flamingo(), 0xf9, 0x47, 0xe3, "Flamingo");
}

static void test_sardine(void)
{
    assert_rgb(tui_ct_sardine(), 0x4f, 0xbe, 0xfe, "Sardine");
}

static void test_squid(void)
{
    assert_rgb(tui_ct_squid(), 0x85, 0x83, 0x92, "Squid");
}

static void test_steep(void)
{
    assert_rgb(tui_ct_steep(), 0xd6, 0xd3, 0xdc, "Steep");
}

static void test_bbq(void)
{
    assert_rgb(tui_ct_bbq(), 0x2d, 0x2c, 0x36, "BBQ");
}

static void test_char(void)
{
    assert_rgb(tui_ct_char(), 0x3a, 0x39, 0x43, "Char");
}

static void test_iron(void)
{
    assert_rgb(tui_ct_iron(), 0x4d, 0x4c, 0x57, "Iron");
}

static void test_steam(void)
{
    assert_rgb(tui_ct_steam(), 0xa2, 0xa0, 0xad, "Steam");
}

static void test_sash(void)
{
    assert_rgb(tui_ct_sash(), 0xec, 0xeb, 0xf0, "Sash");
}

static void test_salt(void)
{
    assert_rgb(tui_ct_salt(), 0xf7, 0xf6, 0xfb, "Salt");
}

static void test_soda(void)
{
    assert_rgb(tui_ct_soda(), 0xfb, 0xfb, 0xfb, "Soda");
}

static void test_ice(void)
{
    assert_rgb(tui_ct_ice(), 0x00, 0xff, 0xfc, "Ice");
}

/* ---------- Verify all return TUI_COLOR_RGB ---------- */

static void test_all_colors_are_rgb_type(void)
{
    /* Spot-check a representative sample across the palette */
    TuiColor colors[] = {
        tui_ct_pepper(),
        tui_ct_coral(),
        tui_ct_guac(),
        tui_ct_charple(),
        tui_ct_turtle(),
        tui_ct_smoke(),
        tui_ct_oyster(),
        tui_ct_julep(),
        tui_ct_hazy(),
        tui_ct_bok(),
        tui_ct_butter(),
        tui_ct_flamingo(),
        tui_ct_sardine(),
        tui_ct_squid(),
        tui_ct_steep(),
        tui_ct_ice(),
    };
    int n = (int)(sizeof(colors) / sizeof(colors[0]));
    for (int i = 0; i < n; i++) {
        if (colors[i].type != TUI_COLOR_RGB) {
            fprintf(stderr, "  FAIL: color[%d] type is %d, expected %d\n",
                    i, colors[i].type, TUI_COLOR_RGB);
            abort();
        }
    }
}

/* ======================================================================== */

int main(void)
{
    printf("charmtones tests:\n");

    /* ANSI base 16 */
    RUN_TEST(test_pepper);
    RUN_TEST(test_coral);
    RUN_TEST(test_guac);
    RUN_TEST(test_mustard);
    RUN_TEST(test_charple);
    RUN_TEST(test_dolly);
    RUN_TEST(test_turtle);
    RUN_TEST(test_smoke);
    RUN_TEST(test_oyster);
    RUN_TEST(test_salmon);
    RUN_TEST(test_julep);
    RUN_TEST(test_zest);
    RUN_TEST(test_hazy);
    RUN_TEST(test_blush);
    RUN_TEST(test_bok);
    RUN_TEST(test_butter);

    /* Extended palette */
    RUN_TEST(test_flamingo);
    RUN_TEST(test_sardine);
    RUN_TEST(test_squid);
    RUN_TEST(test_steep);
    RUN_TEST(test_bbq);
    RUN_TEST(test_char);
    RUN_TEST(test_iron);
    RUN_TEST(test_steam);
    RUN_TEST(test_sash);
    RUN_TEST(test_salt);
    RUN_TEST(test_soda);
    RUN_TEST(test_ice);

    /* Type safety */
    RUN_TEST(test_all_colors_are_rgb_type);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
