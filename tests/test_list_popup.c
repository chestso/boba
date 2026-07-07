/* test_list_popup.c - Unit tests for the list_popup component */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/components/list_popup.h>
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

/* ---------- helpers ---------- */

static const char *buf_data(DynamicBuffer *buf)
{
    return dynamic_buffer_data(buf);
}

/* ---------- create / free ---------- */

static void test_create_and_free(void)
{
    TuiListPopup *p = tui_list_popup_create();
    assert(p != NULL);
    assert(!tui_list_popup_is_visible(p));
    assert(tui_list_popup_selected_index(p) == -1);
    assert(tui_list_popup_selected_text(p) == NULL);
    assert(tui_list_popup_word_start(p) == -1);
    tui_list_popup_free(p);
}

static void test_free_null_safe(void)
{
    tui_list_popup_free(NULL);
}

/* ---------- set items ---------- */

static void test_set_items(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta", "gamma" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_show(p, 0);
    assert(tui_list_popup_selected_index(p) == 0);
    assert(strcmp(tui_list_popup_selected_text(p), "alpha") == 0);
    tui_list_popup_free(p);
}

static void test_set_items_empty(void)
{
    TuiListPopup *p = tui_list_popup_create();
    tui_list_popup_set_items(p, NULL, 0);
    tui_list_popup_show(p, 0);
    assert(tui_list_popup_selected_index(p) == -1);
    assert(tui_list_popup_selected_text(p) == NULL);
    tui_list_popup_free(p);
}

static void test_set_items_replaces_previous(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *first[] = { "one", "two" };
    tui_list_popup_set_items(p, first, 2);
    const char *second[] = { "aaa", "bbb", "ccc" };
    tui_list_popup_set_items(p, second, 3);
    tui_list_popup_show(p, 0);
    assert(strcmp(tui_list_popup_selected_text(p), "aaa") == 0);
    tui_list_popup_free(p);
}

static void test_clear(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "x", "y" };
    tui_list_popup_set_items(p, items, 2);
    tui_list_popup_clear(p);
    assert(tui_list_popup_selected_index(p) == -1);
    assert(tui_list_popup_selected_text(p) == NULL);
    tui_list_popup_free(p);
}

/* ---------- show / hide ---------- */

static void test_show_sets_visible(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a" };
    tui_list_popup_set_items(p, items, 1);
    assert(!tui_list_popup_is_visible(p));
    tui_list_popup_show(p, 5);
    assert(tui_list_popup_is_visible(p));
    assert(tui_list_popup_word_start(p) == 5);
    tui_list_popup_free(p);
}

static void test_hide_clears_visible(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a" };
    tui_list_popup_set_items(p, items, 1);
    tui_list_popup_show(p, 0);
    assert(tui_list_popup_is_visible(p));
    tui_list_popup_hide(p);
    assert(!tui_list_popup_is_visible(p));
    tui_list_popup_free(p);
}

static void test_show_resets_selection(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_show(p, 0);
    tui_list_popup_move_down(p);
    assert(tui_list_popup_selected_index(p) == 1);
    tui_list_popup_hide(p);
    tui_list_popup_show(p, 0);
    assert(tui_list_popup_selected_index(p) == 0);
    tui_list_popup_free(p);
}

/* ---------- navigation ---------- */

static void test_move_down(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_show(p, 0);
    assert(tui_list_popup_move_down(p));
    assert(tui_list_popup_selected_index(p) == 1);
    assert(strcmp(tui_list_popup_selected_text(p), "b") == 0);
    assert(tui_list_popup_move_down(p));
    assert(tui_list_popup_selected_index(p) == 2);
    /* Wrap to top */
    assert(tui_list_popup_move_down(p));
    assert(tui_list_popup_selected_index(p) == 0);
    tui_list_popup_free(p);
}

static void test_move_up(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_show(p, 0);
    /* At index 0, wrap to bottom */
    assert(tui_list_popup_move_up(p));
    assert(tui_list_popup_selected_index(p) == 2);
    assert(strcmp(tui_list_popup_selected_text(p), "c") == 0);
    assert(tui_list_popup_move_up(p));
    assert(tui_list_popup_selected_index(p) == 1);
    tui_list_popup_free(p);
}

static void test_move_down_single_item(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "only" };
    tui_list_popup_set_items(p, items, 1);
    tui_list_popup_show(p, 0);
    /* Move down wraps to same item, returns 0 (no change) */
    assert(!tui_list_popup_move_down(p));
    assert(tui_list_popup_selected_index(p) == 0);
    tui_list_popup_free(p);
}

static void test_move_up_single_item(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "only" };
    tui_list_popup_set_items(p, items, 1);
    tui_list_popup_show(p, 0);
    assert(!tui_list_popup_move_up(p));
    assert(tui_list_popup_selected_index(p) == 0);
    tui_list_popup_free(p);
}

static void test_move_top(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_show(p, 0);
    tui_list_popup_move_down(p);
    tui_list_popup_move_down(p);
    assert(tui_list_popup_selected_index(p) == 2);
    tui_list_popup_move_top(p);
    assert(tui_list_popup_selected_index(p) == 0);
    tui_list_popup_free(p);
}

static void test_move_bottom(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_show(p, 0);
    tui_list_popup_move_bottom(p);
    assert(tui_list_popup_selected_index(p) == 2);
    tui_list_popup_free(p);
}

static void test_move_page_down(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c", "d", "e", "f", "g" };
    tui_list_popup_set_items(p, items, 7);
    tui_list_popup_set_size(p, 40, 3);
    tui_list_popup_show(p, 0);
    assert(tui_list_popup_selected_index(p) == 0);
    tui_list_popup_move_page_down(p);
    assert(tui_list_popup_selected_index(p) == 3);
    tui_list_popup_move_page_down(p);
    assert(tui_list_popup_selected_index(p) == 6);
    tui_list_popup_free(p);
}

static void test_move_page_up(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c", "d", "e", "f", "g" };
    tui_list_popup_set_items(p, items, 7);
    tui_list_popup_set_size(p, 40, 3);
    tui_list_popup_show(p, 0);
    tui_list_popup_move_bottom(p);
    assert(tui_list_popup_selected_index(p) == 6);
    tui_list_popup_move_page_up(p);
    assert(tui_list_popup_selected_index(p) == 3);
    tui_list_popup_move_page_up(p);
    assert(tui_list_popup_selected_index(p) == 0);
    tui_list_popup_free(p);
}

static void test_move_on_empty_returns_zero(void)
{
    TuiListPopup *p = tui_list_popup_create();
    tui_list_popup_set_items(p, NULL, 0);
    tui_list_popup_show(p, 0);
    assert(!tui_list_popup_move_down(p));
    assert(!tui_list_popup_move_up(p));
    assert(!tui_list_popup_move_top(p));
    assert(!tui_list_popup_move_bottom(p));
    assert(!tui_list_popup_move_page_down(p));
    assert(!tui_list_popup_move_page_up(p));
    tui_list_popup_free(p);
}

/* ---------- config ---------- */

static void test_set_size(void)
{
    TuiListPopup *p = tui_list_popup_create();
    tui_list_popup_set_size(p, 60, 10);
    tui_list_popup_free(p);
}

static void test_set_terminal_size(void)
{
    TuiListPopup *p = tui_list_popup_create();
    tui_list_popup_set_terminal_size(p, 120, 40);
    tui_list_popup_free(p);
}

static void test_set_title(void)
{
    TuiListPopup *p = tui_list_popup_create();
    tui_list_popup_set_title(p, "completions");
    tui_list_popup_free(p);
}

static void test_set_filter(void)
{
    TuiListPopup *p = tui_list_popup_create();
    tui_list_popup_set_filter(p, "str");
    tui_list_popup_free(p);
}

/* ---------- rendering ---------- */

static void test_view_hidden_when_not_visible(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b" };
    tui_list_popup_set_items(p, items, 2);
    /* Not shown — view should produce nothing */
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    assert(dynamic_buffer_len(buf) == 0);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

static void test_view_visible_produces_content(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta" };
    tui_list_popup_set_items(p, items, 2);
    tui_list_popup_set_terminal_size(p, 80, 24);
    tui_list_popup_show(p, 0);
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    assert(dynamic_buffer_len(buf) > 0);
    /* Should contain border characters */
    assert(strstr(buf_data(buf), "alpha") != NULL);
    assert(strstr(buf_data(buf), "beta") != NULL);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

static void test_view_has_selected_marker(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta" };
    tui_list_popup_set_items(p, items, 2);
    tui_list_popup_set_terminal_size(p, 80, 24);
    tui_list_popup_show(p, 0);
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    const char *data = buf_data(buf);
    /* Selected item should be visually distinguishable */
    assert(strstr(data, "alpha") != NULL);
    /* The marker char should be present */
    assert(strchr(data, '>') != NULL || strstr(data, "\033[7m") != NULL);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

static void test_view_hidden_items_not_rendered(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c", "d", "e", "f" };
    tui_list_popup_set_items(p, items, 6);
    tui_list_popup_set_terminal_size(p, 80, 24);
    tui_list_popup_set_size(p, 40, 2); /* only 2 visible rows */
    tui_list_popup_show(p, 0);
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    const char *data = buf_data(buf);
    /* First two items visible, rest should not */
    assert(strstr(data, "a") != NULL || strstr(data, ">") != NULL);
    /* "e" as a standalone char might appear in borders, check item context */
    /* The third item "c" should NOT appear since max_visible=2 */
    assert(strstr(data, "c\n") == NULL && strstr(data, "c\r") == NULL);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

/* ---------- scrolling ---------- */

static void test_scroll_follows_selection_down(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c", "d", "e", "f" };
    tui_list_popup_set_items(p, items, 6);
    tui_list_popup_set_size(p, 40, 3); /* 3 visible rows */
    tui_list_popup_show(p, 0);
    /* Move to item 3 (0-indexed) — should scroll */
    tui_list_popup_move_down(p); /* → 1 */
    tui_list_popup_move_down(p); /* → 2 */
    tui_list_popup_move_down(p); /* → 3, scroll_offset should advance */
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    const char *data = buf_data(buf);
    /* After scrolling, "d" should be visible (item 3) */
    assert(strstr(data, "d") != NULL);
    /* "a" should no longer be visible (scrolled out) */
    assert(strstr(data, "a\r") == NULL && strstr(data, "a\n") == NULL);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

static void test_scroll_follows_selection_up(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b", "c", "d", "e", "f" };
    tui_list_popup_set_items(p, items, 6);
    tui_list_popup_set_size(p, 40, 3);
    tui_list_popup_show(p, 0);
    /* Go to bottom, then back up past top of visible area */
    tui_list_popup_move_bottom(p);  /* → 5 */
    tui_list_popup_move_page_up(p); /* → 2 */
    tui_list_popup_move_up(p);      /* → 1 */
    tui_list_popup_move_up(p);      /* → 0, scroll back to top */
    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    const char *data = buf_data(buf);
    assert(strstr(data, "a") != NULL);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

/* ======================================================================== */

int main(void)
{
    printf("list_popup tests:\n");

    /* create / free */
    RUN_TEST(test_create_and_free);
    RUN_TEST(test_free_null_safe);

    /* set items */
    RUN_TEST(test_set_items);
    RUN_TEST(test_set_items_empty);
    RUN_TEST(test_set_items_replaces_previous);
    RUN_TEST(test_clear);

    /* show / hide */
    RUN_TEST(test_show_sets_visible);
    RUN_TEST(test_hide_clears_visible);
    RUN_TEST(test_show_resets_selection);

    /* navigation */
    RUN_TEST(test_move_down);
    RUN_TEST(test_move_up);
    RUN_TEST(test_move_down_single_item);
    RUN_TEST(test_move_up_single_item);
    RUN_TEST(test_move_top);
    RUN_TEST(test_move_bottom);
    RUN_TEST(test_move_page_down);
    RUN_TEST(test_move_page_up);
    RUN_TEST(test_move_on_empty_returns_zero);

    /* config */
    RUN_TEST(test_set_size);
    RUN_TEST(test_set_terminal_size);
    RUN_TEST(test_set_title);
    RUN_TEST(test_set_filter);

    /* rendering */
    RUN_TEST(test_view_hidden_when_not_visible);
    RUN_TEST(test_view_visible_produces_content);
    RUN_TEST(test_view_has_selected_marker);
    RUN_TEST(test_view_hidden_items_not_rendered);
    RUN_TEST(test_scroll_follows_selection_down);
    RUN_TEST(test_scroll_follows_selection_up);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
