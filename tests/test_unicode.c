/* test_unicode.c - codepoint / grapheme-cluster width tests.
 *
 * Widths come from the generated src/unicode_tables.h (UCD 18.0.0).
 * These assertions pin the boundaries that matter to the renderer:
 * UAX #11 Narrow vs Wide, emoji presentation via VS16, and the
 * cluster unit that keeps a base, its combining marks, and its
 * modifiers from being measured or wrapped apart.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <boba/unicode.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

/* ------------------------------------------------------------------ */

static void test_ascii_and_controls(void)
{
    assert(tui_codepoint_width('A') == 1);
    assert(tui_codepoint_width(' ') == 1);
    assert(tui_codepoint_width('~') == 1);
    assert(tui_codepoint_width(0x00) == 0);
    assert(tui_codepoint_width(0x1F) == 0);
    assert(tui_codepoint_width(0x7F) == 0); /* DEL */
    assert(tui_codepoint_width(0x9B) == 0); /* C1 */
    assert(tui_codepoint_width(0xAD) == 0); /* soft hyphen */
    assert(tui_codepoint_width(0xA0) == 1); /* NBSP is one cell */
}

static void test_symbol_width(void)
{
    /* Miscellaneous Symbols and Dingbats is NOT wide wholesale: only
     * the Emoji_Presentation members are. The rest of the block is
     * East Asian Neutral or Ambiguous, one cell bare. */
    assert(tui_codepoint_width(0x2713) == 1); /* CHECK MARK */
    assert(tui_codepoint_width(0x2714) == 1); /* HEAVY CHECK MARK */
    assert(tui_codepoint_width(0x2717) == 1); /* BALLOT X */
    assert(tui_codepoint_width(0x2605) == 1); /* BLACK STAR (ambiguous) */
    assert(tui_codepoint_width(0x2611) == 1); /* BALLOT BOX WITH CHECK */
    assert(tui_codepoint_width(0x270F) == 1); /* PENCIL */

    assert(tui_codepoint_width(0x2693) == 2); /* ANCHOR */
    assert(tui_codepoint_width(0x2705) == 2); /* WHITE HEAVY CHECK MARK */
    assert(tui_codepoint_width(0x274C) == 2); /* CROSS MARK */
    assert(tui_codepoint_width(0x2728) == 2); /* SPARKLES */
    assert(tui_codepoint_width(0x231A) == 2); /* WATCH */
}

static void test_cjk_width(void)
{
    assert(tui_codepoint_width(0x3042) == 2); /* hiragana A */
    assert(tui_codepoint_width(0x4E2D) == 2); /* CJK middle */
    assert(tui_codepoint_width(0x1100) == 2); /* Hangul Jamo */
    assert(tui_codepoint_width(0xFF01) == 2); /* fullwidth */
}

static void test_zero_width(void)
{
    assert(tui_codepoint_width(0x0301) == 0);  /* combining acute */
    assert(tui_codepoint_width(0xFE0F) == 0);  /* VS16 */
    assert(tui_codepoint_width(0xFE0E) == 0);  /* VS15 */
    assert(tui_codepoint_width(0x200D) == 0);  /* ZWJ */
    assert(tui_codepoint_width(0x200C) == 0);  /* ZWNJ */
    assert(tui_codepoint_width(0x200B) == 0);  /* ZWSP */
    assert(tui_codepoint_width(0x2060) == 0);  /* WORD JOINER */
    assert(tui_codepoint_width(0xFEFF) == 0);  /* BOM */
    assert(tui_codepoint_width(0x1F3FB) == 0); /* emoji modifier */
}

static void test_cluster_width(void)
{
    /* Single codepoint: falls through to codepoint width. */
    assert(tui_cluster_width((const uint32_t[]){ 0x2713 }, 1) == 1);
    assert(tui_cluster_width((const uint32_t[]){ 0x2693 }, 1) == 2);
    assert(tui_cluster_width(NULL, 0) == 0);

    /* VS16 forces emoji presentation even on a Narrow base. */
    assert(tui_cluster_width((const uint32_t[]){ 0x270F, 0xFE0F }, 2) == 2);
    assert(tui_cluster_width((const uint32_t[]){ 0x2764, 0xFE0F }, 2) == 2);

    /* VS15 asks for text presentation: cancels the doubling on a
     * Narrow base, but never narrows a Wide one. */
    assert(tui_cluster_width((const uint32_t[]){ 0x2764, 0xFE0E }, 2) == 1);
    assert(tui_cluster_width((const uint32_t[]){ 0x4E2D, 0xFE0E }, 2) == 2);

    /* Regional indicator pair is one flag, two cells. */
    assert(tui_cluster_width((const uint32_t[]){ 0x1F1FA, 0x1F1F8 }, 2) == 2);

    /* Combining mark attaches without widening. */
    assert(tui_cluster_width((const uint32_t[]){ 'e', 0x0301 }, 2) == 1);

    /* Emoji modifier (skin tone) is zero-width: the emoji stays two. */
    assert(tui_cluster_width((const uint32_t[]){ 0x1F44D, 0x1F3FD }, 2) == 2);

    /* ZWJ sequence measures as its base. */
    assert(tui_cluster_width((const uint32_t[]){ 0x1F468, 0x200D, 0x1F4BB }, 3) == 2);
}

static void test_next_cluster_advance(void)
{
    /* "✏️x": the emoji + VS16 is one cluster (6 bytes, 2 cells), then
     * "x" (1 byte, 1 cell), then end. */
    const char *s = "\xE2\x9C\x8F\xEF\xB8\x8F"
                    "x";
    size_t len = strlen(s);
    size_t bytes = 0;
    size_t i = 0;

    assert(tui_next_cluster(s + i, len - i, &bytes) == 2);
    assert(bytes == 6);
    i += bytes;

    assert(tui_next_cluster(s + i, len - i, &bytes) == 1);
    assert(bytes == 1);
    i += bytes;

    assert(i == len);
    assert(tui_next_cluster(s + i, len - i, &bytes) == 0);
    assert(bytes == 0);

    /* A combining mark is carried with its base, never split. */
    s = "e\xCC\x81"
        "b";
    len = strlen(s);
    assert(tui_next_cluster(s, len, &bytes) == 1);
    assert(bytes == 3); /* 'e' + U+0301 */
}

static void test_string_width(void)
{
    assert(tui_utf8_display_width(NULL) == 0);
    assert(tui_utf8_display_width("") == 0);
    assert(tui_utf8_display_width("hello") == 5);
    assert(tui_utf8_display_width("\xE2\x9C\x93") == 1);                     /* ✓ */
    assert(tui_utf8_display_width("\xE2\x9C\x8F\xEF\xB8\x8F") == 2);         /* ✏️ */
    assert(tui_utf8_display_width("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == 2); /* 👍🏽 */
    assert(tui_utf8_display_width("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8") == 2); /* 🇺🇸 */
    assert(tui_utf8_display_width("\xE4\xB8\xAD\xE6\x96\x87") == 4);         /* 中文 */
    assert(tui_utf8_display_width("e\xCC\x81") == 1);                        /* e + acute */
    assert(tui_utf8_display_width("a\xCC\x81"
                                  "b") == 2);
}

static void test_string_width_ansi(void)
{
    const char *sgr = "\x1b[31m"
                      "ab"
                      "\x1b[0m";
    assert(tui_utf8_display_width_ansi(sgr, strlen(sgr)) == 2);

    sgr = "\x1b[1;38;5;208m"
          "\xE4\xB8\xAD"
          "\x1b[0m";
    assert(tui_utf8_display_width_ansi(sgr, strlen(sgr)) == 2);

    assert(tui_utf8_display_width_ansi("", 0) == 0);
}

int main(void)
{
    printf("test_unicode: codepoint and cluster widths\n");
    RUN_TEST(test_ascii_and_controls);
    RUN_TEST(test_symbol_width);
    RUN_TEST(test_cjk_width);
    RUN_TEST(test_zero_width);
    RUN_TEST(test_cluster_width);
    RUN_TEST(test_next_cluster_advance);
    RUN_TEST(test_string_width);
    RUN_TEST(test_string_width_ansi);
    printf("test_unicode: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
