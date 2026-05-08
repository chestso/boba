/* test_focus.c - Tests for component focus + parser additions
 *
 * Covers:
 * - Input parser: Shift+Tab via xterm-legacy CSI Z
 * - Input parser: Ctrl+Space (NUL byte 0x00)
 * - Viewport: TUI_MSG_FOCUS / TUI_MSG_BLUR toggle the focused flag
 * - Viewport: tui_viewport_set_focused() public API
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/components/viewport.h>
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

/* Feed bytes into the parser and return the first message produced.
 * Asserts that exactly one message comes out. */
static TuiMsg parse_one(const unsigned char *bytes, size_t len)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    TuiMsg msgs[8];
    int n = tui_input_parser_parse(p, bytes, len, msgs, 8);
    assert(n == 1);
    tui_input_parser_free(p);
    return msgs[0];
}

/* ---------- parser tests ---------- */

static void test_parser_shift_tab_csi_z(void)
{
    /* xterm-legacy: ESC [ Z */
    const unsigned char seq[] = { 0x1B, '[', 'Z' };
    TuiMsg m = parse_one(seq, sizeof(seq));
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.key == TUI_KEY_TAB);
    assert((m.data.key.mods & TUI_MOD_SHIFT) != 0);
}

static void test_parser_tab_unmodified(void)
{
    /* Plain Tab should still arrive without the SHIFT modifier */
    const unsigned char seq[] = { 0x09 };
    TuiMsg m = parse_one(seq, sizeof(seq));
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.key == TUI_KEY_TAB);
    assert((m.data.key.mods & TUI_MOD_SHIFT) == 0);
}

static void test_parser_ctrl_space(void)
{
    /* Ctrl+Space arrives as NUL */
    const unsigned char seq[] = { 0x00 };
    TuiMsg m = parse_one(seq, sizeof(seq));
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.key == TUI_KEY_NONE);
    assert(m.data.key.rune == ' ');
    assert((m.data.key.mods & TUI_MOD_CTRL) != 0);
}

/* ---------- viewport focus tests ---------- */

static void test_viewport_initial_focused_zero(void)
{
    TuiViewport *vp = tui_viewport_create();
    assert(vp != NULL);
    assert(vp->focused == 0);
    tui_viewport_free(vp);
}

static void test_viewport_focus_message(void)
{
    TuiViewport *vp = tui_viewport_create();
    assert(vp != NULL);

    const TuiComponent *c = tui_viewport_component();
    TuiUpdateResult r;

    r = c->update((TuiModel *)vp, tui_msg_focus());
    assert(vp->focused == 1);
    if (r.cmd)
        tui_cmd_free(r.cmd);

    r = c->update((TuiModel *)vp, tui_msg_blur());
    assert(vp->focused == 0);
    if (r.cmd)
        tui_cmd_free(r.cmd);

    tui_viewport_free(vp);
}

static void test_viewport_set_focused_api(void)
{
    TuiViewport *vp = tui_viewport_create();
    assert(vp != NULL);
    tui_viewport_set_focused(vp, 1);
    assert(vp->focused == 1);
    tui_viewport_set_focused(vp, 0);
    assert(vp->focused == 0);
    /* Non-zero values normalize to 1 */
    tui_viewport_set_focused(vp, 42);
    assert(vp->focused == 1);
    tui_viewport_free(vp);
}

int main(void)
{
    printf("Running focus + parser tests...\n");

    RUN_TEST(test_parser_shift_tab_csi_z);
    RUN_TEST(test_parser_tab_unmodified);
    RUN_TEST(test_parser_ctrl_space);
    RUN_TEST(test_viewport_initial_focused_zero);
    RUN_TEST(test_viewport_focus_message);
    RUN_TEST(test_viewport_set_focused_api);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
