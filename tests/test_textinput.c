/* test_textinput.c - Unit tests for the textinput component */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/cmd.h>
#include <boba/components/textinput.h>
#include <boba/dynamic_buffer.h>
#include <boba/msg.h>

/* Tests deliberately exercise the legacy imperative APIs (set_focus,
 * set_prompt_color) to verify backward-compat behavior. */
_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")

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

static void test_release_event_ignored(void)
{
    /* Release events (only emitted under Kitty kbd protocol) must not
     * insert text, even though the message type is TUI_MSG_KEY_PRESS. */
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    tui_textinput_update(input, tui_msg_key_release(TUI_KEY_NONE, 'a', 0));
    assert(strcmp(tui_textinput_text(input), "") == 0);

    /* Press still inserts. */
    send_char(input, 'a');
    assert(strcmp(tui_textinput_text(input), "a") == 0);
    tui_textinput_free(input);
}

/* Render the textinput view to a buffer and return its data as a malloc'd
 * copy (caller frees). */
static char *render_view(TuiTextInput *input)
{
    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);
    size_t n = dynamic_buffer_len(buf);
    char *out = malloc(n + 1);
    memcpy(out, dynamic_buffer_data(buf), n);
    out[n] = '\0';
    dynamic_buffer_destroy(buf);
    return out;
}

static void test_focused_blurred_styles_differ(void)
{
    TuiTextInputConfig cfg = { .prompt = "> ", .multiline = 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_terminal_width(input, 20);

    /* Bold focused, faint blurred. */
    tui_textinput_set_focused_prompt_style(
        input, tui_style_bold(tui_style_new(), 1));
    tui_textinput_set_blurred_prompt_style(
        input, tui_style_faint(tui_style_new(), 1));

    tui_textinput_set_focus(input, 1);
    char *focused = render_view(input);
    assert(strstr(focused, "\033[1m") != NULL); /* bold open */
    free(focused);

    tui_textinput_set_focus(input, 0);
    char *blurred = render_view(input);
    assert(strstr(blurred, "\033[2m") != NULL); /* faint open */
    free(blurred);

    tui_textinput_free(input);
}

static void test_prompt_style_applies_in_multiline_mode(void)
{
    /* Regression: the multiline render paths read prompt_color directly
     * and ignored focused/blurred_prompt_style (nevermore's chat is a
     * multiline input, so its colored prompt had no effect). */
    TuiTextInputConfig cfg = { .prompt = "> ", .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_terminal_width(input, 20);
    tui_textinput_set_focused_prompt_style(
        input, tui_style_bold(tui_style_new(), 1));
    tui_textinput_set_focus(input, 1);

    char *out = render_view(input);
    assert(strstr(out, "\033[1m> ") != NULL); /* bold prompt */
    free(out);

    /* legacy color is honored when no style is set */
    tui_textinput_set_focused_prompt_style(input, tui_style_new());
    tui_textinput_set_prompt_color(input, "\033[35m");
    out = render_view(input);
    assert(strstr(out, "\033[35m> ") != NULL);
    free(out);
    tui_textinput_free(input);
}

static void test_legacy_color_still_works(void)
{
    /* When no TuiStyle is set, the legacy raw-ANSI setter still applies. */
    TuiTextInputConfig cfg = { .prompt = "> ", .multiline = 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_terminal_width(input, 20);
    tui_textinput_set_focus(input, 1);

    tui_textinput_set_prompt_color(input, "\033[35m");
    char *out = render_view(input);
    assert(strstr(out, "\033[35m") != NULL);
    free(out);
    tui_textinput_free(input);
}

static void test_style_overrides_legacy(void)
{
    /* Setting both legacy color and TuiStyle: TuiStyle wins. */
    TuiTextInputConfig cfg = { .prompt = "> ", .multiline = 0 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_terminal_width(input, 20);
    tui_textinput_set_focus(input, 1);

    tui_textinput_set_prompt_color(input, "\033[35m"); /* legacy */
    tui_textinput_set_focused_prompt_style(
        input, tui_style_bold(tui_style_new(), 1));

    char *out = render_view(input);
    /* bold from style is present */
    assert(strstr(out, "\033[1m") != NULL);
    /* legacy magenta is NOT */
    assert(strstr(out, "\033[35m") == NULL);
    free(out);
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
     * view is verified by test_tab_with_shift_is_noop below. */
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_key(TUI_KEY_TAB, 0, TUI_MOD_SHIFT));
    assert(r.cmd == NULL);
    assert(tui_textinput_cursor(input) == cursor_before);
    assert(strcmp(tui_textinput_text(input), "foo") == 0);

    tui_textinput_free(input);
}

/* ----- word_at_cursor tests ----- */

static void test_word_at_cursor_simple(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_word_chars(input,
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-*!?");

    send_string(input, "hello");
    int word_start = -1;
    char *word = tui_textinput_word_at_cursor(input, &word_start);
    assert(word != NULL);
    assert(strcmp(word, "hello") == 0);
    assert(word_start == 0);
    free(word);

    tui_textinput_free(input);
}

static void test_word_at_cursor_mid_word(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_word_chars(input,
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-*!?");

    send_string(input, "hello");
    tui_textinput_set_cursor(input, 2); /* cursor between 'e' and 'l' */

    int word_start = -1;
    char *word = tui_textinput_word_at_cursor(input, &word_start);
    assert(word != NULL);
    /* Returns prefix from word_start to cursor, not the full word */
    assert(strcmp(word, "he") == 0);
    assert(word_start == 0);
    free(word);

    tui_textinput_free(input);
}

static void test_word_at_cursor_after_space(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_word_chars(input,
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-*!?");

    send_string(input, "foo bar");

    /* Cursor at end, word is "bar" */
    int word_start = -1;
    char *word = tui_textinput_word_at_cursor(input, &word_start);
    assert(word != NULL);
    assert(strcmp(word, "bar") == 0);
    assert(word_start == 4);
    free(word);

    tui_textinput_free(input);
}

static void test_word_at_cursor_empty_input(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_word_chars(input,
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-*!?");

    int word_start = -1;
    char *word = tui_textinput_word_at_cursor(input, &word_start);
    assert(word == NULL);
    assert(word_start == -1);

    tui_textinput_free(input);
}

static void test_word_at_cursor_not_in_word(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_word_chars(input,
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-*!?");

    send_string(input, "foo  "); /* two spaces after word */
    /* cursor is on a space, not in a word */
    int word_start = -1;
    char *word = tui_textinput_word_at_cursor(input, &word_start);
    assert(word == NULL);
    assert(word_start == -1);

    tui_textinput_free(input);
}

static void test_word_at_cursor_null_safe(void)
{
    int word_start = -1;
    char *word = tui_textinput_word_at_cursor(NULL, &word_start);
    assert(word == NULL);
    assert(word_start == -1);
}

static void test_word_at_cursor_after_paren(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_word_chars(input,
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-*!?");

    send_string(input, "(str");
    int word_start = -1;
    char *word = tui_textinput_word_at_cursor(input, &word_start);
    assert(word != NULL);
    assert(strcmp(word, "str") == 0);
    assert(word_start == 1);
    free(word);

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

static void test_get_height_single_line(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    assert(tui_textinput_get_height(input) == 1);
    tui_textinput_free(input);
}

static void test_get_height_multiline(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    assert(tui_textinput_get_height(input) == 1);
    /* Insert a newline (Ctrl+J in multiline) — height grows to 2. */
    tui_textinput_update(input, tui_msg_key(TUI_KEY_NONE, 'j', TUI_MOD_CTRL));
    assert(tui_textinput_get_height(input) == 2);
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
    /* No terminal_row set → inline mode → cursor is visible with
     * row 1 as relative reference. The runtime converts this to
     * relative cursor movement in inline flush. */
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    send_string(input, "hello");

    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.row == 1); /* relative reference row */
    /* col = 0 prompt + 5 chars + 1 (1-indexed) = 6 */
    assert(c.col == 6);

    tui_textinput_free(input);
}

static void test_cursor_pos_multiline(void)
{
    TuiTextInputConfig cfg = { .prompt = ">>> ", .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_terminal_width(input, 80);
    tui_textinput_set_terminal_row(input, 10);

    /* Build "ab\ncd" with cursor at end (row=1, col=2). Newline insertion
     * in multiline mode goes through Ctrl+J, the same path used when the
     * parser sees a literal 0x0A byte from the terminal. */
    send_char(input, 'a');
    send_char(input, 'b');
    tui_textinput_update(input,
                         tui_msg_key(TUI_KEY_NONE, 'j', TUI_MOD_CTRL));
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

/* --- Styled-text highlight callback tests --- */

static char *mock_highlight(const char *text, size_t len, void *ud)
{
    (void)ud;
    char *out = malloc(len + 9);
    memcpy(out, "\033[1m", 4);
    memcpy(out + 4, text, len);
    memcpy(out + 4 + len, "\033[0m", 4);
    out[8 + len] = '\0';
    return out;
}

static void test_textinput_text_renderer_called(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_text_renderer(input, mock_highlight, NULL);
    send_string(input, "hello");
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *rendered = dynamic_buffer_data(buf);
    assert(strstr(rendered, "\033[1m") != NULL);
    assert(strstr(rendered, "hello") != NULL);
    assert(strstr(rendered, "\033[0m") != NULL);
    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

static void test_textinput_no_renderer_plain_text(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    send_string(input, "hello");
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *rendered = dynamic_buffer_data(buf);
    assert(strstr(rendered, "hello") != NULL);
    /* No SGR color/bold sequences in the text (only erase/cursor ok) */
    assert(strstr(rendered, "\033[1m") == NULL);
    assert(strstr(rendered, "\033[0m") == NULL);
    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* --- Inline cursor positioning tests --- */

static void test_textinput_inline_cursor_visible(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_terminal_row(input, 5);
    send_string(input, "hello");
    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.row >= 5);
    tui_textinput_free(input);
}

static void test_textinput_inline_cursor_column(void)
{
    TuiTextInputConfig cfg = { .prompt = ">>> " };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_row(input, 1);
    send_string(input, "hi");
    /* cursor after "hi" = col 7 (4 prompt + 2 text + 1 for 1-indexed) */
    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.col == 7);
    tui_textinput_free(input);
}

/* --- Newline insertion tests --- */

/* Ctrl+D on empty input is a no-op (textinput never decides to quit) */
static void test_ctrl_d_empty_is_noop(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    TuiUpdateResult r = tui_textinput_update(input,
                                             tui_msg_key(TUI_KEY_NONE, 'd', TUI_MOD_CTRL));
    assert(r.cmd == NULL);
    assert(tui_textinput_len(input) == 0);
    tui_textinput_free(input);
}

/* Ctrl+D on non-empty input deletes char under cursor */
static void test_ctrl_d_nonempty_deletes_char(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    send_string(input, "hello");
    /* Move cursor to middle (between 'l' and 'l') */
    tui_textinput_set_cursor(input, 3);
    TuiUpdateResult r = tui_textinput_update(input,
                                             tui_msg_key(TUI_KEY_NONE, 'd', TUI_MOD_CTRL));
    assert(r.cmd == NULL);
    /* "hello" with cursor at 3 → delete 'l' at pos 3 → "helo" */
    assert(strcmp(tui_textinput_text(input), "helo") == 0);
    tui_textinput_free(input);
}

/* Ctrl+D on empty multiline input is also a no-op (no quit) */
static void test_ctrl_d_empty_multiline_is_noop(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    TuiUpdateResult r = tui_textinput_update(input,
                                             tui_msg_key(TUI_KEY_NONE, 'd', TUI_MOD_CTRL));
    assert(r.cmd == NULL);
    tui_textinput_free(input);
}

/* tui_msg_char('\n') sends a char message with rune 0x0A which is < 0x20,
 * so the textinput should NOT insert it — control characters are ignored
 * except Tab. Use TUI_KEY_ENTER + TUI_MOD_SHIFT for newline insertion. */
static void test_msg_char_newline_is_noop(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    send_string(input, "hello");
    /* Try to insert \n via char message — should be a no-op */
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_char('\n', 0));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    /* Text should still be "hello" with no newline */
    assert(strcmp(tui_textinput_text(input), "hello") == 0);
    assert(tui_textinput_line_count(input) == 1);
    tui_textinput_free(input);
}

/* Shift+Enter in multiline mode inserts a real newline */
static void test_shift_enter_inserts_newline(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    send_string(input, "hello");
    TuiUpdateResult r = tui_textinput_update(input,
                                             tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_SHIFT));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    send_string(input, "world");
    /* Text should be "hello\nworld" */
    assert(strcmp(tui_textinput_text(input), "hello\nworld") == 0);
    assert(tui_textinput_line_count(input) == 2);
    tui_textinput_free(input);
}

/* --- Multi-line relative rendering prefix tests --- */

/* In relative mode (terminal_row == 0), view() must emit \r + EL_TO_END
 * at the start so each frame replaces the previous content instead of
 * appending to it. Without this, stale text accumulates on the line. */
static void test_multiline_relative_emits_clear_prefix(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    send_string(input, "hello");
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);
    /* The first bytes should be \r followed by EL_TO_END (CSI K) */
    assert(data[0] == '\r');
    assert(strstr(data, "\033[K") != NULL);
    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* In relative mode with multi-line content, each line after the first
 * should also get \r\n + EL_TO_END so wrapped lines don't accumulate */
static void test_multiline_relative_clears_between_lines(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    send_string(input, "hello");
    TuiUpdateResult r = tui_textinput_update(input,
                                             tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_SHIFT));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    send_string(input, "world");
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);
    /* Should contain \r\n between lines, with EL_TO_END after */
    assert(strstr(data, "\r\n\033[K") != NULL);
    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* --- Soft wrapping tests --- */

static void test_soft_wrap_height(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghijklmno"); /* 15 chars, width 10 → 2 visual rows */
    assert(tui_textinput_get_height(input) == 2);
    tui_textinput_free(input);
}

static void test_soft_wrap_height_short(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "hello"); /* 5 chars fits in 10 → 1 row */
    assert(tui_textinput_get_height(input) == 1);
    tui_textinput_free(input);
}

static void test_soft_wrap_height_exact(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghij"); /* exactly 10 → 1 row */
    assert(tui_textinput_get_height(input) == 1);
    tui_textinput_free(input);
}

static void test_soft_wrap_height_multiline(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "hello");
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_SHIFT));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    send_string(input, "world!");
    assert(tui_textinput_get_height(input) == 2);
    tui_textinput_free(input);
}

static void test_soft_wrap_height_multiline_wrap(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghijklmno");
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_SHIFT));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    send_string(input, "xy");
    assert(tui_textinput_get_height(input) == 3);
    tui_textinput_free(input);
}

static void test_soft_wrap_cursor_row(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghijklmno"); /* 15 chars → 2 visual rows */
    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.row == 2);
    assert(c.col == 6);
    tui_textinput_free(input);
}

static void test_soft_wrap_disabled_default(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_terminal_width(input, 10);
    send_string(input, "abcdefghijklmno");
    assert(tui_textinput_get_height(input) == 1);
    tui_textinput_free(input);
}

/* In multiline absolute mode, soft-wrap must split a long logical line
 * across visual rows (continuation-indented) instead of emitting the whole
 * line on one row for the terminal to auto-wrap. Regression: previously the
 * absolute multiline path ignored soft_wrap entirely and rendered each
 * logical line on a single row, so content past the terminal width spilled
 * (and garbled the screen with autowrap). */
static void test_soft_wrap_absolute_splits_into_visual_rows(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 10); /* content width = 8 */
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghijklmno"); /* 15 chars -> 2 visual rows */

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);

    /* Two absolute rows, the second indented under the text column. */
    assert(strstr(data, "\x1b[1;1H") != NULL);
    assert(strstr(data, "\x1b[2;1H") != NULL);
    assert(strstr(data, "> abcdefgh") != NULL);
    assert(strstr(data, "  ijklmno") != NULL);

    /* Cursor lands on the second visual row, after the 7 wrapped chars:
     * prompt width (2) + 7 + 1 (1-indexed) = 10. */
    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.row == 2);
    assert(c.col == 10);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* Row count emitted by soft-wrapped absolute rendering must match the height
 * reported by tui_textinput_get_height(), and the cursor's row must fall
 * within that range — otherwise the parent under-reserves space. */
static void test_soft_wrap_absolute_rows_match_height(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 12); /* content width = 10 */
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_soft_wrap(input, 1);

    /* Two logical lines, both long enough to wrap. */
    send_string(input, "hello foo bar baz"); /* 18 chars, line 0 */
    TuiUpdateResult r = tui_textinput_update(
        input, tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_SHIFT));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    send_string(input, "second line"); /* 11 chars, line 1 */

    int height = tui_textinput_get_height(input);
    assert(height == 4); /* 2 + 2 visual rows */

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);

    /* Count absolute row placements: CSI <n>;1H */
    int rows = 0;
    for (const char *p = data; (p = strstr(p, "\x1b[")) != NULL; p++) {
        if (p[2] >= '1' && p[2] <= '9')
            rows++;
    }
    assert(rows == height);

    /* Cursor must fall on one of the rendered rows. */
    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.row >= 1 && c.row <= height);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* Single-line soft-wrap in absolute mode also spreads across visual rows. */
static void test_soft_wrap_absolute_single_line(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghijklmno");

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);

    assert(strstr(data, "\x1b[2;1H") != NULL);
    assert(strstr(data, "> abcdefgh") != NULL);
    assert(strstr(data, "  ijklmno") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* With soft_wrap OFF, multiline absolute rendering keeps one row per
 * logical line and horizontally scrolls (unchanged behavior). */
static void test_no_soft_wrap_absolute_stays_single_row(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_terminal_row(input, 1);
    send_string(input, "abcdefghijklmno");

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);

    assert(tui_textinput_get_height(input) == 1);
    assert(strstr(data, "\x1b[1;1H") != NULL);
    assert(strstr(data, "\x1b[2;1H") == NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* When a soft-wrapped line shrinks back to fewer visual rows, the next frame
 * must erase the leftover rows (the runtime does not clear stale alt-screen
 * rows). Regression: backspacing across a wrap boundary used to leave the
 * vacated row rendered. */
static void test_soft_wrap_absolute_erases_surplus_rows(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 10); /* content width = 8 */
    tui_textinput_set_terminal_row(input, 1);
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghijklmno"); /* 2 visual rows */

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf); /* frame 1: two rows */
    dynamic_buffer_clear(buf);

    for (int i = 0; i < 10; i++)
        send_key(input, TUI_KEY_BACKSPACE);
    tui_textinput_view(input, buf); /* frame 2: one row */
    const char *data = dynamic_buffer_data(buf);

    /* Row 2 must be cleared (positioned + EL_TO_END), even though the current
     * frame only has one row of content. */
    assert(strstr(data, "\x1b[2;1H\x1b[K") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* In RELATIVE mode (terminal_row unset — inline rendering) a soft-wrapped
 * logical line must be split into explicit visual rows separated by "\r\n"
 * + EL, with the continuation prompt indenting the wrapped rows. Regression:
 * relative mode emitted the whole line and let the terminal auto-wrap, so
 * the runtime (which counts frame rows by '\n') tracked fewer rows than were
 * painted and walked the cursor one row too far up on every frame. */
static void test_soft_wrap_relative_splits_into_rows(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 10); /* content width = 8 */
    tui_textinput_set_soft_wrap(input, 1);
    send_string(input, "abcdefghijklmno"); /* 15 chars → 2 visual rows */

    assert(tui_textinput_get_height(input) == 2);

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);

    /* Two explicit rows: prompt + 8 cells, then continuation + remainder. */
    assert(strstr(data, "\r\n\033[K") != NULL);
    assert(strstr(data, "> abcdefgh") != NULL);
    assert(strstr(data, "  ijklmno") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* The runtime derives the inline frame height from the count of '\n' bytes
 * in the rendered content. For soft-wrapped relative output that count (+1)
 * must equal tui_textinput_get_height(), or the tracked and painted row
 * counts diverge. */
static void test_soft_wrap_relative_rows_match_height(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 12); /* content width = 10 */
    tui_textinput_set_soft_wrap(input, 1);

    /* Two logical lines, both long enough to wrap. */
    send_string(input, "hello foo bar baz"); /* 18 chars → 2 rows */
    TuiUpdateResult r = tui_textinput_update(
        input, tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_SHIFT));
    if (r.cmd)
        tui_cmd_free(r.cmd);
    send_string(input, "second line"); /* 11 chars → 2 rows */

    int height = tui_textinput_get_height(input);
    assert(height == 4);

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);

    int newlines = 0;
    for (const char *p = data; *p; p++)
        if (*p == '\n')
            newlines++;
    /* Explicit row separators: visual rows = newlines + 1. */
    assert(newlines + 1 == height);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* A cursor at the end of a line whose length exactly fills the last visual
 * row belongs to that row's end, not a phantom next row. Reporting the
 * phantom row made the inline runtime move the cursor up one row too far
 * each frame — the walk-to-top-of-screen drift. */
static void test_soft_wrap_cursor_exact_wrap_boundary(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_prompt(input, "> ");
    tui_textinput_set_terminal_width(input, 10); /* content width = 8 */
    tui_textinput_set_soft_wrap(input, 1);

    /* Exactly one row of content: cursor stays on row 1, at the row's end. */
    send_string(input, "abcdefgh");
    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.row == 1);
    assert(c.col == 10);

    /* Exactly two rows: cursor on row 2, not a phantom row 3. */
    send_string(input, "ijklmnop");
    c = tui_textinput_cursor_pos(input);
    assert(c.row == 2);
    assert(c.col == 10);
    assert(tui_textinput_get_height(input) == 2);

    tui_textinput_free(input);
}

/* ---------- gutter tests ---------- */

/* The gutter renders LEFT of the prompt on the input row. */
static void test_gutter_renders_left_of_prompt(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_prompt(input, "> ");

    TuiSpan spans[1];
    spans[0].text = "ctx 12k/128k ";
    spans[0].len = 0;
    spans[0].style = tui_style_new();
    tui_textinput_set_gutter(input, spans, 1);

    send_string(input, "cmd");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);

    assert(strstr(data, "ctx 12k/128k > cmd") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* Clearing the gutter removes it. */
static void test_gutter_clear(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    TuiTextInputConfig cfg = { .prompt = "> " };
    (void)cfg;

    TuiSpan spans[1];
    spans[0].text = "ZZ";
    spans[0].len = 0;
    spans[0].style = tui_style_new();
    tui_textinput_set_gutter(input, spans, 1);
    tui_textinput_set_gutter(input, NULL, 0);

    send_string(input, "cmd");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);
    assert(strstr(data, "ZZ") == NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* The gutter's display width is folded into the wrap arithmetic. */
static void test_gutter_width_affects_wrap(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_prompt(input, "> "); /* 2 */
    tui_textinput_set_terminal_width(input, 10);
    tui_textinput_set_soft_wrap(input, 1);

    TuiSpan spans[1];
    spans[0].text = "AB"; /* 2 -> content width = 10 - 2 - 2 = 6 */
    spans[0].len = 0;
    spans[0].style = tui_style_new();
    tui_textinput_set_gutter(input, spans, 1);

    send_string(input, "abcdefghijklmno"); /* 15 chars -> ceil(15/6) = 3 rows */
    assert(tui_textinput_get_height(input) == 3);

    tui_textinput_free(input);
}

/* Cursor column includes the gutter's width. */
static void test_gutter_cursor_column_offset(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_prompt(input, "> ");

    TuiSpan spans[1];
    spans[0].text = "AB"; /* 2 */
    spans[0].len = 0;
    spans[0].style = tui_style_new();
    tui_textinput_set_gutter(input, spans, 1);

    send_string(input, "cmd"); /* cursor at 3 */
    TuiCursor c = tui_textinput_cursor_pos(input);
    assert(c.visible == 1);
    assert(c.col == 2 + 2 + 3 + 1); /* gutter + prompt + cursors + 1-index */

    tui_textinput_free(input);
}

/* Multiple spans, each with its own style, all render. */
static void test_gutter_multiple_styled_spans(void)
{
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    TuiSpan spans[2];
    spans[0].text = "spinner ";
    spans[0].len = 0;
    spans[0].style = tui_style_new();
    spans[1].text = "ctx";
    spans[1].len = 0;
    spans[1].style = tui_style_bold(tui_style_new(), 1);
    tui_textinput_set_gutter(input, spans, 2);

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);
    const char *data = dynamic_buffer_data(buf);
    assert(strstr(data, "spinner ") != NULL);
    assert(strstr(data, "ctx") != NULL);
    /* The second span is bold. */
    assert(strstr(data, "\033[1m") != NULL);

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
    RUN_TEST(test_release_event_ignored);
    RUN_TEST(test_focused_blurred_styles_differ);
    RUN_TEST(test_prompt_style_applies_in_multiline_mode);
    RUN_TEST(test_legacy_color_still_works);
    RUN_TEST(test_style_overrides_legacy);
    RUN_TEST(test_history_navigation);
    RUN_TEST(test_tab_emits_command);
    RUN_TEST(test_tab_word_start);
    RUN_TEST(test_tab_with_shift_is_noop);
    RUN_TEST(test_insert_completion);

    /* word_at_cursor */
    RUN_TEST(test_word_at_cursor_simple);
    RUN_TEST(test_word_at_cursor_mid_word);
    RUN_TEST(test_word_at_cursor_after_space);
    RUN_TEST(test_word_at_cursor_empty_input);
    RUN_TEST(test_word_at_cursor_not_in_word);
    RUN_TEST(test_word_at_cursor_null_safe);
    RUN_TEST(test_word_at_cursor_after_paren);

    RUN_TEST(test_view_output);
    RUN_TEST(test_set_cursor);
    RUN_TEST(test_line_count);
    RUN_TEST(test_prompt);
    RUN_TEST(test_get_height_single_line);
    RUN_TEST(test_get_height_multiline);

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

    RUN_TEST(test_textinput_text_renderer_called);
    RUN_TEST(test_textinput_no_renderer_plain_text);
    RUN_TEST(test_textinput_inline_cursor_visible);
    RUN_TEST(test_textinput_inline_cursor_column);
    RUN_TEST(test_msg_char_newline_is_noop);
    RUN_TEST(test_shift_enter_inserts_newline);
    RUN_TEST(test_multiline_relative_emits_clear_prefix);
    RUN_TEST(test_multiline_relative_clears_between_lines);
    RUN_TEST(test_ctrl_d_empty_is_noop);
    RUN_TEST(test_ctrl_d_nonempty_deletes_char);
    RUN_TEST(test_ctrl_d_empty_multiline_is_noop);
    RUN_TEST(test_soft_wrap_height);
    RUN_TEST(test_soft_wrap_height_short);
    RUN_TEST(test_soft_wrap_height_exact);
    RUN_TEST(test_soft_wrap_height_multiline);
    RUN_TEST(test_soft_wrap_height_multiline_wrap);
    RUN_TEST(test_soft_wrap_cursor_row);
    RUN_TEST(test_soft_wrap_disabled_default);
    RUN_TEST(test_soft_wrap_absolute_splits_into_visual_rows);
    RUN_TEST(test_soft_wrap_absolute_rows_match_height);
    RUN_TEST(test_soft_wrap_absolute_single_line);
    RUN_TEST(test_no_soft_wrap_absolute_stays_single_row);
    RUN_TEST(test_soft_wrap_absolute_erases_surplus_rows);
    RUN_TEST(test_soft_wrap_relative_splits_into_rows);
    RUN_TEST(test_soft_wrap_relative_rows_match_height);
    RUN_TEST(test_soft_wrap_cursor_exact_wrap_boundary);

    RUN_TEST(test_gutter_renders_left_of_prompt);
    RUN_TEST(test_gutter_clear);
    RUN_TEST(test_gutter_width_affects_wrap);
    RUN_TEST(test_gutter_cursor_column_offset);
    RUN_TEST(test_gutter_multiple_styled_spans);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
