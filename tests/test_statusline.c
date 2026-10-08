/* test_statusline.c - Unit tests for the status line component
 *
 * The component's contract is a DECLARED row: the caller says what the
 * segments are, in what order, on which side, what may give when the row
 * is narrow — and the component does the columns. These tests drive the
 * declarations and read the painted bytes back, so every assertion about
 * a column is an assertion about what the terminal receives.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/components/statusline.h>
#include <boba/dynamic_buffer.h>
#include <boba/msg.h>
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

/* ---------- helpers ---------- */

static TuiSegment seg(const char *text, TuiSegmentAlign align, int priority,
                      int min_cols, int pad_left)
{
    TuiSegment s;
    memset(&s, 0, sizeof(s));
    s.text = text;
    s.style = tui_style_new();
    s.align = align;
    s.priority = priority;
    s.min_cols = min_cols;
    s.pad_left = pad_left;
    return s;
}

/* The row the component paints, i.e. the view output minus its "\r" + EL
 * lead. Caller frees. */
static char *painted_row(const TuiStatusLine *sl)
{
    DynamicBuffer *buf = dynamic_buffer_create(64);
    tui_statusline_view(sl, buf);
    const char *data = dynamic_buffer_data(buf);
    size_t len = dynamic_buffer_len(buf);
    const char *lead = "\r\033[K";
    size_t lead_len = strlen(lead);
    assert(len >= lead_len);
    assert(memcmp(data, lead, lead_len) == 0);
    char *row = (char *)malloc(len - lead_len + 1);
    memcpy(row, data + lead_len, len - lead_len);
    row[len - lead_len] = '\0';
    dynamic_buffer_destroy(buf);
    return row;
}

static int cols_of(const char *s) { return tui_utf8_display_width(s); }

/* nevermore's shape: a fixed glyph + gauge on the left, a rule fill, and a
 * right-packed identity that gives first. */
static int nevermore_segments(TuiSegment *segs, const char *identity)
{
    segs[0] = seg("X ", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg("ctx 12.4K/200.0K ", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[2] = seg("\xe2\x94\x80", TUI_SEGMENT_FILL, 1, 1, 0); /* ─ */
    segs[3] = seg(identity, TUI_SEGMENT_RIGHT, 2, 1, 1);
    return 4;
}

/* ---------- create / free / height ---------- */

static void test_create_free_empty(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    assert(sl != NULL);
    assert(tui_statusline_get_height(sl) == 0);
    assert(tui_statusline_component() != NULL);

    /* No segments: the view paints only the row lead (an empty row). */
    DynamicBuffer *buf = dynamic_buffer_create(16);
    tui_statusline_view(sl, buf);
    assert(strcmp(dynamic_buffer_data(buf), "\r\033[K") == 0);
    dynamic_buffer_destroy(buf);

    tui_statusline_free(sl);
}

static void test_height_is_one_row_with_segments(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    TuiSegment segs[1] = { seg("abc", TUI_SEGMENT_LEFT, 0, 0, 0) };
    tui_statusline_set_segments(sl, segs, 1);
    assert(tui_statusline_get_height(sl) == 1);

    /* Clearing drops the row. */
    tui_statusline_set_segments(sl, NULL, 0);
    assert(tui_statusline_get_height(sl) == 0);
    tui_statusline_free(sl);
}

/* ---------- view contract ---------- */

static void test_view_lead_and_no_trailing_newline(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 10);
    TuiSegment segs[1] = { seg("abc", TUI_SEGMENT_LEFT, 0, 0, 0) };
    tui_statusline_set_segments(sl, segs, 1);

    DynamicBuffer *buf = dynamic_buffer_create(32);
    tui_statusline_view(sl, buf);
    const char *data = dynamic_buffer_data(buf);
    /* The lead is "\r" + EL (the runtime's row counting depends on the
     * ABSENCE of a trailing newline, and on nothing else). */
    assert(strncmp(data, "\r\033[K", 4) == 0);
    assert(strchr(data, '\n') == NULL);
    dynamic_buffer_destroy(buf);

    /* view is pure: painting twice emits identical bytes. */
    DynamicBuffer *a = dynamic_buffer_create(32);
    DynamicBuffer *b = dynamic_buffer_create(32);
    tui_statusline_view(sl, a);
    tui_statusline_view(sl, b);
    assert(dynamic_buffer_len(a) == dynamic_buffer_len(b));
    assert(memcmp(dynamic_buffer_data(a), dynamic_buffer_data(b),
                  dynamic_buffer_len(a)) == 0);
    dynamic_buffer_destroy(a);
    dynamic_buffer_destroy(b);

    tui_statusline_free(sl);
}

/* ---------- the fit: left + fill + right ---------- */

static void test_exact_fit_and_right_edge(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 40);
    TuiSegment segs[3];
    segs[0] = seg("ab", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg("\xe2\x94\x80", TUI_SEGMENT_FILL, 1, 1, 0);
    segs[2] = seg("right", TUI_SEGMENT_RIGHT, 0, 0, 0);
    tui_statusline_set_segments(sl, segs, 3);

    char *row = painted_row(sl);
    assert(cols_of(row) == 40);                          /* exactly the row */
    assert(strncmp(row, "ab", 2) == 0);                  /* left packs from column 0 */
    assert(strcmp(row + strlen(row) - 5, "right") == 0); /* ends at w-1 */
    assert(row[2] == (char)0xe2);                        /* the fill starts right after */
    free(row);
    tui_statusline_free(sl);
}

/* The slack goes to the fill: the row is always exactly the width, whatever
 * the caller's content, as long as one fill is declared. */
static void test_fill_absorbs_slack(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 20);
    TuiSegment segs[2];
    segs[0] = seg("hi", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg(".", TUI_SEGMENT_FILL, 1, 1, 0);
    tui_statusline_set_segments(sl, segs, 2);

    char *row = painted_row(sl);
    char want[21];
    memset(want, '.', 20);
    want[20] = '\0';
    memcpy(want, "hi", 2);
    assert(strcmp(row, want) == 0);
    free(row);

    /* A resize relayouts: the same declarations, a wider row. */
    tui_statusline_set_terminal_width(sl, 26);
    row = painted_row(sl);
    assert(cols_of(row) == 26);
    assert(strcmp(row, "hi........................") == 0);
    free(row);
    tui_statusline_free(sl);
}

/* A fill whose unit is wider than one column repeats whole units and blanks
 * the remainder (never a partial glyph). */
static void test_fill_multi_column_unit_remainder(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 9);
    TuiSegment segs[1] = { seg("ab ", TUI_SEGMENT_FILL, 1, 0, 0) };
    tui_statusline_set_segments(sl, segs, 1);

    char *row = painted_row(sl);
    assert(strcmp(row, "ab ab ab ") == 0); /* 3 units + 0 remainder */
    free(row);
    tui_statusline_free(sl);
}

/* Right-packed with no fill: the group is really right-packed, the gap is
 * blank (a segment never floats in the middle by accident). */
static void test_right_packed_without_fill(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 12);
    TuiSegment segs[2];
    segs[0] = seg("L", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg("R", TUI_SEGMENT_RIGHT, 0, 0, 0);
    tui_statusline_set_segments(sl, segs, 2);

    char *row = painted_row(sl);
    assert(strcmp(row, "L          R") == 0); /* 12 columns, R in the last */
    assert(cols_of(row) == 12);
    free(row);
    tui_statusline_free(sl);
}

/* Several RIGHT segments keep their declaration order and the last one ends
 * at the row's last column. */
static void test_several_right_segments(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 10);
    TuiSegment segs[3];
    segs[0] = seg("A", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg("BB", TUI_SEGMENT_RIGHT, 0, 0, 0);
    segs[2] = seg("CCC", TUI_SEGMENT_RIGHT, 0, 0, 0);
    tui_statusline_set_segments(sl, segs, 3);

    char *row = painted_row(sl);
    assert(strcmp(row, "A    BBCCC") == 0); /* 10 columns, order kept */
    assert(cols_of(row) == 10);
    free(row);
    tui_statusline_free(sl);
}

/* ---------- the ladder ---------- */

/* nevermore's row at four widths — the model's elision ladder end to end:
 * the identity gives first (elided, then dropped), the rule keeps a column,
 * the fixed chrome is never touched. */
static void test_nevermore_ladder(void)
{
    const char *ident = "openrouter \xc2\xb7 meta-llama/llama-3.3-70b";
    TuiSegment segs[4];
    int n = nevermore_segments(segs, ident);

    TuiStatusLine *sl = tui_statusline_create();

    /* 80: everything fits, the fill takes the slack. */
    tui_statusline_set_terminal_width(sl, 80);
    tui_statusline_set_segments(sl, segs, (size_t)n);
    char *row = painted_row(sl);
    assert(cols_of(row) == 80);
    assert(strstr(row, ident) != NULL);
    assert(strcmp(row + strlen(row) - strlen(ident), ident) == 0);
    free(row);

    /* 46: the identity is elided to what the row has left. */
    tui_statusline_set_terminal_width(sl, 46);
    row = painted_row(sl);
    assert(cols_of(row) == 46);
    assert(strstr(row, "openrouter \xc2\xb7 meta-llama/\xe2\x80\xa6") != NULL);
    assert(strstr(row, "llama-3.3-70b") == NULL);
    assert(row[19] == (char)0xe2); /* one rule column survives */
    free(row);

    /* 21: no room for the identity at all — dropped, rule only. */
    tui_statusline_set_terminal_width(sl, 21);
    row = painted_row(sl);
    assert(cols_of(row) == 21);
    assert(strstr(row, "openrouter") == NULL);
    assert(strstr(row, "\xe2\x94\x80") != NULL);
    free(row);

    /* 17: narrower than the fixed chrome — the row overflows (the
     * terminal's EL clears the tail); the chrome is NOT cut. */
    tui_statusline_set_terminal_width(sl, 17);
    row = painted_row(sl);
    assert(strncmp(row, "X ctx 12.4K/200.0K ", 19) == 0);
    assert(strstr(row, "openrouter") == NULL);
    free(row);

    tui_statusline_free(sl);
}

static void test_elision_keeps_head_and_marks_cut(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 6);
    TuiSegment segs[1] = { seg("abcdefghij", TUI_SEGMENT_LEFT, 1, 1, 0) };
    tui_statusline_set_segments(sl, segs, 1);

    char *row = painted_row(sl);
    assert(strcmp(row, "abcde\xe2\x80\xa6") == 0);
    assert(cols_of(row) == 6);
    free(row);

    /* A FIXED segment of the same text is never cut: the row overflows
     * into the terminal's EL instead. */
    segs[0].priority = 0;
    tui_statusline_set_segments(sl, segs, 1);
    row = painted_row(sl);
    assert(strcmp(row, "abcdefghij") == 0);
    free(row);
    tui_statusline_free(sl);
}

/* A wide cluster is never split: the cut lands on a cluster boundary, and
 * the column a wide glyph could not use goes back to the row — the fill
 * absorbs it, so the composed row is still exactly the width. */
static void test_elision_wide_cluster_boundary(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 5);
    TuiSegment segs[2];
    /* Two 2-cell clusters (CJK) + ASCII: 6 columns of text, shrunk to 4 —
     * room for one whole 2-cell cluster and the marker, i.e. 3 columns. */
    segs[0] = seg("\xe6\xbc\xa2\xe5\xad\x97\xe6\xbc\xa2", TUI_SEGMENT_LEFT, 2,
                  1, 0);
    segs[1] = seg("\xe2\x94\x80", TUI_SEGMENT_FILL, 1, 1, 0); /* ─ */
    tui_statusline_set_segments(sl, segs, 2);

    char *row = painted_row(sl);
    assert(strcmp(row, "\xe6\xbc\xa2\xe2\x80\xa6\xe2\x94\x80\xe2\x94\x80") ==
           0);                 /* 漢…── */
    assert(cols_of(row) == 5); /* the cut's spare column went to the fill */
    free(row);
    tui_statusline_free(sl);
}

/* min_cols is respected before the drop: a segment with a 6-column floor
 * stops there and the other one is untouched (the floor is where shrinking
 * stops, not where it starts). */
static void test_min_cols_floors_before_shrinking(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 14);
    TuiSegment segs[2];
    segs[0] = seg("AAAAAAAA", TUI_SEGMENT_LEFT, 1, 4, 0); /* floor 4 */
    segs[1] = seg("BBBBBBBB", TUI_SEGMENT_LEFT, 2, 6, 0); /* floor 6 */
    tui_statusline_set_segments(sl, segs, 2);

    char *row = painted_row(sl);
    assert(cols_of(row) == 14);
    /* B gives first (priority 2), stops at its floor of 6 and is elided
     * there; A keeps its 8 columns, because the row fits again. */
    assert(strcmp(row, "AAAAAAAABBBBB\xe2\x80\xa6") == 0);
    free(row);
    tui_statusline_free(sl);
}

/* Below the floors the same order continues, so a narrower row shrinks
 * both segments (priority first, then declaration order). */
static void test_shrinking_continues_below_the_floor(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 12);
    TuiSegment segs[2];
    segs[0] = seg("AAAAAAAA", TUI_SEGMENT_LEFT, 1, 4, 0); /* floor 4 */
    segs[1] = seg("BBBBBBBB", TUI_SEGMENT_LEFT, 2, 6, 0); /* floor 6 */
    tui_statusline_set_segments(sl, segs, 2);

    char *row = painted_row(sl);
    assert(cols_of(row) == 12);
    /* B floors at 6, then A gives the remaining 2 — both still above their
     * floors, so neither is dropped, and both carry the cut marker. */
    assert(strcmp(row, "AAAAA\xe2\x80\xa6"
                       "BBBBB\xe2\x80\xa6") == 0);
    free(row);
    tui_statusline_free(sl);
}

/* A segment that cannot reach its floor is dropped whole — its pad goes
 * with it, and the freed columns go to the fill. */
static void test_drop_takes_its_pad(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 6);
    TuiSegment segs[3];
    segs[0] = seg("left", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg("Z", TUI_SEGMENT_FILL, 1, 1, 0);
    segs[2] = seg("IDENTITY", TUI_SEGMENT_RIGHT, 2, 1, 1);
    tui_statusline_set_segments(sl, segs, 3);

    char *row = painted_row(sl);
    assert(cols_of(row) == 6);
    assert(strstr(row, "IDENTITY") == NULL);
    assert(strstr(row, "I") == NULL);   /* nothing of it survives, not even
                                         * the elided head */
    assert(strcmp(row, "leftZZ") == 0); /* the pad left with it */
    free(row);
    tui_statusline_free(sl);
}

/* pad_left is reserved inside the segment's width and separates it from
 * whatever precedes it. */
static void test_pad_left_reserved(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 10);
    TuiSegment segs[2];
    segs[0] = seg("ab", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg("cd", TUI_SEGMENT_RIGHT, 0, 0, 1);
    tui_statusline_set_segments(sl, segs, 2);

    char *row = painted_row(sl);
    assert(strcmp(row, "ab      cd") == 0); /* the pad is the gap */
    assert(cols_of(row) == 10);
    free(row);
    tui_statusline_free(sl);
}

/* ---------- width handling ---------- */

static void test_default_width_and_clamp(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    TuiSegment segs[1] = { seg(".", TUI_SEGMENT_FILL, 1, 0, 0) };

    /* Width 0 means the default (80). */
    tui_statusline_set_segments(sl, segs, 1);
    char *row = painted_row(sl);
    assert(cols_of(row) == TUI_STATUSLINE_DEFAULT_COLS);
    free(row);

    /* A terminal wider than the budget stops at the budget. */
    tui_statusline_set_terminal_width(sl, 4000);
    row = painted_row(sl);
    assert(cols_of(row) == TUI_STATUSLINE_MAX_COLS);
    free(row);

    tui_statusline_free(sl);
}

/* ---------- change detection ---------- */

/* An identical declaration set is a no-op: the owned text is not re-copied
 * (the pointer stays), and the composed row is not rebuilt. */
static void test_identical_segments_are_a_noop(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    TuiSegment segs[1] = { seg("hello", TUI_SEGMENT_LEFT, 0, 0, 0) };
    tui_statusline_set_segments(sl, segs, 1);
    const char *first = sl->seg_text[0];
    const char *row = sl->row;

    tui_statusline_set_segments(sl, segs, 1);
    assert(sl->seg_text[0] == first); /* no re-copy */
    assert(sl->row == row);           /* no realloc */

    /* A changed byte re-copies (and the row follows). */
    segs[0].text = "hellp";
    tui_statusline_set_segments(sl, segs, 1);
    char *painted = painted_row(sl);
    assert(strcmp(painted, "hellp") == 0);
    free(painted);
    tui_statusline_free(sl);
}

/* A style change alone is a change (it reaches the painted bytes). */
static void test_style_change_detected(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    TuiSegment segs[1] = { seg("x", TUI_SEGMENT_LEFT, 0, 0, 0) };
    tui_statusline_set_segments(sl, segs, 1);

    segs[0].style = tui_style_bold(tui_style_new(), 1);
    tui_statusline_set_segments(sl, segs, 1);

    DynamicBuffer *buf = dynamic_buffer_create(32);
    tui_statusline_view(sl, buf);
    assert(strstr(dynamic_buffer_data(buf), "\033[1mx\033[0m") != NULL);
    dynamic_buffer_destroy(buf);
    tui_statusline_free(sl);
}

/* The layout is EAGER: the setters compose the row, so a caller that never
 * calls view() still has the bytes, and view() only emits them. */
static void test_layout_is_eager(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 8);
    TuiSegment segs[2];
    segs[0] = seg("ab", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg(".", TUI_SEGMENT_FILL, 1, 0, 0);
    tui_statusline_set_segments(sl, segs, 2);

    /* Composed by the setter, before any view call. */
    assert(sl->row_len == 8);
    assert(memcmp(sl->row, "ab......", 8) == 0);

    /* And re-composed by a resize, again before any view call. */
    tui_statusline_set_terminal_width(sl, 4);
    assert(sl->row_len == 4);
    assert(memcmp(sl->row, "ab..", 4) == 0);

    tui_statusline_free(sl);
}

/* ---------- styles ---------- */

static void test_pieces_carry_their_styles(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    tui_statusline_set_terminal_width(sl, 20);
    TuiSegment segs[2];
    segs[0] = seg("plain ", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1] = seg("bold", TUI_SEGMENT_LEFT, 0, 0, 0);
    segs[1].style = tui_style_bold(tui_style_new(), 1);
    tui_statusline_set_segments(sl, segs, 2);

    DynamicBuffer *buf = dynamic_buffer_create(64);
    tui_statusline_view(sl, buf);
    const char *data = dynamic_buffer_data(buf);
    assert(strstr(data, "plain ") != NULL);
    assert(strstr(data, "\033[1mbold\033[0m") != NULL);
    dynamic_buffer_destroy(buf);
    tui_statusline_free(sl);
}

/* ---------- component slots ---------- */

static void test_update_handles_window_size(void)
{
    TuiStatusLine *sl = tui_statusline_create();
    TuiSegment segs[1] = { seg(".", TUI_SEGMENT_FILL, 1, 0, 0) };
    tui_statusline_set_segments(sl, segs, 1);

    tui_statusline_update(sl, tui_msg_window_size(30, 10));
    assert(sl->terminal_width == 30);
    char *row = painted_row(sl);
    assert(cols_of(row) == 30);
    free(row);

    /* Any other message is a no-op. */
    tui_statusline_update(sl, tui_msg_key(TUI_KEY_ENTER, 0, 0));
    assert(sl->terminal_width == 30);

    tui_statusline_free(sl);
}

/* The component vtable: init builds a model, view paints its row, free
 * releases it (ASan is the default build here, so a leak fails the run). */
static void test_component_slots(void)
{
    const TuiComponent *c = tui_statusline_component();
    assert(c && c->init && c->update && c->view && c->free);

    TuiInitResult ir = c->init(NULL);
    assert(ir.model != NULL);
    TuiStatusLine *sl = (TuiStatusLine *)ir.model;

    TuiSegment segs[1] = { seg("slot", TUI_SEGMENT_LEFT, 0, 0, 0) };
    tui_statusline_set_segments(sl, segs, 1);

    DynamicBuffer *buf = dynamic_buffer_create(32);
    TuiView v = c->view(ir.model, buf);
    assert(v.layer == buf);
    assert(v.cursor.visible == 0); /* chrome carries no cursor */
    assert(strstr(dynamic_buffer_data(buf), "slot") != NULL);
    dynamic_buffer_destroy(buf);

    c->update(ir.model, tui_msg_window_size(44, 10));
    assert(sl->terminal_width == 44);

    c->free(ir.model);
}

int main(void)
{
    printf("Running statusline tests:\n");

    /* lifecycle */
    RUN_TEST(test_create_free_empty);
    RUN_TEST(test_height_is_one_row_with_segments);
    RUN_TEST(test_view_lead_and_no_trailing_newline);

    /* layout */
    RUN_TEST(test_exact_fit_and_right_edge);
    RUN_TEST(test_fill_absorbs_slack);
    RUN_TEST(test_fill_multi_column_unit_remainder);
    RUN_TEST(test_right_packed_without_fill);
    RUN_TEST(test_several_right_segments);
    RUN_TEST(test_nevermore_ladder);
    RUN_TEST(test_elision_keeps_head_and_marks_cut);
    RUN_TEST(test_elision_wide_cluster_boundary);
    RUN_TEST(test_min_cols_floors_before_shrinking);
    RUN_TEST(test_shrinking_continues_below_the_floor);
    RUN_TEST(test_drop_takes_its_pad);
    RUN_TEST(test_pad_left_reserved);

    /* width */
    RUN_TEST(test_default_width_and_clamp);

    /* change detection */
    RUN_TEST(test_identical_segments_are_a_noop);
    RUN_TEST(test_style_change_detected);
    RUN_TEST(test_layout_is_eager);

    /* styles */
    RUN_TEST(test_pieces_carry_their_styles);

    /* component */
    RUN_TEST(test_update_handles_window_size);
    RUN_TEST(test_component_slots);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
