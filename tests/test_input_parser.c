/* test_input_parser.c - Tests for SGR mouse parsing, including v2-aligned
 * wheel left/right buttons and SGR modifier-bit extraction (Shift/Meta/Ctrl).
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <bloom-boba/input_parser.h>
#include <bloom-boba/msg.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

/* Feed a NUL-terminated escape sequence and return the single resulting
 * message. Asserts exactly one message came out. */
static TuiMsg parse_one(const char *seq)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    TuiMsg msgs[4];
    int n = tui_input_parser_parse(p, (const unsigned char *)seq, strlen(seq),
                                   msgs, 4);
    assert(n == 1);
    tui_input_parser_free(p);
    return msgs[0];
}

/* ----- basic buttons ---------------------------------------------------- */

static void test_left_press(void)
{
    TuiMsg m = parse_one("\033[<0;5;10M");
    assert(m.type == TUI_MSG_MOUSE);
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_PRESS);
    assert(m.data.mouse.col == 5);
    assert(m.data.mouse.row == 10);
    assert(m.data.mouse.mods == 0);
}

static void test_left_release(void)
{
    TuiMsg m = parse_one("\033[<0;5;10m");
    assert(m.type == TUI_MSG_MOUSE);
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_RELEASE);
    assert(m.data.mouse.mods == 0);
}

static void test_middle_press(void)
{
    TuiMsg m = parse_one("\033[<1;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_MIDDLE);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_PRESS);
}

static void test_right_press(void)
{
    TuiMsg m = parse_one("\033[<2;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_RIGHT);
}

static void test_release_no_button(void)
{
    /* Button code 3 = release / no specific button (legacy X10 semantic). */
    TuiMsg m = parse_one("\033[<3;5;10m");
    assert(m.data.mouse.button == TUI_MOUSE_RELEASE);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_RELEASE);
}

/* ----- wheel (including new wheel left/right) --------------------------- */

static void test_wheel_up(void)
{
    TuiMsg m = parse_one("\033[<64;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_UP);
    assert(m.data.mouse.mods == 0);
}

static void test_wheel_down(void)
{
    TuiMsg m = parse_one("\033[<65;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_DOWN);
}

static void test_wheel_left(void)
{
    TuiMsg m = parse_one("\033[<66;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_LEFT);
}

static void test_wheel_right(void)
{
    TuiMsg m = parse_one("\033[<67;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_RIGHT);
}

/* ----- modifier bits ---------------------------------------------------- */

static void test_shift_left_click(void)
{
    /* button = 0 (left) | 4 (shift) = 4 */
    TuiMsg m = parse_one("\033[<4;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.mods == TUI_MOD_SHIFT);
}

static void test_meta_left_click(void)
{
    /* button = 0 | 8 (meta) = 8 */
    TuiMsg m = parse_one("\033[<8;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.mods == TUI_MOD_META);
}

static void test_ctrl_left_click(void)
{
    /* button = 0 | 16 (ctrl) = 16 */
    TuiMsg m = parse_one("\033[<16;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.mods == TUI_MOD_CTRL);
}

static void test_ctrl_shift_wheel_up(void)
{
    /* button = 64 (wheel up) | 4 (shift) | 16 (ctrl) = 84 */
    TuiMsg m = parse_one("\033[<84;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_UP);
    assert(m.data.mouse.mods == (TUI_MOD_SHIFT | TUI_MOD_CTRL));
}

/* ----- motion ----------------------------------------------------------- */

static void test_motion_left_held(void)
{
    /* button = 0 (left) | 32 (motion) = 32 */
    TuiMsg m = parse_one("\033[<32;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_MOTION);
    assert(m.data.mouse.mods == 0);
}

static void test_motion_with_ctrl(void)
{
    /* button = 0 | 32 (motion) | 16 (ctrl) = 48 */
    TuiMsg m = parse_one("\033[<48;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_MOTION);
    assert(m.data.mouse.mods == TUI_MOD_CTRL);
}

/* ----- regression: regular keys still parse after these changes --------- */

static void test_regular_char_still_parses(void)
{
    TuiMsg m = parse_one("a");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'a');
}

int main(void)
{
    printf("Running input parser tests...\n");

    RUN_TEST(test_left_press);
    RUN_TEST(test_left_release);
    RUN_TEST(test_middle_press);
    RUN_TEST(test_right_press);
    RUN_TEST(test_release_no_button);

    RUN_TEST(test_wheel_up);
    RUN_TEST(test_wheel_down);
    RUN_TEST(test_wheel_left);
    RUN_TEST(test_wheel_right);

    RUN_TEST(test_shift_left_click);
    RUN_TEST(test_meta_left_click);
    RUN_TEST(test_ctrl_left_click);
    RUN_TEST(test_ctrl_shift_wheel_up);

    RUN_TEST(test_motion_left_held);
    RUN_TEST(test_motion_with_ctrl);

    RUN_TEST(test_regular_char_still_parses);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
