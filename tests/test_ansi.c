/* test_ansi.c - Tests for ANSI cursor movement helper functions */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/ansi_sequences.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                      \
    do {                                                         \
        if (strcmp((a), (b)) != 0) {                             \
            fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", \
                    __FILE__, __LINE__, (a), (b));               \
            abort();                                             \
        }                                                        \
    } while (0)

static void test_ansi_cursor_up(void)
{
    char buf[32];
    ansi_format_cursor_up(buf, sizeof(buf), 3);
    ASSERT_STR_EQ(buf, "\033[3A");
}

static void test_ansi_cursor_up_one(void)
{
    char buf[32];
    ansi_format_cursor_up(buf, sizeof(buf), 1);
    /* n=1 should emit CSI 1A (parameterized form for consistency) */
    ASSERT_STR_EQ(buf, "\033[1A");
}

static void test_ansi_cursor_down(void)
{
    char buf[32];
    ansi_format_cursor_down(buf, sizeof(buf), 5);
    ASSERT_STR_EQ(buf, "\033[5B");
}

static void test_ansi_cursor_fwd(void)
{
    char buf[32];
    ansi_format_cursor_fwd(buf, sizeof(buf), 10);
    ASSERT_STR_EQ(buf, "\033[10C");
}

static void test_ansi_cursor_back(void)
{
    char buf[32];
    ansi_format_cursor_back(buf, sizeof(buf), 7);
    ASSERT_STR_EQ(buf, "\033[7D");
}

static void test_ansi_cursor_up_zero(void)
{
    char buf[32];
    ansi_format_cursor_up(buf, sizeof(buf), 0);
    /* n=0 should be a no-op (empty string) since cursor up 0 is meaningless */
    ASSERT_STR_EQ(buf, "");
}

static void test_ansi_cursor_down_zero(void)
{
    char buf[32];
    ansi_format_cursor_down(buf, sizeof(buf), 0);
    ASSERT_STR_EQ(buf, "");
}

/* The kitty keyboard push carries the declared FLAG SET: the terminal
 * stores it on its own stack, so the sequence is the declaration. */
static void test_ansi_kbd_push_flag_one(void)
{
    char buf[ANSI_KBD_PUSH_BUFSIZE];
    ansi_format_kbd_push(buf, sizeof(buf), 1);
    ASSERT_STR_EQ(buf, "\033[>1u");
}

static void test_ansi_kbd_push_full_tier(void)
{
    char buf[ANSI_KBD_PUSH_BUFSIZE];
    /* 1 | 8 | 16 — disambiguate + all keys as escape codes + associated
     * text (the full-fidelity tier). */
    ansi_format_kbd_push(buf, sizeof(buf), 1 | 8 | 16);
    ASSERT_STR_EQ(buf, "\033[>25u");
}

static void test_ansi_kbd_push_zero_is_empty(void)
{
    char buf[ANSI_KBD_PUSH_BUFSIZE];
    ansi_format_kbd_push(buf, sizeof(buf), 0);
    /* No flags: nothing to declare, and an empty push is not a sequence
     * the terminal should see. */
    ASSERT_STR_EQ(buf, "");
}

static void test_ansi_kbd_push_short_buffer_is_empty(void)
{
    char buf[4];
    buf[0] = 'x';
    ansi_format_kbd_push(buf, sizeof(buf), 25);
    ASSERT_STR_EQ(buf, "");
}

int main(void)
{
    printf("ansi cursor movement tests:\n");

    RUN_TEST(test_ansi_cursor_up);
    RUN_TEST(test_ansi_cursor_up_one);
    RUN_TEST(test_ansi_cursor_down);
    RUN_TEST(test_ansi_cursor_fwd);
    RUN_TEST(test_ansi_cursor_back);
    RUN_TEST(test_ansi_cursor_up_zero);
    RUN_TEST(test_ansi_cursor_down_zero);
    RUN_TEST(test_ansi_kbd_push_flag_one);
    RUN_TEST(test_ansi_kbd_push_full_tier);
    RUN_TEST(test_ansi_kbd_push_zero_is_empty);
    RUN_TEST(test_ansi_kbd_push_short_buffer_is_empty);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
