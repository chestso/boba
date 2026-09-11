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

/* ---------- filter-as-view ---------- */

/* The filter is a VIEW over the full list: items stay in place
 * (selected_text indexes the true item, not a copy), the popup
 * renders/skips only matches, and selection runs over the filtered
 * view. Zero copies per keystroke — matching is scan-only. */

static void test_filter_matches_are_case_insensitive_substrings(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "Beta", "gamma", "alphabet" };
    tui_list_popup_set_items(p, items, 4);

    tui_list_popup_set_filter(p, "ALP");
    assert(tui_list_popup_filtered_count(p) == 2);
    assert(strcmp(tui_list_popup_filtered_text(p, 0), "alpha") == 0);
    assert(strcmp(tui_list_popup_filtered_text(p, 1), "alphabet") == 0);

    tui_list_popup_set_filter(p, "zzz");
    assert(tui_list_popup_filtered_count(p) == 0);

    tui_list_popup_set_filter(p, NULL);
    assert(tui_list_popup_filtered_count(p) == 4);
    tui_list_popup_free(p);
}

static void test_filter_no_filter_sees_all_items(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "a", "b" };
    tui_list_popup_set_items(p, items, 2);
    assert(tui_list_popup_filtered_count(p) == 2);
    assert(strcmp(tui_list_popup_filtered_text(p, 1), "b") == 0);
    tui_list_popup_free(p);
}

static void test_filter_selection_resets_to_first_match(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "apple", "banana", "grape" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_show(p, 0);
    tui_list_popup_move_bottom(p); /* selected = 2 (grape) */

    tui_list_popup_set_filter(p, "ap");
    /* matches: apple, grape — selection snaps to first match */
    assert(tui_list_popup_selected_text(p) != NULL);
    assert(strcmp(tui_list_popup_selected_text(p), "apple") == 0);
    assert(tui_list_popup_selected_index(p) == 0);

    /* Navigation cycles the filtered view (apple -> grape -> apple) */
    assert(tui_list_popup_move_down(p) == 1);
    assert(strcmp(tui_list_popup_selected_text(p), "grape") == 0);
    assert(tui_list_popup_move_down(p) == 1);
    assert(strcmp(tui_list_popup_selected_text(p), "apple") == 0);

    tui_list_popup_free(p);
}

static void test_filter_empty_filter_sees_all_items(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta" };
    tui_list_popup_set_items(p, items, 2);
    tui_list_popup_set_filter(p, "b");
    tui_list_popup_set_filter(p, "");
    assert(tui_list_popup_filtered_count(p) == 2);
    tui_list_popup_free(p);
}

static void test_filter_multi_token_is_and_semantics(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "qwen3-coder:latest", "Qwen Vision", "coder" };
    tui_list_popup_set_items(p, items, 3);

    /* Space-separated tokens, ALL must match (case-insensitive). */
    tui_list_popup_set_filter(p, "qwen co");
    assert(tui_list_popup_filtered_count(p) == 1);
    assert(strcmp(tui_list_popup_filtered_text(p, 0),
                  "qwen3-coder:latest") == 0);

    /* Tabs separate tokens too. */
    tui_list_popup_set_filter(p, "vision\tq");
    assert(tui_list_popup_filtered_count(p) == 1);
    assert(strcmp(tui_list_popup_filtered_text(p, 0), "Qwen Vision") == 0);

    tui_list_popup_free(p);
}

static void test_filter_set_items_clears_filter(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta" };
    tui_list_popup_set_items(p, items, 2);
    tui_list_popup_set_filter(p, "alp");
    const char *next[] = { "x", "y", "z" };
    tui_list_popup_set_items(p, next, 3);
    /* New list: the old filter must not silently hide items. */
    assert(tui_list_popup_filtered_count(p) == 3);
    tui_list_popup_free(p);
}

static void test_filter_view_renders_only_matches(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta", "alphabet" };
    tui_list_popup_set_items(p, items, 3);
    tui_list_popup_set_terminal_size(p, 80, 24);
    tui_list_popup_show(p, 0);
    tui_list_popup_set_filter(p, "ph"); /* alpha, alphabet */

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    const char *data = buf_data(buf);
    assert(strstr(data, "alpha") != NULL);
    assert(strstr(data, "alphabet") != NULL);
    assert(strstr(data, "beta") == NULL);
    /* Title carries the query and match count: models: "ph" (2/3) */
    assert(strstr(data, "\"ph\"") != NULL);
    assert(strstr(data, "(2/3)") != NULL);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

static void test_filter_view_empty_result_renders_nothing(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta" };
    tui_list_popup_set_items(p, items, 2);
    tui_list_popup_set_terminal_size(p, 80, 24);
    tui_list_popup_show(p, 0);
    tui_list_popup_set_filter(p, "zzz");

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    assert(dynamic_buffer_len(buf) == 0);
    dynamic_buffer_destroy(buf);
    tui_list_popup_free(p);
}

static void test_filter_no_match_selection_is_none(void)
{
    TuiListPopup *p = tui_list_popup_create();
    const char *items[] = { "alpha", "beta" };
    tui_list_popup_set_items(p, items, 2);
    tui_list_popup_show(p, 0);
    tui_list_popup_set_filter(p, "zzz");
    assert(tui_list_popup_selected_index(p) == -1);
    assert(tui_list_popup_selected_text(p) == NULL);

    /* Reopening the world (filter cleared) restores a selection. */
    tui_list_popup_set_filter(p, NULL);
    assert(tui_list_popup_selected_text(p) != NULL);
    tui_list_popup_free(p);
}

static void test_filter_500_items_round_trip(void)
{
    /* The picker's scale test: 500 items, filter, view, selection
     * round-trips — selection must always index the true item. */
    TuiListPopup *p = tui_list_popup_create();
    char **items = malloc(500 * sizeof(char *));
    for (int i = 0; i < 500; i++) {
        items[i] = malloc(32);
        snprintf(items[i], 32, "model-%03d", i);
    }
    tui_list_popup_set_items(p, (const char *const *)items, 500);
    tui_list_popup_set_terminal_size(p, 80, 24);
    tui_list_popup_show(p, 0);

    tui_list_popup_set_filter(p, "model-4");
    assert(tui_list_popup_filtered_count(p) == 100); /* 400-499 */
    tui_list_popup_move_bottom(p);
    assert(strcmp(tui_list_popup_selected_text(p), "model-499") == 0);
    assert(tui_list_popup_move_top(p) == 1);
    assert(strcmp(tui_list_popup_selected_text(p), "model-400") == 0);

    DynamicBuffer *buf = dynamic_buffer_create(0);
    tui_list_popup_view(p, buf);
    assert(strstr(buf_data(buf), "model-400") != NULL);
    assert(strstr(buf_data(buf), "model-499") == NULL); /* only 8 rows fit */
    assert(strstr(buf_data(buf), "(100/500)") != NULL);
    dynamic_buffer_destroy(buf);

    for (int i = 0; i < 500; i++)
        free(items[i]);
    free(items);
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
    assert(strstr(data, "alpha") != NULL);
    /* Selected row should have SGR styling (reverse or bg color) */
    assert(strstr(data, "\033[") != NULL);
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
    assert(strstr(data, "a") != NULL);
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
    RUN_TEST(test_filter_matches_are_case_insensitive_substrings);
    RUN_TEST(test_filter_no_filter_sees_all_items);
    RUN_TEST(test_filter_selection_resets_to_first_match);
    RUN_TEST(test_filter_empty_filter_sees_all_items);
    RUN_TEST(test_filter_multi_token_is_and_semantics);
    RUN_TEST(test_filter_set_items_clears_filter);
    RUN_TEST(test_filter_view_renders_only_matches);
    RUN_TEST(test_filter_view_empty_result_renders_nothing);
    RUN_TEST(test_filter_no_match_selection_is_none);
    RUN_TEST(test_filter_500_items_round_trip);

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
