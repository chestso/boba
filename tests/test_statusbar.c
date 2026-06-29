/* test_statusbar.c - Unit tests for the statusbar component */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <boba/components/statusbar.h>
#include <boba/dynamic_buffer.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

static void test_create_and_free(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    assert(sb != NULL);
    tui_statusbar_free(sb);
}

static void test_get_height_default(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    assert(tui_statusbar_get_height(sb) == 1);
    tui_statusbar_free(sb);
}

static void test_set_mode(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_mode(sb, "NORMAL");
    assert(strcmp(tui_statusbar_get_mode(sb), "NORMAL") == 0);
    tui_statusbar_free(sb);
}

static void test_set_mode_empty(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_mode(sb, "TEST");
    tui_statusbar_set_mode(sb, "");
    assert(tui_statusbar_get_mode(sb) == NULL);
    tui_statusbar_free(sb);
}

static void test_set_mode_null(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_mode(sb, "TEST");
    tui_statusbar_set_mode(sb, NULL);
    assert(tui_statusbar_get_mode(sb) == NULL);
    tui_statusbar_free(sb);
}

static void test_set_notification(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_notification(sb, "Connected");
    assert(strcmp(tui_statusbar_get_notification(sb), "Connected") == 0);
    tui_statusbar_free(sb);
}

static void test_clear_notification(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_notification(sb, "Test");
    tui_statusbar_clear_notification(sb);
    assert(tui_statusbar_get_notification(sb) == NULL);
    tui_statusbar_free(sb);
}

static void test_view_mode_only(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_terminal_width(sb, 80);
    tui_statusbar_set_mode(sb, "INSERT");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_statusbar_view(sb, buf);
    assert(strstr(dynamic_buffer_data(buf), "INSERT") != NULL);

    dynamic_buffer_destroy(buf);
    tui_statusbar_free(sb);
}

static void test_view_notification_only(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_terminal_width(sb, 80);
    tui_statusbar_set_notification(sb, "Saved");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_statusbar_view(sb, buf);
    assert(strstr(dynamic_buffer_data(buf), "Saved") != NULL);

    dynamic_buffer_destroy(buf);
    tui_statusbar_free(sb);
}

static void test_view_mode_and_notification(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_terminal_width(sb, 80);
    tui_statusbar_set_mode(sb, "NORMAL");
    tui_statusbar_set_notification(sb, "Connected");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_statusbar_view(sb, buf);
    const char *output = dynamic_buffer_data(buf);
    assert(strstr(output, "NORMAL") != NULL);
    assert(strstr(output, "Connected") != NULL);

    dynamic_buffer_destroy(buf);
    tui_statusbar_free(sb);
}

static void test_view_with_absolute_position(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_terminal_width(sb, 80);
    tui_statusbar_set_terminal_row(sb, 24);
    tui_statusbar_set_mode(sb, "TEST");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_statusbar_view(sb, buf);
    /* Should contain CSI cursor positioning sequence */
    assert(strstr(dynamic_buffer_data(buf), "\033[24;1H") != NULL);

    dynamic_buffer_destroy(buf);
    tui_statusbar_free(sb);
}

static void test_mode_with_emoji(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_mode(sb, "\xf0\x9f\x9f\xa2"); /* Green circle emoji UTF-8 */
    assert(strcmp(tui_statusbar_get_mode(sb), "\xf0\x9f\x9f\xa2") == 0);
    tui_statusbar_free(sb);
}

static void test_mode_replace(void)
{
    TuiStatusBar *sb = tui_statusbar_create();
    tui_statusbar_set_mode(sb, "FIRST");
    assert(strcmp(tui_statusbar_get_mode(sb), "FIRST") == 0);
    tui_statusbar_set_mode(sb, "SECOND");
    assert(strcmp(tui_statusbar_get_mode(sb), "SECOND") == 0);
    tui_statusbar_free(sb);
}

static void test_component_interface(void)
{
    const TuiComponent *comp = tui_statusbar_component();
    assert(comp != NULL);
    assert(comp->init != NULL);
    assert(comp->update != NULL);
    assert(comp->view != NULL);
    assert(comp->free != NULL);
}

int main(void)
{
    printf("statusbar tests:\n");

    RUN_TEST(test_create_and_free);
    RUN_TEST(test_get_height_default);
    RUN_TEST(test_set_mode);
    RUN_TEST(test_set_mode_empty);
    RUN_TEST(test_set_mode_null);
    RUN_TEST(test_set_notification);
    RUN_TEST(test_clear_notification);
    RUN_TEST(test_view_mode_only);
    RUN_TEST(test_view_notification_only);
    RUN_TEST(test_view_mode_and_notification);
    RUN_TEST(test_view_with_absolute_position);
    RUN_TEST(test_mode_with_emoji);
    RUN_TEST(test_mode_replace);
    RUN_TEST(test_component_interface);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
