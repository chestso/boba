/* test_text.c - the one width-aware fit/emit walk (text.h).
 *
 * The walk replaced five component-local copies (viewport clipping and its
 * column lookup, list_popup truncation, statusline elision, textinput's
 * wrap/scroll budget). These pin the shared behavior: whole clusters only,
 * escapes passed over and included in the prefix, a strict budget except
 * when the caller needs progress. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <boba/dynamic_buffer.h>
#include <boba/text.h>
#include <boba/unicode.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

static const char *buf_str(const DynamicBuffer *b)
{
    return dynamic_buffer_data(b);
}

static void test_fit_ascii(void)
{
    TuiTextFit f = tui_text_fit("abcdef", 6, 3, 0);
    assert(f.bytes == 3);
    assert(f.cols == 3);
    assert(f.clipped == 1);

    /* Fits exactly: nothing is clipped. */
    f = tui_text_fit("abc", 3, 3, 0);
    assert(f.bytes == 3 && f.cols == 3 && f.clipped == 0);

    /* A generous budget consumes everything. */
    f = tui_text_fit("ab", 2, 10, 0);
    assert(f.bytes == 2 && f.cols == 2 && f.clipped == 0);
}

static void test_fit_empty_and_budget(void)
{
    TuiTextFit f = tui_text_fit(NULL, 0, 5, 0);
    assert(f.bytes == 0 && f.cols == 0 && f.clipped == 0);

    f = tui_text_fit("abc", 3, 0, 0);
    assert(f.bytes == 0 && f.cols == 0 && f.clipped == 1);

    f = tui_text_fit("abc", 3, -4, 0);
    assert(f.bytes == 0 && f.cols == 0 && f.clipped == 1);

    /* No input is never "clipped", even on a zero budget. */
    f = tui_text_fit("", 0, 0, 0);
    assert(f.bytes == 0 && f.cols == 0 && f.clipped == 0);
}

static void test_fit_wide_clusters(void)
{
    /* 中文 = two 2-column clusters. A 3-column budget takes one. */
    TuiTextFit f = tui_text_fit("\xE4\xB8\xAD\xE6\x96\x87", 6, 3, 0);
    assert(f.bytes == 3 && f.cols == 2 && f.clipped == 1);

    /* A cluster wider than the budget is left out (strict). */
    f = tui_text_fit("\xE4\xB8\xAD", 3, 1, 0);
    assert(f.bytes == 0 && f.cols == 0 && f.clipped == 1);

    /* ... unless the caller needs progress (a chunked walk). */
    f = tui_text_fit("\xE4\xB8\xAD", 3, 1, 1);
    assert(f.bytes == 3 && f.cols == 2 && f.clipped == 0);

    /* at_least_one still stops at the budget for later clusters. */
    f = tui_text_fit("\xE4\xB8\xAD\xE6\x96\x87", 6, 1, 1);
    assert(f.bytes == 3 && f.cols == 2 && f.clipped == 1);
}

static void test_fit_zero_width_cluster(void)
{
    /* "e" + combining acute is one cluster of width 1. The mark rides
     * with its base and is never left behind. */
    TuiTextFit f = tui_text_fit("e\xCC\x81x", 4, 1, 0);
    assert(f.bytes == 3 && f.cols == 1 && f.clipped == 1);

    /* A lone combining mark is a zero-width cluster: it fits a budget
     * that is already full, and it does not advance the column. */
    f = tui_text_fit("a\xCC\x81", 3, 1, 0);
    assert(f.bytes == 3 && f.cols == 1 && f.clipped == 0);
}

static void test_fit_escapes(void)
{
    /* SGR is zero-width and part of the prefix. */
    const char *sgr = "\x1b[31mab\x1b[0m";
    TuiTextFit f = tui_text_fit(sgr, strlen(sgr), 2, 0);
    assert(f.cols == 2 && f.clipped == 0);
    assert(f.bytes == strlen(sgr)); /* the reset is consumed too */

    /* The cut lands on a cluster boundary, after the sequences that
     * precede it. */
    const char *more = "\x1b[31mab\x1b[0mcd";
    f = tui_text_fit(more, strlen(more), 2, 0);
    assert(f.cols == 2 && f.clipped == 1);
    assert(f.bytes == strlen("\x1b[31mab\x1b[0m"));

    /* OSC 8 is one sequence, not text: the URL occupies no columns. */
    const char *osc = "\x1b]8;;http://example.com/\x07"
                      "abcd"
                      "\x1b]8;;\x07";
    f = tui_text_fit(osc, strlen(osc), 4, 0);
    assert(f.cols == 4 && f.clipped == 0);
    assert(f.bytes == strlen(osc));
}

static void test_emit_matches_fit(void)
{
    const char *s = "a\xE4\xB8\xAD"
                    "b"; /* a, 中 (2 cols), b */
    DynamicBuffer *out = dynamic_buffer_create(16);
    TuiTextFit e = tui_text_emit(out, s, strlen(s), 3, 0, NULL, NULL);
    TuiTextFit f = tui_text_fit(s, strlen(s), 3, 0);
    assert(e.bytes == f.bytes && e.cols == f.cols && e.clipped == f.clipped);
    assert(e.bytes == strlen(s) - 1); /* the trailing 'b' is clipped */
    assert(dynamic_buffer_len(out) == e.bytes);
    assert(memcmp(buf_str(out), s, e.bytes) == 0);

    /* A NULL output measures and emits nothing (tui_text_fit). */
    dynamic_buffer_destroy(out);
}

typedef struct
{
    int calls;
    int cols[8];
    int widths[8];
} HookLog;

static void hook(DynamicBuffer *out, int col, int width, void *user)
{
    HookLog *log = (HookLog *)user;
    if (log->calls < 8) {
        log->cols[log->calls] = col;
        log->widths[log->calls] = width;
    }
    log->calls++;
    char mark[8];
    snprintf(mark, sizeof(mark), "<%d>", col);
    dynamic_buffer_append_str(out, mark);
}

static void test_emit_hook_reports_columns(void)
{
    const char *s = "ab\xE4\xB8\xAD"; /* a, b, 中 */
    DynamicBuffer *out = dynamic_buffer_create(64);
    HookLog log = { 0 };

    TuiTextFit e = tui_text_emit(out, s, strlen(s), 4, 0, hook, &log);
    assert(e.cols == 4 && e.clipped == 0);
    assert(log.calls == 3);
    assert(log.cols[0] == 0 && log.widths[0] == 1);
    assert(log.cols[1] == 1 && log.widths[1] == 1);
    assert(log.cols[2] == 2 && log.widths[2] == 2);

    /* The hook fires before each cluster's bytes, so its markers are
     * interleaved with the text in column order. */
    assert(strcmp(buf_str(out), "<0>a<1>b<2>\xE4\xB8\xAD") == 0);

    dynamic_buffer_destroy(out);

    /* The hook is not called in measure mode. */
    HookLog log2 = { 0 };
    (void)tui_text_emit(NULL, s, strlen(s), 3, 0, hook, &log2);
    assert(log2.calls == 0);
}

static void test_fit_used_as_column_lookup(void)
{
    /* viewport's byte-offset-for-column is exactly a strict fit. */
    const char *s = "\x1b[31m"
                    "abcd";
    assert(tui_text_fit(s, strlen(s), 2, 0).bytes == strlen("\x1b[31mab"));
    assert(tui_text_fit(s, strlen(s), 99, 0).bytes == strlen(s));
}

int main(void)
{
    printf("text fit tests:\n");
    RUN_TEST(test_fit_ascii);
    RUN_TEST(test_fit_empty_and_budget);
    RUN_TEST(test_fit_wide_clusters);
    RUN_TEST(test_fit_zero_width_cluster);
    RUN_TEST(test_fit_escapes);
    RUN_TEST(test_emit_matches_fit);
    RUN_TEST(test_emit_hook_reports_columns);
    RUN_TEST(test_fit_used_as_column_lookup);
    printf("text fit: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
