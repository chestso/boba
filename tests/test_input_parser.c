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

/* ----- Kitty keyboard protocol: press/release -------------------------- */

static void test_kitty_press_default_action(void)
{
    /* CSI 65 u — 'A' press. No event-type given → defaults to PRESS. */
    TuiMsg m = parse_one("\033[65u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'A');
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

static void test_kitty_press_explicit_event_type(void)
{
    /* CSI 65;1:1 u — 'A' press with explicit event-type 1. */
    TuiMsg m = parse_one("\033[65;1:1u");
    assert(m.data.key.rune == 'A');
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

static void test_kitty_repeat_maps_to_press(void)
{
    /* CSI 65;1:2 u — 'A' repeat. Folds into PRESS. */
    TuiMsg m = parse_one("\033[65;1:2u");
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

static void test_kitty_release(void)
{
    /* CSI 65;1:3 u — 'A' release. Type stays KEY_PRESS; action discriminates. */
    TuiMsg m = parse_one("\033[65;1:3u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'A');
    assert(m.data.key.action == TUI_KEY_ACTION_RELEASE);
}

static void test_kitty_release_with_ctrl(void)
{
    /* CSI 65;5:3 u — Ctrl+'A' release. */
    TuiMsg m = parse_one("\033[65;5:3u");
    assert(m.data.key.rune == 'A');
    assert(m.data.key.mods == TUI_MOD_CTRL);
    assert(m.data.key.action == TUI_KEY_ACTION_RELEASE);
}

static void test_kitty_release_special_key(void)
{
    /* CSI 13;1:3 u — Enter release. */
    TuiMsg m = parse_one("\033[13;1:3u");
    assert(m.data.key.key == TUI_KEY_ENTER);
    assert(m.data.key.action == TUI_KEY_ACTION_RELEASE);
}

/* Regression: standard CSI sequences with ';' separator still parse after
 * the param-array refactor. */
static void test_csi_ctrl_up_still_works(void)
{
    /* CSI 1;5A — Ctrl+Up arrow. */
    TuiMsg m = parse_one("\033[1;5A");
    assert(m.data.key.key == TUI_KEY_UP);
    assert(m.data.key.mods == TUI_MOD_CTRL);
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

/* ----- bracketed paste ------------------------------------------------- */

/* Feed the whole input in one shot and assert exactly `expected` messages
 * were produced. Caller is responsible for freeing each msg with
 * tui_msg_free(). */
static int parse_into(const char *input, size_t input_len, TuiMsg *out,
                      int max)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    int n = tui_input_parser_parse(p, (const unsigned char *)input, input_len,
                                   out, max);
    tui_input_parser_free(p);
    return n;
}

static void test_paste_simple(void)
{
    const char *input = "\033[200~hello\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[0].type == TUI_MSG_PASTE_START);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 5);
    assert(strcmp(msgs[1].data.paste.text, "hello") == 0);
    assert(msgs[2].type == TUI_MSG_PASTE_END);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_empty(void)
{
    /* 200~ immediately followed by 201~ — zero-length payload. */
    const char *input = "\033[200~\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[0].type == TUI_MSG_PASTE_START);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 0);
    assert(msgs[1].data.paste.text != NULL);
    assert(msgs[1].data.paste.text[0] == '\0');
    assert(msgs[2].type == TUI_MSG_PASTE_END);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_split_across_feeds(void)
{
    /* Same paste, but fed in two batches via two parse() calls on the
     * same parser. The second call should drain pending PASTE_END from
     * the first via the queued-pending path. */
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    TuiMsg msgs[8];

    const char *part1 = "\033[200~hel";
    int n1 = tui_input_parser_parse(p, (const unsigned char *)part1,
                                    strlen(part1), msgs, 8);
    /* Only PASTE_START so far. */
    assert(n1 == 1);
    assert(msgs[0].type == TUI_MSG_PASTE_START);

    const char *part2 = "lo\033[201~";
    int n2 = tui_input_parser_parse(p, (const unsigned char *)part2,
                                    strlen(part2), msgs, 8);
    assert(n2 == 2);
    assert(msgs[0].type == TUI_MSG_PASTE);
    assert(msgs[0].data.paste.len == 5);
    assert(strcmp(msgs[0].data.paste.text, "hello") == 0);
    assert(msgs[1].type == TUI_MSG_PASTE_END);
    tui_msg_free(&msgs[0]);
    tui_msg_free(&msgs[1]);
    tui_input_parser_free(p);
}

static void test_paste_contains_csi_not_reparsed(void)
{
    /* Paste containing what looks like a CSI sequence: must NOT be
     * dispatched as a key/cursor msg. Payload is the literal bytes. */
    const char *input = "\033[200~ab\033[1;2Hcd\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[1].type == TUI_MSG_PASTE);
    /* Payload: "ab\033[1;2Hcd" = 10 bytes */
    assert(msgs[1].data.paste.len == 10);
    assert(memcmp(msgs[1].data.paste.text, "ab\033[1;2Hcd", 10) == 0);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_contains_bare_esc(void)
{
    /* Bare ESC (not followed by [) inside the paste. The terminator
     * scanner should match \033 then mismatch on the next byte and
     * flush \033 back into the buffer. */
    const char *input = "\033[200~x\033yz\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 4);
    assert(memcmp(msgs[1].data.paste.text, "x\033yz", 4) == 0);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_grows_beyond_initial_buf(void)
{
    /* Force the geometric grow path: paste payload exceeds initial 256
     * bytes. */
    char input[2048];
    char expected[1024];
    size_t pos = 0;
    memcpy(input + pos, "\033[200~", 6);
    pos += 6;
    /* 1000 'A's */
    memset(input + pos, 'A', 1000);
    memset(expected, 'A', 1000);
    expected[1000] = '\0';
    pos += 1000;
    memcpy(input + pos, "\033[201~", 6);
    pos += 6;

    TuiMsg msgs[8];
    int n = parse_into(input, pos, msgs, 8);
    assert(n == 3);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 1000);
    assert(memcmp(msgs[1].data.paste.text, expected, 1000) == 0);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
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

    RUN_TEST(test_kitty_press_default_action);
    RUN_TEST(test_kitty_press_explicit_event_type);
    RUN_TEST(test_kitty_repeat_maps_to_press);
    RUN_TEST(test_kitty_release);
    RUN_TEST(test_kitty_release_with_ctrl);
    RUN_TEST(test_kitty_release_special_key);
    RUN_TEST(test_csi_ctrl_up_still_works);

    RUN_TEST(test_paste_simple);
    RUN_TEST(test_paste_empty);
    RUN_TEST(test_paste_split_across_feeds);
    RUN_TEST(test_paste_contains_csi_not_reparsed);
    RUN_TEST(test_paste_contains_bare_esc);
    RUN_TEST(test_paste_grows_beyond_initial_buf);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
