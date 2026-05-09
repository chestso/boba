/* test_textinput.c - Unit tests for the textinput component */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/cmd.h>
#include <bloom-boba/components/textinput.h>
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

/* ---------- helpers ---------- */

/* Send a string of ASCII characters to the input */
static void send_string(TuiTextInput *input, const char *s)
{
    for (const char *p = s; *p; p++) {
        tui_textinput_update(input, tui_msg_char((uint32_t)*p, 0));
    }
}

static void send_key(TuiTextInput *input, int key)
{
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_key(key, 0, 0));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

/* Free any command returned by update (avoid leaks in tests) */
static void send_char(TuiTextInput *input, char c)
{
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_char((uint32_t)c, 0));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

/* ---------- tests ---------- */

static void test_create_and_free(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    assert(input != NULL);
    assert(strcmp(tui_textinput_text(input), "") == 0);
    assert(tui_textinput_len(input) == 0);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_create_with_config(void)
{
    TuiTextInputConfig cfg = {
        .prompt = "> ",
        .width = 40,
        .height = 1,
        .multiline = 0,
    };
    TuiTextInput *input = tui_textinput_create(&cfg);
    assert(input != NULL);
    assert(input->width == 40);
    tui_textinput_free(input);
}

static void test_char_insertion(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_char(input, 'h');
    send_char(input, 'e');
    send_char(input, 'l');
    send_char(input, 'l');
    send_char(input, 'o');

    assert(strcmp(tui_textinput_text(input), "hello") == 0);
    assert(tui_textinput_len(input) == 5);
    assert(tui_textinput_cursor(input) == 5);
    tui_textinput_free(input);
}

static void test_backspace(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    send_key(input, TUI_KEY_BACKSPACE);

    assert(strcmp(tui_textinput_text(input), "hell") == 0);
    assert(tui_textinput_cursor(input) == 4);
    tui_textinput_free(input);
}

static void test_backspace_at_start(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    /* Backspace on empty input should do nothing */
    send_key(input, TUI_KEY_BACKSPACE);
    assert(strcmp(tui_textinput_text(input), "") == 0);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_delete_key(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    /* Move cursor to start */
    send_key(input, TUI_KEY_HOME);
    /* Delete first character */
    send_key(input, TUI_KEY_DELETE);

    assert(strcmp(tui_textinput_text(input), "ello") == 0);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_cursor_left_right(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "abc");
    assert(tui_textinput_cursor(input) == 3);

    send_key(input, TUI_KEY_LEFT);
    assert(tui_textinput_cursor(input) == 2);

    send_key(input, TUI_KEY_LEFT);
    assert(tui_textinput_cursor(input) == 1);

    send_key(input, TUI_KEY_RIGHT);
    assert(tui_textinput_cursor(input) == 2);

    tui_textinput_free(input);
}

static void test_cursor_left_at_start(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    /* Left on empty input should stay at 0 */
    send_key(input, TUI_KEY_LEFT);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_cursor_right_at_end(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "ab");
    assert(tui_textinput_cursor(input) == 2);

    /* Right at end should stay put */
    send_key(input, TUI_KEY_RIGHT);
    assert(tui_textinput_cursor(input) == 2);
    tui_textinput_free(input);
}

static void test_home_end(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    assert(tui_textinput_cursor(input) == 5);

    send_key(input, TUI_KEY_HOME);
    assert(tui_textinput_cursor(input) == 0);

    send_key(input, TUI_KEY_END);
    assert(tui_textinput_cursor(input) == 5);

    tui_textinput_free(input);
}

static void test_insert_in_middle(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hllo");
    /* Move cursor after 'h' */
    send_key(input, TUI_KEY_HOME);
    send_key(input, TUI_KEY_RIGHT);
    /* Insert 'e' */
    send_char(input, 'e');

    assert(strcmp(tui_textinput_text(input), "hello") == 0);
    assert(tui_textinput_cursor(input) == 2);
    tui_textinput_free(input);
}

static void test_set_text(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    tui_textinput_set_text(input, "preset");
    assert(strcmp(tui_textinput_text(input), "preset") == 0);
    assert(tui_textinput_len(input) == 6);
    tui_textinput_free(input);
}

static void test_clear(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    tui_textinput_clear(input);
    assert(strcmp(tui_textinput_text(input), "") == 0);
    assert(tui_textinput_len(input) == 0);
    tui_textinput_free(input);
}

static void test_focus(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);

    /* Default is focused */
    assert(tui_textinput_is_focused(input) == 1);
    tui_textinput_set_focus(input, 0);
    assert(tui_textinput_is_focused(input) == 0);
    tui_textinput_set_focus(input, 1);
    assert(tui_textinput_is_focused(input) == 1);
    tui_textinput_free(input);
}

static void test_unfocused_ignores_input(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    /* Explicitly unfocus (default is focused) */
    tui_textinput_set_focus(input, 0);

    send_char(input, 'a');
    assert(strcmp(tui_textinput_text(input), "") == 0);
    tui_textinput_free(input);
}

static void test_history_navigation(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_history_size(input, 10);

    tui_textinput_history_add(input, "first");
    tui_textinput_history_add(input, "second");

    /* Start with empty input — history uses prefix filter, empty matches all */

    /* Up arrow -> most recent history entry */
    send_key(input, TUI_KEY_UP);
    assert(strcmp(tui_textinput_text(input), "second") == 0);

    /* Up again -> older entry */
    send_key(input, TUI_KEY_UP);
    assert(strcmp(tui_textinput_text(input), "first") == 0);

    /* Down -> back to most recent */
    send_key(input, TUI_KEY_DOWN);
    assert(strcmp(tui_textinput_text(input), "second") == 0);

    /* Down again -> back to saved (empty) input */
    send_key(input, TUI_KEY_DOWN);
    assert(strcmp(tui_textinput_text(input), "") == 0);

    tui_textinput_free(input);
}

static void test_tab_emits_command(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "he");

    /* Press Tab -> should emit TUI_CMD_TAB_COMPLETE */
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_key(TUI_KEY_TAB, 0, 0));
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_TAB_COMPLETE);
    assert(strcmp(r.cmd->payload.tab_complete.prefix, "he") == 0);
    assert(r.cmd->payload.tab_complete.word_start == 0);
    tui_cmd_free(r.cmd);

    /* Text should be unchanged (app does the insertion) */
    assert(strcmp(tui_textinput_text(input), "he") == 0);

    tui_textinput_free(input);
}

static void test_tab_word_start(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "foo he");

    /* Tab should extract "he" starting at byte 4 */
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_key(TUI_KEY_TAB, 0, 0));
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_TAB_COMPLETE);
    assert(strcmp(r.cmd->payload.tab_complete.prefix, "he") == 0);
    assert(r.cmd->payload.tab_complete.word_start == 4);
    tui_cmd_free(r.cmd);

    tui_textinput_free(input);
}

static void test_tab_with_shift_is_noop(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "foo");
    size_t cursor_before = tui_textinput_cursor(input);

    /* Shift+Tab is not bound by textinput. It must not emit a
     * tab-complete command and must not modify the buffer — that
     * leaves the parent free to use it for focus cycling. */
    TuiUpdateResult r = tui_textinput_update(
        input, tui_msg_key(TUI_KEY_TAB, 0, TUI_MOD_SHIFT));
    assert(r.cmd == NULL);
    assert(strcmp(tui_textinput_text(input), "foo") == 0);
    assert(tui_textinput_cursor(input) == cursor_before);

    tui_textinput_free(input);
}

static void test_insert_completion(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "he");

    /* Simulate app inserting a completion */
    tui_textinput_insert_completion(input, 0, "hello");
    assert(strcmp(tui_textinput_text(input), "hello") == 0);
    assert(tui_textinput_cursor(input) == 5);

    tui_textinput_free(input);
}

static void test_view_output(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);

    /* View output should contain the text */
    assert(dynamic_buffer_len(buf) > 0);
    assert(strstr(dynamic_buffer_data(buf), "hello") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

static void test_set_cursor(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    tui_textinput_set_cursor(input, 2);
    assert(tui_textinput_cursor(input) == 2);

    /* Insert at cursor position */
    send_char(input, 'X');
    assert(strcmp(tui_textinput_text(input), "heXllo") == 0);

    tui_textinput_free(input);
}

static void test_line_count(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);

    send_string(input, "line1");
    assert(tui_textinput_line_count(input) == 1);

    /* Ctrl+J in multiline mode inserts newline (Enter submits) */
    tui_textinput_update(input, tui_msg_key(TUI_KEY_NONE, 'j', TUI_MOD_CTRL));
    send_string(input, "line2");
    assert(tui_textinput_line_count(input) == 2);

    tui_textinput_free(input);
}

static void test_prompt(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_prompt(input, "$ ");
    tui_textinput_set_show_prompt(input, 1);

    send_string(input, "cmd");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);

    assert(strstr(dynamic_buffer_data(buf), "$ ") != NULL);
    assert(strstr(dynamic_buffer_data(buf), "cmd") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

static void test_get_height_no_dividers(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    assert(tui_textinput_get_height(input) == 1);
    tui_textinput_free(input);
}

static void test_get_height_with_dividers(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_show_dividers(input, 1);
    assert(tui_textinput_get_height(input) == 3);
    tui_textinput_free(input);
}

static void test_get_height_dynamic(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    assert(tui_textinput_get_height(input) == 1);
    tui_textinput_set_show_dividers(input, 1);
    assert(tui_textinput_get_height(input) == 3);
    tui_textinput_set_show_dividers(input, 0);
    assert(tui_textinput_get_height(input) == 1);
    tui_textinput_free(input);
}

/* ---------- selection / clipboard tests ---------- */

/* Run an update through the component interface and return the result so
 * the test can inspect (and free) any returned cmd. */
static TuiUpdateResult run_update(TuiTextInput *input, TuiMsg msg)
{
    const TuiComponent *c = tui_textinput_component();
    return c->update((TuiModel *)input, msg);
}

static void test_ctrl_space_toggles_mark(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello");

    /* C-SPC sets mark at cursor (cursor is at byte 5 after "hello"). */
    TuiUpdateResult r = run_update(
        input, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    assert(input->has_mark == 1);
    assert(input->mark_byte == 5);
    assert(r.cmd == NULL);

    /* Second C-SPC clears the mark. */
    r = run_update(input, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    assert(input->has_mark == 0);
    assert(r.cmd == NULL);

    tui_textinput_free(input);
}

static void test_m_w_no_mark_copies_whole_input(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello");

    TuiUpdateResult r = run_update(
        input, tui_msg_key(TUI_KEY_NONE, 'w', TUI_MOD_ALT));
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(r.cmd->payload.clipboard.len == 5);
    assert(memcmp(r.cmd->payload.clipboard.text, "hello", 5) == 0);
    tui_cmd_free(r.cmd);

    tui_textinput_free(input);
}

static void test_m_w_with_mark_copies_region(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello world");

    /* Place cursor at byte 5, set mark at 0, then move cursor back to 5
     * (already there). Use the public set_cursor and direct mark assignment. */
    tui_textinput_set_cursor(input, 0);
    /* Set mark at byte 0 via C-SPC */
    run_update(input, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    assert(input->has_mark == 1);
    assert(input->mark_byte == 0);

    /* Move cursor right by 5 chars via C-F */
    for (int i = 0; i < 5; i++)
        run_update(input, tui_msg_key(TUI_KEY_NONE, 'f', TUI_MOD_CTRL));
    assert(input->cursor_byte == 5);
    assert(input->has_mark == 1); /* motion preserves mark */

    /* M-w copies "hello" and clears mark */
    TuiUpdateResult r = run_update(
        input, tui_msg_key(TUI_KEY_NONE, 'w', TUI_MOD_ALT));
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(r.cmd->payload.clipboard.len == 5);
    assert(memcmp(r.cmd->payload.clipboard.text, "hello", 5) == 0);
    assert(input->has_mark == 0);
    tui_cmd_free(r.cmd);

    tui_textinput_free(input);
}

static void test_ctrl_w_kills_region_when_mark_set(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "abcdef");

    tui_textinput_set_cursor(input, 1);
    run_update(input, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    /* Mark at 1; move cursor to 4 */
    for (int i = 0; i < 3; i++)
        run_update(input, tui_msg_key(TUI_KEY_NONE, 'f', TUI_MOD_CTRL));
    assert(input->cursor_byte == 4);

    /* C-W with mark: kills region [1,4) = "bcd" */
    TuiUpdateResult r = run_update(
        input, tui_msg_key(TUI_KEY_NONE, 'w', TUI_MOD_CTRL));
    assert(strcmp(tui_textinput_text(input), "aef") == 0);
    assert(input->has_mark == 0);
    /* kill-ring filled */
    assert(input->kill_buf_len == 3);
    assert(memcmp(input->kill_buf, "bcd", 3) == 0);
    /* clipboard cmd emitted */
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(r.cmd->payload.clipboard.len == 3);
    assert(memcmp(r.cmd->payload.clipboard.text, "bcd", 3) == 0);
    tui_cmd_free(r.cmd);

    tui_textinput_free(input);
}

static void test_ctrl_k_emits_clipboard_cmd(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello");
    tui_textinput_set_cursor(input, 0);

    TuiUpdateResult r = run_update(
        input, tui_msg_key(TUI_KEY_NONE, 'k', TUI_MOD_CTRL));
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(r.cmd->payload.clipboard.len == 5);
    assert(memcmp(r.cmd->payload.clipboard.text, "hello", 5) == 0);
    tui_cmd_free(r.cmd);

    /* Buffer is now empty */
    assert(tui_textinput_len(input) == 0);

    tui_textinput_free(input);
}

static void test_ctrl_u_emits_clipboard_cmd(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello");
    /* Cursor at end. C-U kills back to start of line. */

    TuiUpdateResult r = run_update(
        input, tui_msg_key(TUI_KEY_NONE, 'u', TUI_MOD_CTRL));
    assert(r.cmd != NULL);
    assert(r.cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(r.cmd->payload.clipboard.len == 5);
    assert(memcmp(r.cmd->payload.clipboard.text, "hello", 5) == 0);
    tui_cmd_free(r.cmd);

    assert(tui_textinput_len(input) == 0);

    tui_textinput_free(input);
}

static void test_edit_clears_mark(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "abc");
    run_update(input, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    assert(input->has_mark == 1);

    /* Insert a character — mark should clear. */
    send_char(input, 'x');
    assert(input->has_mark == 0);

    tui_textinput_free(input);
}

static void test_escape_clears_mark(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "abc");
    run_update(input, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    assert(input->has_mark == 1);

    run_update(input, tui_msg_key(TUI_KEY_ESCAPE, 0, 0));
    assert(input->has_mark == 0);

    tui_textinput_free(input);
}

static void test_view_renders_selection_with_reverse(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello");
    tui_textinput_set_cursor(input, 0);
    run_update(input, tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL));
    /* Move cursor to byte 3 → selection [0,3) = "hel" */
    for (int i = 0; i < 3; i++)
        run_update(input, tui_msg_key(TUI_KEY_NONE, 'f', TUI_MOD_CTRL));

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);
    const char *out = dynamic_buffer_data(buf);
    /* SGR_REVERSE is "\033[7m" */
    assert(strstr(out, "\033[7m") != NULL);
    dynamic_buffer_destroy(buf);

    tui_textinput_free(input);
}

/* ---------- horizontal scroll / overflow ---------- */

static void test_overflow_single_line(void)
{
    TuiTextInputConfig cfg = { 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_show_prompt(input, 0);
    tui_textinput_set_terminal_width(input, 20);

    for (int i = 0; i < 30; i++)
        send_char(input, 'a' + (i % 26));

    /* Window must have shifted right and stay within content_width */
    assert(input->offset > 0);
    assert(input->offset_right - input->offset <= 20);
    assert(input->offset_right >= 30); /* cursor visible at right edge */

    tui_textinput_free(input);
}

static void test_overflow_multiline_cursor_line(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_show_prompt(input, 0);
    tui_textinput_set_terminal_width(input, 20);

    /* Short first line, then a long second line. In multiline mode
     * plain Enter submits (clears the buffer); Shift+Enter inserts
     * a literal newline — see src/components/textinput.c:886. */
    send_string(input, "short");
    TuiUpdateResult r = tui_textinput_update(
        input, tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_SHIFT));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    for (int i = 0; i < 40; i++)
        send_char(input, 'a' + (i % 26));

    /* Offsets reflect cursor's logical line (the long second line),
     * not a global codepoint index across the whole buffer. */
    assert(input->cursor_row == 1);
    assert(input->offset > 0);
    assert(input->offset_right - input->offset <= 20);
    /* offset_right is in line-local codepoint space and includes the
     * cursor sentinel (one past the last character). For a 40-char
     * second line that's 41 — see the single-line case at line 677
     * (30 chars → offset_right == 31). If offsets were global it
     * would land in the 40s+ once you count "short\n" prefix bytes. */
    assert(input->offset_right <= 41);

    tui_textinput_free(input);
}

static void test_overflow_resize_recomputes(void)
{
    TuiTextInputConfig cfg = { 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_show_prompt(input, 0);
    tui_textinput_set_terminal_width(input, 80);

    for (int i = 0; i < 50; i++)
        send_char(input, 'a' + (i % 26));

    /* All 50 chars fit within 80 cols → no scroll yet */
    assert(input->offset == 0);

    /* Resize narrower without further input — offsets must update */
    tui_textinput_set_terminal_width(input, 20);
    assert(input->offset > 0);
    assert(input->offset_right - input->offset <= 20);

    tui_textinput_free(input);
}

static void test_overflow_cursor_left_scrolls_back(void)
{
    TuiTextInputConfig cfg = { 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_show_prompt(input, 0);
    tui_textinput_set_terminal_width(input, 20);

    for (int i = 0; i < 40; i++)
        send_char(input, 'a' + (i % 26));
    assert(input->offset > 0);

    /* Move cursor to start of line — offset must reset to 0 */
    run_update(input, tui_msg_key(TUI_KEY_NONE, 'a', TUI_MOD_CTRL));
    assert(input->offset == 0);

    tui_textinput_free(input);
}

/* ---------- cursor() tests ---------- */

static void test_cursor_pos_focused_absolute(void)
{
    TuiTextInputConfig cfg = { .prompt = "> ", .multiline = 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_terminal_width(input, 80);
    tui_textinput_set_terminal_row(input, 5);

    send_string(input, "abc");

    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.row == 5);
    /* prompt_len(2) + cursor_cp(3) + 1 (1-indexed) = 6 */
    assert(c.col == 6);

    tui_textinput_free(input);
}

static void test_cursor_pos_blurred(void)
{
    TuiTextInputConfig cfg = { .prompt = "> ", .multiline = 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_width(input, 80);
    tui_textinput_set_terminal_row(input, 5);
    tui_textinput_set_focus(input, 0);

    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 0);

    tui_textinput_free(input);
}

static void test_cursor_pos_relative_abstains(void)
{
    /* No terminal_row set → relative mode → abstain */
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello");

    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 0);

    tui_textinput_free(input);
}

static void test_cursor_pos_multiline(void)
{
    TuiTextInputConfig cfg = { .prompt = ">>> ", .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_terminal_width(input, 80);
    tui_textinput_set_terminal_row(input, 10);

    /* Build "ab\ncd" with cursor at end (row=1, col=2). */
    send_char(input, 'a');
    send_char(input, 'b');
    tui_textinput_update(input,
                         tui_msg_key(TUI_KEY_NONE, '\n', 0));
    send_char(input, 'c');
    send_char(input, 'd');

    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    /* row: terminal_row(10) + cursor_row(1) = 11
     * col: cursor_col(2) - offset(0) + prompt_len(4) + 1 = 7 */
    assert(c.row == 11);
    assert(c.col == 7);

    tui_textinput_free(input);
}

static void test_view_no_longer_emits_trailing_cup(void)
{
    /* With Bubbletea v2 alignment, view() must not emit cursor positioning.
     * Single-line absolute mode used to emit "CSI <row>;<col>H" after the
     * prompt; verify it no longer appears. */
    TuiTextInputConfig cfg = { .prompt = "> ", .multiline = 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_terminal_width(input, 80);
    tui_textinput_set_terminal_row(input, 7);
    send_string(input, "hi");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);

    const char *data = dynamic_buffer_data(buf);
    /* The single CUP we expect is the row-positioning at start ("\x1b[7;1H"),
     * not a cursor-placement at the prompt. There must be exactly one ";1H"
     * (the line start) — cursor_visual_col would have been ";4H". */
    int found_row_start = strstr(data, "\x1b[7;1H") != NULL;
    int found_cursor_at_4 = strstr(data, "\x1b[7;4H") != NULL;
    assert(found_row_start == 1);
    assert(found_cursor_at_4 == 0);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* ---------- main ---------- */

int main(void)
{
    printf("textinput tests:\n");

    RUN_TEST(test_create_and_free);
    RUN_TEST(test_create_with_config);
    RUN_TEST(test_char_insertion);
    RUN_TEST(test_backspace);
    RUN_TEST(test_backspace_at_start);
    RUN_TEST(test_delete_key);
    RUN_TEST(test_cursor_left_right);
    RUN_TEST(test_cursor_left_at_start);
    RUN_TEST(test_cursor_right_at_end);
    RUN_TEST(test_home_end);
    RUN_TEST(test_insert_in_middle);
    RUN_TEST(test_set_text);
    RUN_TEST(test_clear);
    RUN_TEST(test_focus);
    RUN_TEST(test_unfocused_ignores_input);
    RUN_TEST(test_history_navigation);
    RUN_TEST(test_tab_emits_command);
    RUN_TEST(test_tab_word_start);
    RUN_TEST(test_tab_with_shift_is_noop);
    RUN_TEST(test_insert_completion);
    RUN_TEST(test_view_output);
    RUN_TEST(test_set_cursor);
    RUN_TEST(test_line_count);
    RUN_TEST(test_prompt);
    RUN_TEST(test_get_height_no_dividers);
    RUN_TEST(test_get_height_with_dividers);
    RUN_TEST(test_get_height_dynamic);

    RUN_TEST(test_ctrl_space_toggles_mark);
    RUN_TEST(test_m_w_no_mark_copies_whole_input);
    RUN_TEST(test_m_w_with_mark_copies_region);
    RUN_TEST(test_ctrl_w_kills_region_when_mark_set);
    RUN_TEST(test_ctrl_k_emits_clipboard_cmd);
    RUN_TEST(test_ctrl_u_emits_clipboard_cmd);
    RUN_TEST(test_edit_clears_mark);
    RUN_TEST(test_escape_clears_mark);
    RUN_TEST(test_view_renders_selection_with_reverse);

    RUN_TEST(test_overflow_single_line);
    RUN_TEST(test_overflow_multiline_cursor_line);
    RUN_TEST(test_overflow_resize_recomputes);
    RUN_TEST(test_overflow_cursor_left_scrolls_back);

    RUN_TEST(test_cursor_pos_focused_absolute);
    RUN_TEST(test_cursor_pos_blurred);
    RUN_TEST(test_cursor_pos_relative_abstains);
    RUN_TEST(test_cursor_pos_multiline);
    RUN_TEST(test_view_no_longer_emits_trailing_cup);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
