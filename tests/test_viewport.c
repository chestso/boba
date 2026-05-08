/* test_viewport.c - Unit tests for the viewport component */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/cmd.h>
#include <bloom-boba/components/viewport.h>
#include <bloom-boba/dynamic_buffer.h>
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

/* ---------- tests ---------- */

static void test_create_and_free(void)
{
    TuiViewport *vp = tui_viewport_create();
    assert(vp != NULL);
    assert(tui_viewport_line_count(vp) == 0);
    tui_viewport_free(vp);
}

static void test_append_single_line(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    /* "hello\n" creates the "hello" line plus a new empty line after it */
    tui_viewport_append(vp, "hello\n", 6);
    assert(tui_viewport_line_count(vp) == 2);

    tui_viewport_free(vp);
}

static void test_append_multiple_lines(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    /* Each \n finalizes a line and starts a new empty one */
    const char *text = "line1\nline2\nline3\n";
    tui_viewport_append(vp, text, strlen(text));
    assert(tui_viewport_line_count(vp) == 4);

    tui_viewport_free(vp);
}

static void test_append_no_trailing_newline(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    /* Text without trailing newline creates one partial line */
    tui_viewport_append(vp, "partial", 7);
    assert(tui_viewport_line_count(vp) == 1);

    tui_viewport_free(vp);
}

static void test_clear(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    tui_viewport_append(vp, "hello\n", 6);
    assert(tui_viewport_line_count(vp) == 2);

    tui_viewport_clear(vp);
    assert(tui_viewport_line_count(vp) == 0);

    tui_viewport_free(vp);
}

static void test_scroll_down_and_up(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_auto_scroll(vp, 0);

    /* Add more lines than the viewport height */
    for (int i = 0; i < 20; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* Should start at top */
    tui_viewport_scroll_down(vp, 5);
    /* After scrolling down 5, we should not be at bottom */
    assert(tui_viewport_at_bottom(vp) == 0);

    /* Scroll to bottom */
    tui_viewport_scroll_to_bottom(vp);
    assert(tui_viewport_at_bottom(vp) == 1);

    /* Scroll up */
    tui_viewport_scroll_up(vp, 3);
    assert(tui_viewport_at_bottom(vp) == 0);

    tui_viewport_free(vp);
}

static void test_auto_scroll(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_auto_scroll(vp, 1);

    /* Add many lines with auto-scroll on */
    for (int i = 0; i < 20; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* With auto-scroll, should be at bottom */
    assert(tui_viewport_at_bottom(vp) == 1);

    tui_viewport_free(vp);
}

static void test_page_up_down(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_auto_scroll(vp, 0);

    for (int i = 0; i < 30; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* Page down then page up should work */
    tui_viewport_page_down(vp);
    assert(tui_viewport_at_bottom(vp) == 0);

    tui_viewport_scroll_to_bottom(vp);
    tui_viewport_page_up(vp);
    assert(tui_viewport_at_bottom(vp) == 0);

    tui_viewport_free(vp);
}

static void test_max_lines(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);
    tui_viewport_set_max_lines(vp, 10);

    /* Add more than max_lines */
    for (int i = 0; i < 20; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* Should be capped at max_lines */
    assert(tui_viewport_line_count(vp) <= 10);

    tui_viewport_free(vp);
}

static void test_view_output(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);
    tui_viewport_set_render_position(vp, 1, 1);

    tui_viewport_append(vp, "hello world\n", 12);

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_viewport_view(vp, buf);

    assert(dynamic_buffer_len(buf) > 0);
    assert(strstr(dynamic_buffer_data(buf), "hello world") != NULL);

    dynamic_buffer_destroy(buf);
    tui_viewport_free(vp);
}

static void test_set_size(void)
{
    TuiViewport *vp = tui_viewport_create();

    tui_viewport_set_size(vp, 120, 40);
    assert(vp->width == 120);
    assert(vp->height == 40);

    tui_viewport_free(vp);
}

static void test_wrap_mode(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 10, 5);

    tui_viewport_set_wrap_mode(vp, 1);
    assert(vp->wrap_mode == 1);

    tui_viewport_set_wrap_mode(vp, 0);
    assert(vp->wrap_mode == 0);

    tui_viewport_free(vp);
}

/* ---------- copy-mode / selection tests ---------- */

/* Helper: free any update result command */
static void run_update(TuiViewport *vp, TuiMsg msg)
{
    const TuiComponent *c = tui_viewport_component();
    TuiUpdateResult r = c->update((TuiModel *)vp, msg);
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

static void test_enter_copy_mode_via_ctrl_space(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_focused(vp, 1);
    tui_viewport_append(vp, "alpha\nbeta\n", 11);

    /* C-SPC enters copy-mode */
    run_update(vp, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    assert(vp->copy_mode == 1);
    assert(vp->has_mark == 0);
    assert(vp->cursor_visual_line == 0);
    assert(vp->cursor_col == 0);

    /* Second C-SPC sets the mark */
    run_update(vp, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    assert(vp->has_mark == 1);

    /* Escape with mark clears mark */
    run_update(vp, tui_msg_key(TUI_KEY_ESCAPE, 0, 0));
    assert(vp->has_mark == 0);
    assert(vp->copy_mode == 1);

    /* Escape without mark exits copy-mode */
    run_update(vp, tui_msg_key(TUI_KEY_ESCAPE, 0, 0));
    assert(vp->copy_mode == 0);

    tui_viewport_free(vp);
}

static void test_cursor_navigation(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 3);
    tui_viewport_set_focused(vp, 1);
    tui_viewport_append(vp, "one\ntwo\nthree\nfour\nfive\n", 24);

    tui_viewport_enter_copy_mode(vp);
    assert(vp->cursor_visual_line == vp->y_offset);

    /* C-n moves down; cursor advances. */
    size_t start = vp->cursor_visual_line;
    run_update(vp, tui_msg_key(TUI_KEY_NONE, 'n', TUI_MOD_CTRL));
    assert(vp->cursor_visual_line == start + 1);

    /* C-p moves back up. */
    run_update(vp, tui_msg_key(TUI_KEY_NONE, 'p', TUI_MOD_CTRL));
    assert(vp->cursor_visual_line == start);

    /* C-e jumps to end of line; "one" has display_width 3. */
    run_update(vp, tui_msg_key(TUI_KEY_NONE, 'e', TUI_MOD_CTRL));
    assert(vp->cursor_col == 3);

    /* C-a jumps back to start. */
    run_update(vp, tui_msg_key(TUI_KEY_NONE, 'a', TUI_MOD_CTRL));
    assert(vp->cursor_col == 0);

    tui_viewport_free(vp);
}

static void test_cursor_follow_scrolls(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 3);
    tui_viewport_set_focused(vp, 1);
    tui_viewport_set_auto_scroll(vp, 0);

    for (int i = 0; i < 20; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line%d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* Scroll back to top, enter copy mode at top of visible area. */
    tui_viewport_scroll_up(vp, 1000);
    tui_viewport_enter_copy_mode(vp);
    size_t initial_offset = vp->y_offset;

    /* Move cursor down past the bottom of the visible area. */
    for (int i = 0; i < 5; i++)
        run_update(vp, tui_msg_key(TUI_KEY_NONE, 'n', TUI_MOD_CTRL));

    /* The viewport should have scrolled to keep the cursor visible. */
    assert(vp->y_offset > initial_offset);
    assert(vp->cursor_visual_line >= vp->y_offset);
    assert(vp->cursor_visual_line < vp->y_offset + (size_t)vp->height);

    tui_viewport_free(vp);
}

static void test_extract_selection_same_line(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_focused(vp, 1);
    tui_viewport_append(vp, "hello world\n", 12);

    tui_viewport_enter_copy_mode(vp);
    /* Mark at col 0, cursor at col 4 → selection "hello". */
    vp->mark_visual_line = 0;
    vp->mark_col = 0;
    vp->has_mark = 1;
    vp->cursor_visual_line = 0;
    vp->cursor_col = 4; /* inclusive: chars 0..4 → "hello" */

    char *out = NULL;
    size_t len = 0;
    tui_viewport_extract_selection(vp, &out, &len);
    assert(out != NULL);
    assert(len == 5);
    assert(memcmp(out, "hello", 5) == 0);
    free(out);

    tui_viewport_free(vp);
}

static void test_extract_selection_strips_ansi(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_focused(vp, 1);
    /* "RED" wrapped in red SGR */
    const char *line = "\033[31mRED\033[0m\n";
    tui_viewport_append(vp, line, strlen(line));

    tui_viewport_enter_copy_mode(vp);
    vp->mark_visual_line = 0;
    vp->mark_col = 0;
    vp->has_mark = 1;
    vp->cursor_visual_line = 0;
    vp->cursor_col = 2; /* "RED" */

    char *out = NULL;
    size_t len = 0;
    tui_viewport_extract_selection(vp, &out, &len);
    assert(out != NULL);
    assert(len == 3);
    assert(memcmp(out, "RED", 3) == 0);
    /* No ESC bytes leaked through. */
    for (size_t i = 0; i < len; i++)
        assert(out[i] != '\033');
    free(out);

    tui_viewport_free(vp);
}

static void test_extract_selection_multi_line(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_focused(vp, 1);
    tui_viewport_append(vp, "line1\nline2\nline3\n", 18);

    tui_viewport_enter_copy_mode(vp);
    /* From line1 col 2 to line3 col 1: "ne1\nline2\nli" */
    vp->mark_visual_line = 0;
    vp->mark_col = 2;
    vp->has_mark = 1;
    vp->cursor_visual_line = 2;
    vp->cursor_col = 1; /* inclusive → 2 chars on last line */

    char *out = NULL;
    size_t len = 0;
    tui_viewport_extract_selection(vp, &out, &len);
    assert(out != NULL);
    /* Expected: "ne1" + "\n" + "line2" + "\n" + "li" */
    const char *expected = "ne1\nline2\nli";
    assert(len == strlen(expected));
    assert(memcmp(out, expected, len) == 0);
    free(out);

    tui_viewport_free(vp);
}

static void test_m_w_emits_clipboard_command(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_focused(vp, 1);
    tui_viewport_append(vp, "copyme\n", 7);

    tui_viewport_enter_copy_mode(vp);
    vp->mark_visual_line = 0;
    vp->mark_col = 0;
    vp->has_mark = 1;
    vp->cursor_visual_line = 0;
    vp->cursor_col = 5; /* "copyme" */

    const TuiComponent *c = tui_viewport_component();
    TuiUpdateResult r = c->update(
        (TuiModel *)vp, tui_msg_key(TUI_KEY_NONE, 'w', TUI_MOD_ALT));
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(r.cmd->payload.clipboard.len == 6);
    assert(memcmp(r.cmd->payload.clipboard.text, "copyme", 6) == 0);
    /* Mark should be cleared after copy. */
    assert(vp->has_mark == 0);
    tui_cmd_free(r.cmd);

    tui_viewport_free(vp);
}

static void test_mouse_left_press_starts_selection(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_render_position(vp, 1, 1);
    tui_viewport_set_focused(vp, 1);
    tui_viewport_append(vp, "abcdef\nghijkl\n", 14);

    /* Click at row 1, col 3 (1-indexed; local row 0, col 2). */
    run_update(vp, tui_msg_mouse(TUI_MOUSE_LEFT, TUI_MOUSE_ACTION_PRESS, 3, 1));
    assert(vp->copy_mode == 1);
    assert(vp->has_mark == 1);
    assert(vp->cursor_col == 2);
    assert(vp->cursor_visual_line == vp->y_offset);
    assert(vp->mouse_dragging == 1);

    /* Drag to row 2, col 5 (local row 1, col 4). */
    run_update(vp,
               tui_msg_mouse(TUI_MOUSE_LEFT, TUI_MOUSE_ACTION_MOTION, 5, 2));
    assert(vp->cursor_visual_line == vp->y_offset + 1);
    assert(vp->cursor_col == 4);

    /* Release stops dragging. */
    run_update(vp,
               tui_msg_mouse(TUI_MOUSE_LEFT, TUI_MOUSE_ACTION_RELEASE, 5, 2));
    assert(vp->mouse_dragging == 0);

    tui_viewport_free(vp);
}

static void test_contains_hit_test(void)
{
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 20, 5);
    tui_viewport_set_render_position(vp, 3, 2); /* render at row 3, col 2 */

    /* Inside corners */
    assert(tui_viewport_contains(vp, 3, 2) == 1);
    assert(tui_viewport_contains(vp, 7, 21) == 1);
    /* Outside */
    assert(tui_viewport_contains(vp, 2, 2) == 0);
    assert(tui_viewport_contains(vp, 3, 1) == 0);
    assert(tui_viewport_contains(vp, 8, 2) == 0);
    assert(tui_viewport_contains(vp, 3, 22) == 0);

    tui_viewport_free(vp);
}

/* ---------- main ---------- */

int main(void)
{
    printf("viewport tests:\n");

    RUN_TEST(test_create_and_free);
    RUN_TEST(test_append_single_line);
    RUN_TEST(test_append_multiple_lines);
    RUN_TEST(test_append_no_trailing_newline);
    RUN_TEST(test_clear);
    RUN_TEST(test_scroll_down_and_up);
    RUN_TEST(test_auto_scroll);
    RUN_TEST(test_page_up_down);
    RUN_TEST(test_max_lines);
    RUN_TEST(test_view_output);
    RUN_TEST(test_set_size);
    RUN_TEST(test_wrap_mode);

    RUN_TEST(test_enter_copy_mode_via_ctrl_space);
    RUN_TEST(test_cursor_navigation);
    RUN_TEST(test_cursor_follow_scrolls);
    RUN_TEST(test_extract_selection_same_line);
    RUN_TEST(test_extract_selection_strips_ansi);
    RUN_TEST(test_extract_selection_multi_line);
    RUN_TEST(test_m_w_emits_clipboard_command);
    RUN_TEST(test_mouse_left_press_starts_selection);
    RUN_TEST(test_contains_hit_test);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
