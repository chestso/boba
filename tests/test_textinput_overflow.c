/* test_textinput_overflow.c - the window, resize and layout contracts
 *
 * Everything here states what the component PROMISES, not what it used to
 * print. Three layers:
 *
 *   1. spec tests   - one rule each: the layout's wrap arithmetic, the
 *                     window's clamps, the two placement modes
 *   2. invariants   - the same properties checked after every step of a
 *                     randomized edit/resize sequence, so a rule that
 *                     holds in isolation is also checked when combined
 *   3. fit helper   - tui_fit_rows/tui_fit_cols on the degenerate matrix,
 *                     because every component will lean on them
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/ansi_sequences.h>
#include <boba/cmd.h>
#include <boba/components/textinput.h>
#include <boba/dynamic_buffer.h>
#include <boba/msg.h>

#include "fit.h"

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

static TuiUpdateResult run_update(TuiTextInput *input, TuiMsg msg)
{
    const TuiComponent *c = tui_textinput_component();
    return c->update((TuiModel *)input, msg);
}

static void send_char(TuiTextInput *input, char c)
{
    TuiUpdateResult r = run_update(input, tui_msg_char((uint32_t)c, 0));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

static void send_key(TuiTextInput *input, int key)
{
    TuiUpdateResult r = run_update(input, tui_msg_key(key, 0, 0));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

static void send_ctrl(TuiTextInput *input, char c)
{
    TuiUpdateResult r =
        run_update(input, tui_msg_key(TUI_KEY_NONE, (uint32_t)c, TUI_MOD_CTRL));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

static void send_key_mods(TuiTextInput *input, int key, int mods)
{
    TuiUpdateResult r = run_update(input, tui_msg_key(key, 0, mods));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

/* Feed a WINDOW_SIZE message: what a resize delivers. */
static void send_size(TuiTextInput *input, int w, int h)
{
    TuiUpdateResult r = run_update(input, tui_msg_window_size(w, h));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

/* The frame's bytes as a NUL-terminated string (freed by the caller). */
static char *render_view(TuiTextInput *input)
{
    DynamicBuffer *buf = dynamic_buffer_create(256);
    assert(buf != NULL);
    tui_textinput_view(input, buf);
    size_t n = dynamic_buffer_len(buf);
    char *out = (char *)malloc(n + 1);
    assert(out != NULL);
    memcpy(out, dynamic_buffer_data(buf), n);
    out[n] = '\0';
    dynamic_buffer_destroy(buf);
    return out;
}

static int count_char(const char *s, char c)
{
    int n = 0;
    for (; *s; s++)
        if (*s == c)
            n++;
    return n;
}

static int count_substr(const char *hay, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);
    while ((hay = strstr(hay, needle)) != NULL) {
        n++;
        hay += len;
    }
    return n;
}

/* The absolute rows a frame addressed, i.e. the CUP <r>;1H sequences —
 * deliberately not "every CSI", because [K and the SGRs are not rows.
 * Fills up to `max` entries and returns how many were found. */
static int cup_rows(const char *out, int *rows, int max)
{
    int n = 0;
    const char *p = out;
    while ((p = strstr(p, "\x1b[")) != NULL) {
        const char *q = p + 2;
        if (*q < '0' || *q > '9') {
            p = q;
            continue;
        }
        int r = 0;
        while (*q >= '0' && *q <= '9')
            r = r * 10 + (*q++ - '0');
        if (q[0] == ';' && q[1] == '1' && q[2] == 'H') {
            if (n < max)
                rows[n] = r;
            n++;
        }
        p = q;
    }
    return n;
}

/* Display width of the widest row a frame painted, with the escape
 * sequences it emits (EL, SGR, CUP) removed and rows split on '\r'/'\n'.
 * These tests paint no wide clusters, so counting bytes is counting
 * columns; that is what makes "a row is wider than the box" assertable. */
static int widest_row_cols(const char *out)
{
    int widest = 0, cur = 0;
    for (const char *p = out; *p; p++) {
        if (*p == '\r' || *p == '\n') {
            if (cur > widest)
                widest = cur;
            cur = 0;
            continue;
        }
        if (*p == '\x1b') {
            p++;
            if (*p == '[') {
                p++;
                while (*p && !(*p >= '@' && *p <= '~'))
                    p++;
            }
            continue;
        }
        cur++;
    }
    if (cur > widest)
        widest = cur;
    return widest;
}

/* Build `count` logical lines of FIXED width (8 columns + '\n'), so a
 * line's byte offset is line * 9 and the tests can name one. */
#define LINE_BYTES 9

static char *make_lines(int count)
{
    size_t cap = (size_t)count * LINE_BYTES + 16;
    char *s = (char *)malloc(cap);
    assert(s != NULL);
    s[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < count; i++) {
        if (i)
            s[used++] = '\n';
        int n = snprintf(s + used, cap - used, "%03d-line", i);
        used += (size_t)n;
    }
    return s;
}

static void set_lines(TuiTextInput *input, int count)
{
    char *s = make_lines(count);
    tui_textinput_set_text(input, s);
    free(s);
}

/* ---------- fit helper ---------- */

static void test_fit_rows_natural(void)
{
    /* Nothing known about the screen: the content's own height wins. */
    assert(tui_fit_rows(5, 0, NULL, 0, 0) == 5);
    assert(tui_fit_rows(1, 0, NULL, 0, 0) == 1);
    assert(tui_fit_rows(0, 0, NULL, 0, 0) == 1);
}

static void test_fit_rows_screen_cap(void)
{
    assert(tui_fit_rows(100, 24, NULL, 0, 0) == 24);
    assert(tui_fit_rows(10, 24, NULL, 0, 0) == 10);
}

static void test_fit_rows_reserves(void)
{
    TuiFitInsets ins = { 2, 1, 0, 0 };
    assert(tui_fit_rows(100, 10, &ins, 0, 0) == 7);
    /* Absolute placement is expressed as rows above. */
    TuiFitInsets row20 = { 19, 0, 0, 0 };
    assert(tui_fit_rows(100, 24, &row20, 0, 0) == 5);
}

static void test_fit_rows_min_never_leaves_the_screen(void)
{
    /* A floor is a preference; it cannot smuggle a frame off-screen. */
    assert(tui_fit_rows(100, 3, NULL, 5, 0) == 3);
    assert(tui_fit_rows(100, 24, NULL, 5, 0) == 24);
    assert(tui_fit_rows(4, 24, NULL, 5, 0) == 4);
}

static void test_fit_rows_max_cap(void)
{
    assert(tui_fit_rows(100, 40, NULL, 0, 8) == 8);
    assert(tui_fit_rows(3, 40, NULL, 0, 8) == 3);
}

static void test_fit_rows_degenerate_screen(void)
{
    /* Reserves larger than the screen still yield a row to look at. */
    TuiFitInsets ins = { 5, 5, 0, 0 };
    assert(tui_fit_rows(100, 2, &ins, 0, 0) == 1);
    assert(tui_fit_rows(100, 1, NULL, 0, 0) == 1);
}

static void test_fit_cols(void)
{
    /* Nothing known reads as "no constraint". */
    assert(tui_fit_cols(0, NULL, 0) == 0);
    assert(tui_fit_cols(80, NULL, 0) == 80);

    TuiFitInsets ins = { 0, 0, 2, 0 };
    assert(tui_fit_cols(80, &ins, 0) == 78);
    assert(tui_fit_cols(80, &ins, 30) == 30);
    /* Degenerate: still one column. */
    assert(tui_fit_cols(3, &ins, 0) == 1);
}

/* ---------- layout and window ---------- */

static TuiTextInput *make_input(void)
{
    TuiTextInputConfig cfg = { .multiline = 1 };
    TuiTextInput *input = tui_textinput_create(&cfg);
    assert(input != NULL);
    return input;
}

static void test_natural_height_is_the_text_height(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 10);
    assert(tui_textinput_total_rows(in) == 10);
    assert(tui_textinput_visible_rows(in) == 10);
    tui_textinput_free(in);
}

static void test_fixed_rows_window(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 100);
    tui_textinput_set_cursor(in, 0);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.rows = 5;
    tui_textinput_set_geometry(in, &g);

    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.total_rows == 100);
    assert(s.visible_rows == 5);
    assert(s.first_row == 0); /* the cursor is on the first row */
    assert(s.hidden_above == 0);
    assert(s.hidden_below == 95);
    tui_textinput_free(in);
}

static void test_cursor_at_end_scrolls_into_view(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 100);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.rows = 5;
    g.scrolloff = 1;
    tui_textinput_set_geometry(in, &g);

    /* Typing at the end (which set_text leaves the cursor past) puts the
     * cursor on screen with one row of context below it. */
    tui_textinput_set_cursor(in, tui_textinput_len(in));

    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.visible_rows == 5);
    /* The cursor is the last row, so the window cannot honour scrolloff
     * without leaving blank rows: it clamps to total - H instead. */
    assert(s.first_row == 95);
    assert(s.hidden_below == 0);
    assert(s.hidden_above == 95);

    int row = -1, col = -1;
    tui_textinput_cursor_visual(in, &row, &col);
    assert(row == 4); /* the last visible row */
    assert(col == 8); /* "099-line" */
    tui_textinput_free(in);
}

static void test_home_pulls_the_window_to_the_top(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 100);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.rows = 5;
    tui_textinput_set_geometry(in, &g);
    tui_textinput_set_cursor(in, tui_textinput_len(in));
    assert(tui_textinput_scroll_state(in).first_row > 0);

    /* Ctrl+Home is the buffer start (plain Home is the line start, which
     * would leave the cursor on the last line). */
    send_key_mods(in, TUI_KEY_HOME, TUI_MOD_CTRL);
    assert(tui_textinput_scroll_state(in).first_row == 0);
    tui_textinput_free(in);
}

static void test_page_down_moves_by_h_minus_1(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 100);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.rows = 5;
    g.scrolloff = 0;
    tui_textinput_set_geometry(in, &g);
    tui_textinput_set_cursor(in, 0);

    send_key(in, TUI_KEY_PAGE_DOWN);
    int row = -1, col = -1;
    tui_textinput_cursor_visual(in, &row, &col);
    assert(row == 4); /* H - 1 visual rows of motion */
    /* Row 4 is the last visible row, so the window has not scrolled yet. */
    assert(tui_textinput_scroll_state(in).first_row == 0);

    send_key(in, TUI_KEY_PAGE_UP);
    tui_textinput_cursor_visual(in, &row, &col);
    assert(row == 0);
    assert(tui_textinput_scroll_state(in).first_row == 0);
    tui_textinput_free(in);
}

static void test_scrolloff_is_capped_at_half_the_window(void)
{
    /* Mid-document, so the "no blank rows below" clamp is not what decides
     * the answer: an absurd scrolloff is capped at (H - 1) / 2 == 2. */
    TuiTextInput *in = make_input();
    set_lines(in, 100);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.rows = 5;
    g.scrolloff = 10;
    tui_textinput_set_geometry(in, &g);
    tui_textinput_set_cursor(in, 60 * LINE_BYTES); /* line 60 */

    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.first_row == 58); /* 60 - (5 - 1 - 2) */

    /* With a scrolloff that fits, the cursor keeps exactly that much. */
    g.scrolloff = 1;
    tui_textinput_set_geometry(in, &g);
    tui_textinput_set_cursor(in, 70 * LINE_BYTES);          /* line 70 */
    assert(tui_textinput_scroll_state(in).first_row == 67); /* 70 - (5 - 1 - 1) */
    tui_textinput_free(in);
}

static void test_wrap_puts_the_prompt_on_the_first_row_only(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .prompt = "> ", .multiline = 1, .wrap = TUI_TEXTINPUT_WRAP, .geometry = { .width = 12 } });
    assert(in != NULL);
    tui_textinput_set_text(in, "abcdefghijklmnop");

    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.total_rows == 2); /* "> abcdefghij" + "  klmnop" */

    char *out = render_view(in);
    assert(count_substr(out, "> ") == 1);
    assert(count_char(out, '\n') == 1); /* two inline rows */
    assert(strstr(out, "abcdefghij") != NULL);
    assert(strstr(out, "klmnop") != NULL);
    free(out);
    tui_textinput_free(in);
}

static void test_clip_scrolls_every_row_together(void)
{
    /* One horizontal window for all rows: the text scrolls under a fixed
     * column window, so neighbouring lines do not jump sideways relative to
     * the cursor's line. */
    TuiTextInput *in = make_input();
    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.width = 20;
    tui_textinput_set_geometry(in, &g);

    char text[3][48];
    for (int i = 0; i < 3; i++) {
        memset(text[i], 'x' + i, 41);
        text[i][0] = (char)('a' + i);
        text[i][40] = (char)('A' + i);
        text[i][41] = '\0';
    }
    char joined[160];
    snprintf(joined, sizeof(joined), "%s\n%s\n%s", text[0], text[1], text[2]);
    tui_textinput_set_text(in, joined);

    /* Cursor at the end of line 0: the window is the line's right edge. */
    tui_textinput_set_cursor(in, 41);

    char *out = render_view(in);
    /* The far end of every line is painted... */
    assert(strstr(out, "A") != NULL);
    assert(strstr(out, "B") != NULL);
    assert(strstr(out, "C") != NULL);
    /* ...and the left edge of no line is. */
    assert(strstr(out, "a") == NULL);
    assert(strstr(out, "b") == NULL);
    assert(strstr(out, "c") == NULL);
    free(out);
    tui_textinput_free(in);
}

static void test_screen_clamp_needs_the_size_message(void)
{
    /* The clamp is not a discovery. Without a routed TUI_MSG_WINDOW_SIZE the
     * input has no screen to clamp to, so an AT_ROW box paints its rows
     * wherever `row` says — the documented consequence of forgetting it
     * (see "The screen size is routed, not discovered" in the header).
     * Route the size and the very same box clamps. */
    TuiTextInput *in = make_input();

    TuiTextInputGeometry g = { 0 };
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.place = TUI_TEXTINPUT_AT_ROW;
    g.row = 35; /* first screen row, on a 40-row terminal */
    g.width = 40;
    g.rows = 10;
    tui_textinput_set_geometry(in, &g);
    tui_textinput_set_text(in, "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10");

    assert(tui_textinput_visible_rows(in) == 10); /* no screen to cap it */
    char *out = render_view(in);
    int rows[16];
    int n = cup_rows(out, rows, 16);
    assert(n == 10);
    assert(rows[0] == 35);
    assert(rows[9] == 44); /* four rows past the bottom */
    free(out);

    /* Told the size, the window stops at the last screen row. */
    send_size(in, 80, 40);
    assert(tui_textinput_visible_rows(in) == 6);

    out = render_view(in);
    n = cup_rows(out, rows, 16);
    assert(n == 6);
    assert(rows[0] == 35);
    assert(rows[5] == 40);
    free(out);
    tui_textinput_free(in);
}

static void test_clip_does_not_clip_a_row_to_the_cursor_lines_length(void)
{
    /* The CLIP window's right edge is a COLUMN, not the cursor line's
     * length: a box wider than every line paints every line whole, wherever
     * the cursor happens to be. */
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .wrap = TUI_TEXTINPUT_CLIP });
    assert(in != NULL);
    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.width = 120; /* wider than both lines: nothing needs clipping */
    tui_textinput_set_geometry(in, &g);

    tui_textinput_set_text(in, "alpha\nbeta"); /* cursor on "beta" */
    assert(tui_textinput_scroll_state(in).offset_right == 120);

    char *out = render_view(in);
    assert(strstr(out, "alpha") != NULL); /* was "alph": clipped at 4 */
    assert(strstr(out, "beta") != NULL);
    free(out);
    tui_textinput_free(in);
}

static void test_clip_window_is_one_column_range_for_every_row(void)
{
    /* Cursor on the longest line: the window scrolls (rather than "fitting"),
     * and the shorter rows are still painted through the same COLUMN window,
     * so what they show is what falls inside it — not "everything up to
     * where the cursor's line ends". */
    TuiTextInput *in = make_input();
    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.width = 10;
    tui_textinput_set_geometry(in, &g);

    tui_textinput_set_text(in,
                           "0123456789ABCDEF\n0123456789ABCDEFGH\nab");
    /* set_text leaves the cursor at the end of the "ab" line, so the window
     * starts at column 0. Then move it onto the 16-column line: the window
     * scrolls to keep it in view. */
    tui_textinput_set_cursor(in, 16); /* end of line 0 */

    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.offset_left == 7);   /* 17 - 10 */
    assert(s.offset_right == 17); /* a column, past the cursor's line end */

    char *out = render_view(in);
    /* Line 0 from column 7: the window's last 9 columns. */
    assert(strstr(out, "789ABCDEF") != NULL);
    assert(strstr(out, "0123456") == NULL);
    /* "ab" is entirely left of the window, so its row is blank... */
    assert(strstr(out, "ab") == NULL);
    /* ...while the 18-column line keeps painting to the window's right
     * edge, one column past where the cursor's line ends. */
    assert(strstr(out, "789ABCDEFG") != NULL);
    free(out);
    tui_textinput_free(in);
}

static void test_clip_short_line_resets_the_window_to_column_zero(void)
{
    /* Scrolled to the right on a long line, then the cursor moves to a line
     * that fits: the window jumps back to column 0, and every row follows
     * (this is the "one window for every row" rule, not a per-row one). */
    TuiTextInput *in = make_input();
    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.width = 10;
    tui_textinput_set_geometry(in, &g);

    tui_textinput_set_text(in, "0123456789ABCDEF\nab");
    tui_textinput_set_cursor(in, 16); /* end of the long line */
    assert(tui_textinput_scroll_state(in).offset_left > 0);

    tui_textinput_set_cursor(in, tui_textinput_len(in)); /* end of "ab" */
    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.offset_left == 0);
    assert(s.offset_right == 10);

    char *out = render_view(in);
    assert(strstr(out, "0123456789") != NULL); /* the long row is back */
    assert(strstr(out, "ab") != NULL);
    free(out);
    tui_textinput_free(in);
}

static void test_clip_window_is_budgeted_against_the_widest_prompt(void)
{
    /* A continuation prompt wider than the main one is not a layout, but it
     * must not overflow the box: the one shared window is sized for the
     * wider prompt, so both rows stay inside `width`. */
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .prompt = "> ", .wrap = TUI_TEXTINPUT_CLIP });
    assert(in != NULL);
    tui_textinput_set_continuation_prompt(in, "...."); /* 4 > 2 */

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.width = 12;
    tui_textinput_set_geometry(in, &g);

    tui_textinput_set_text(in, "0123456789ABCDEFGHIJ\nabcdefghijklmnopqrst");
    tui_textinput_set_cursor(in, 20); /* end of line 0 */

    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.offset_right - s.offset_left == 8); /* 12 - the wider prompt */

    char *out = render_view(in);
    assert(widest_row_cols(out) <= 12);
    /* Both rows show the same slice of their line, "> " and "...." aside. */
    assert(strstr(out, "> DEFGHIJ") != NULL);
    assert(strstr(out, "....nopqrst") != NULL);
    free(out);
    tui_textinput_free(in);
}

static void test_clip_rows_never_exceed_the_box(void)
{
    /* The invariant behind the two tests above, over every cursor position:
     * no painted row is wider than the box, whatever the cursor does. */
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .prompt = "> ", .wrap = TUI_TEXTINPUT_CLIP });
    assert(in != NULL);
    tui_textinput_set_continuation_prompt(in, "....");

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.width = 12;
    tui_textinput_set_geometry(in, &g);

    tui_textinput_set_text(in,
                           "0123456789ABCDEFGHIJ\nabcdefghij\nklmnopqrstuvwxyz"
                           "\n\nx\n0123456789ABCDEFGHIJ0123456789");

    for (size_t pos = 0; pos <= tui_textinput_len(in); pos++) {
        tui_textinput_set_cursor(in, pos);
        char *out = render_view(in);
        assert(widest_row_cols(out) <= 12);
        free(out);
    }
    tui_textinput_free(in);
}

static void test_one_column_width_always_progresses(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .wrap = TUI_TEXTINPUT_WRAP, .geometry = { .width = 1 } });
    assert(in != NULL);
    tui_textinput_set_text(in, "\xe7\x95\x8c\xe7\x95\x8c"); /* two wide clusters */
    /* A cluster wider than the row is taken whole: one row each, no hang. */
    assert(tui_textinput_total_rows(in) == 2);
    char *out = render_view(in);
    assert(count_substr(out, "\xe7\x95\x8c") == 2);
    free(out);
    tui_textinput_free(in);
}

static void test_prompt_is_dropped_when_it_cannot_fit(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .prompt = "> ", .multiline = 1, .wrap = TUI_TEXTINPUT_WRAP, .geometry = { .width = 1 } });
    assert(in != NULL);
    tui_textinput_set_text(in, "abc");
    char *out = render_view(in);
    assert(strstr(out, "> ") == NULL);
    assert(strstr(out, "a") != NULL);
    free(out);
    tui_textinput_free(in);
}

static void test_empty_and_trailing_newline_rows(void)
{
    TuiTextInput *in = make_input();

    assert(tui_textinput_total_rows(in) == 1); /* empty input keeps a row */
    tui_textinput_set_text(in, "a");
    assert(tui_textinput_total_rows(in) == 1);
    tui_textinput_set_text(in, "a\n");
    assert(tui_textinput_total_rows(in) == 2);
    tui_textinput_set_text(in, "a\n\n");
    assert(tui_textinput_total_rows(in) == 3);
    tui_textinput_free(in);
}

static void test_cursor_at_end_of_full_row_stays_on_that_row(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .wrap = TUI_TEXTINPUT_WRAP, .geometry = { .width = 4 } });
    assert(in != NULL);
    tui_textinput_set_text(in, "abcd"); /* exactly one full row */
    assert(tui_textinput_total_rows(in) == 1);
    send_key(in, TUI_KEY_END);

    int row = -1, col = -1;
    tui_textinput_cursor_visual(in, &row, &col);
    assert(row == 0);
    assert(col == 3); /* last cell of the row, not a phantom second row */
    tui_textinput_free(in);
}

static void test_absolute_placement_never_paints_past_the_screen(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 30);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.place = TUI_TEXTINPUT_AT_ROW;
    g.row = 20;
    g.rows = 10;
    tui_textinput_set_geometry(in, &g);
    send_size(in, 80, 24); /* 5 rows left below row 20 */

    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.visible_rows == 5);

    char *out = render_view(in);
    int rows[32];
    int n = cup_rows(out, rows, 32);
    assert(n == s.visible_rows);
    for (int i = 0; i < n; i++)
        assert(rows[i] >= 20 && rows[i] <= 24);
    free(out);
    tui_textinput_free(in);
}

static void test_resize_keeps_the_cursor_in_the_window(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .wrap = TUI_TEXTINPUT_WRAP, .geometry = { .sizing = TUI_TEXTINPUT_FIXED, .rows = 6 } });
    assert(in != NULL);

    char *text = (char *)malloc(600);
    assert(text != NULL);
    text[0] = '\0';
    for (int i = 0; i < 20; i++)
        strcat(text, "lorem ipsum dolor sit amet ");
    tui_textinput_set_text(in, text);
    free(text);
    tui_textinput_set_cursor(in, tui_textinput_len(in));

    int widths[] = { 80, 20, 5, 80, 12, 200 };
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        TuiTextInputGeometry g = tui_textinput_geometry(in);
        g.width = widths[i];
        tui_textinput_set_geometry(in, &g);
        send_size(in, widths[i], 24);

        TuiTextInputScroll s = tui_textinput_scroll_state(in);
        assert(s.visible_rows >= 1 && s.visible_rows <= s.total_rows);
        assert(s.first_row >= 0);
        assert(s.first_row <= s.total_rows - s.visible_rows);
        assert(s.hidden_below >= 0); /* no blank tail after a width change */
        int row = -1, col = -1;
        tui_textinput_cursor_visual(in, &row, &col);
        assert(row >= 0 && row < s.visible_rows);
    }
    tui_textinput_free(in);
}

static void test_height_shrink_and_grow_pulls_rows_back(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 30);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.rows = 20;
    tui_textinput_set_geometry(in, &g);
    tui_textinput_set_cursor(in, tui_textinput_len(in));

    int grown_before = tui_textinput_visible_rows(in);
    assert(grown_before == 20);

    /* Shrink: fewer rows, and the cursor stays put on screen. */
    g.rows = 3;
    tui_textinput_set_geometry(in, &g);
    assert(tui_textinput_visible_rows(in) == 3);
    assert(tui_textinput_scroll_state(in).hidden_below == 0);

    /* Grow back: hidden rows above return, and no blank rows appear. */
    g.rows = 20;
    tui_textinput_set_geometry(in, &g);
    TuiTextInputScroll s = tui_textinput_scroll_state(in);
    assert(s.visible_rows == 20);
    assert(s.hidden_below == 0);
    int row = -1, col = -1;
    tui_textinput_cursor_visual(in, &row, &col);
    assert(row == 19); /* last visible row, since scrolloff is 1 by default */
    tui_textinput_free(in);
}

static void test_flow_derives_height_from_the_reserves(void)
{
    TuiTextInput *in = make_input();
    set_lines(in, 100);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FLOW;
    g.reserve_above = 1;
    g.reserve_below = 2;
    tui_textinput_set_geometry(in, &g);
    send_size(in, 80, 24);

    assert(tui_textinput_visible_rows(in) == 21);
    tui_textinput_free(in);
}

static void test_flow_derives_width_from_the_window(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .wrap = TUI_TEXTINPUT_WRAP, .geometry = { .sizing = TUI_TEXTINPUT_FLOW } });
    assert(in != NULL);

    char text[41];
    memset(text, 'x', 40);
    text[40] = '\0';
    tui_textinput_set_text(in, text);
    assert(tui_textinput_total_rows(in) == 1); /* width unknown yet */

    send_size(in, 10, 24);
    assert(tui_textinput_total_rows(in) == 4); /* 40 columns / 10 */

    /* A declared width wins over the window: an app that boxes the input
     * in a border knows its own width. */
    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.width = 20;
    tui_textinput_set_geometry(in, &g);
    assert(tui_textinput_total_rows(in) == 2);
    tui_textinput_free(in);
}

static void test_resize_while_unfocused_still_updates_the_model(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .wrap = TUI_TEXTINPUT_WRAP, .geometry = { .sizing = TUI_TEXTINPUT_FLOW } });
    assert(in != NULL);

    char text[41];
    memset(text, 'x', 40);
    text[40] = '\0';
    tui_textinput_set_text(in, text);

    run_update(in, tui_msg_blur());
    send_size(in, 10, 24);
    assert(tui_textinput_total_rows(in) == 4);

    /* And the cursor is still reported hidden, not stale. */
    assert(tui_textinput_cursor_pos(in).visible == 0);
    tui_textinput_free(in);
}

/* ---------- invariants ---------- */

/* Every property the window promises, checked in one place so the
 * randomized sequence cannot silently skip one. */
static void check_invariants(TuiTextInput *in, int absolute, int term_h)
{
    TuiTextInputScroll s = tui_textinput_scroll_state(in);

    /* 1. at least one row, never more than the text or the box */
    assert(s.visible_rows >= 1);
    assert(s.visible_rows <= s.total_rows);

    /* 2. the window is inside the rows that exist */
    assert(s.first_row >= 0);
    assert(s.first_row <= s.total_rows - s.visible_rows);

    /* the three numbers account for every row */
    assert(s.hidden_above == s.first_row);
    assert(s.hidden_above + s.visible_rows + s.hidden_below == s.total_rows);

    /* 3. the cursor is inside the window */
    int row = -1, col = -1;
    tui_textinput_cursor_visual(in, &row, &col);
    assert(row >= 0 && row < s.visible_rows);
    assert(col >= 0);

    /* 4 and 5: the frame paints exactly H rows, inside the screen */
    char *out = render_view(in);
    if (absolute) {
        assert(count_char(out, '\n') == 0);
        int rows[64];
        int n = cup_rows(out, rows, 64);
        assert(n == s.visible_rows);
        for (int i = 0; i < n; i++)
            assert(rows[i] >= 1 && (term_h <= 0 || rows[i] <= term_h));
    } else {
        /* one '\r\n' between rows, none after the last */
        assert(count_char(out, '\n') == s.visible_rows - 1);
    }
    free(out);
}

static void test_invariants_hold_over_a_random_sequence(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .geometry = { .sizing = TUI_TEXTINPUT_FIXED, .rows = 8 } });
    assert(in != NULL);

    unsigned seed = 12345;
    int term_w = 40, term_h = 12;

    for (int step = 0; step < 1500; step++) {
        seed = seed * 1103515245u + 12345u;
        unsigned op = (seed >> 16) % 100u;

        if (op < 40) {
            send_char(in, (char)('a' + (int)((seed >> 8) % 26u)));
        } else if (op < 50) {
            send_key(in, TUI_KEY_BACKSPACE);
        } else if (op < 58) {
            send_ctrl(in, 'j'); /* newline in multiline */
        } else if (op < 66) {
            send_key(in, TUI_KEY_UP);
        } else if (op < 74) {
            send_key(in, TUI_KEY_DOWN);
        } else if (op < 80) {
            send_key(in, TUI_KEY_LEFT);
        } else if (op < 86) {
            send_key(in, TUI_KEY_RIGHT);
        } else if (op < 91) {
            send_key(in, (seed & 1) ? TUI_KEY_PAGE_UP : TUI_KEY_PAGE_DOWN);
        } else if (op < 94) {
            send_key(in, TUI_KEY_HOME);
        } else if (op < 97) {
            TuiTextInputGeometry g = tui_textinput_geometry(in);
            g.rows = (int)((seed >> 4) % 12u); /* 0..11, 0 = no cap */
            tui_textinput_set_geometry(in, &g);
        } else {
            /* A resize, in either dimension. */
            term_w = 5 + (int)((seed >> 3) % 60u);
            term_h = 2 + (int)((seed >> 9) % 20u);
            send_size(in, term_w, term_h);
        }

        check_invariants(in, 0, term_h);
    }
    tui_textinput_free(in);
}

static void test_invariants_hold_in_absolute_mode(void)
{
    TuiTextInput *in = tui_textinput_create(&(TuiTextInputConfig){
        .multiline = 1, .geometry = { .sizing = TUI_TEXTINPUT_FLOW, .place = TUI_TEXTINPUT_AT_ROW, .row = 5, .rows = 6 } });
    assert(in != NULL);

    unsigned seed = 987;
    int term_w = 40, term_h = 12;

    for (int step = 0; step < 600; step++) {
        seed = seed * 1103515245u + 12345u;
        unsigned op = (seed >> 16) % 100u;
        if (op < 50)
            send_char(in, (char)('a' + (int)((seed >> 8) % 26u)));
        else if (op < 65)
            send_ctrl(in, 'j');
        else if (op < 80)
            send_key(in, TUI_KEY_BACKSPACE);
        else {
            term_w = 6 + (int)((seed >> 3) % 50u);
            term_h = 6 + (int)((seed >> 9) % 16u);
            send_size(in, term_w, term_h);
        }
        check_invariants(in, 1, term_h);
    }
    tui_textinput_free(in);
}

static void test_erase_never_targets_a_row_off_screen(void)
{
    /* A frame painted when the screen was tall, then a resize that makes it
     * short: the surplus erase must not aim at rows the screen no longer
     * has (they clamp to the last row and overdraw it). */
    TuiTextInput *in = make_input();
    set_lines(in, 20);

    TuiTextInputGeometry g = tui_textinput_geometry(in);
    g.sizing = TUI_TEXTINPUT_FIXED;
    g.place = TUI_TEXTINPUT_AT_ROW;
    g.row = 1;
    g.rows = 20;
    tui_textinput_set_geometry(in, &g);
    send_size(in, 80, 30);
    assert(tui_textinput_visible_rows(in) == 20);

    char *first = render_view(in);
    free(first);

    /* Shrink both the box and the screen. */
    send_size(in, 80, 4);
    g.rows = 10;
    tui_textinput_set_geometry(in, &g);

    char *out = render_view(in);
    int rows[64];
    int n = cup_rows(out, rows, 64);
    assert(n >= 1);
    for (int i = 0; i < n; i++)
        assert(rows[i] >= 1 && rows[i] <= 4);
    free(out);
    tui_textinput_free(in);
}

/* ---------- main ---------- */

int main(void)
{
    printf("Running textinput overflow tests...\n");

    /* fit helper */
    RUN_TEST(test_fit_rows_natural);
    RUN_TEST(test_fit_rows_screen_cap);
    RUN_TEST(test_fit_rows_reserves);
    RUN_TEST(test_fit_rows_min_never_leaves_the_screen);
    RUN_TEST(test_fit_rows_max_cap);
    RUN_TEST(test_fit_rows_degenerate_screen);
    RUN_TEST(test_fit_cols);

    /* layout and window */
    RUN_TEST(test_natural_height_is_the_text_height);
    RUN_TEST(test_fixed_rows_window);
    RUN_TEST(test_cursor_at_end_scrolls_into_view);
    RUN_TEST(test_home_pulls_the_window_to_the_top);
    RUN_TEST(test_page_down_moves_by_h_minus_1);
    RUN_TEST(test_scrolloff_is_capped_at_half_the_window);
    RUN_TEST(test_wrap_puts_the_prompt_on_the_first_row_only);
    RUN_TEST(test_clip_scrolls_every_row_together);
    RUN_TEST(test_clip_does_not_clip_a_row_to_the_cursor_lines_length);
    RUN_TEST(test_clip_window_is_one_column_range_for_every_row);
    RUN_TEST(test_clip_short_line_resets_the_window_to_column_zero);
    RUN_TEST(test_clip_window_is_budgeted_against_the_widest_prompt);
    RUN_TEST(test_clip_rows_never_exceed_the_box);
    RUN_TEST(test_screen_clamp_needs_the_size_message);
    RUN_TEST(test_one_column_width_always_progresses);
    RUN_TEST(test_prompt_is_dropped_when_it_cannot_fit);
    RUN_TEST(test_empty_and_trailing_newline_rows);
    RUN_TEST(test_cursor_at_end_of_full_row_stays_on_that_row);
    RUN_TEST(test_absolute_placement_never_paints_past_the_screen);
    RUN_TEST(test_resize_keeps_the_cursor_in_the_window);
    RUN_TEST(test_height_shrink_and_grow_pulls_rows_back);
    RUN_TEST(test_flow_derives_height_from_the_reserves);
    RUN_TEST(test_flow_derives_width_from_the_window);
    RUN_TEST(test_resize_while_unfocused_still_updates_the_model);

    /* invariants */
    RUN_TEST(test_erase_never_targets_a_row_off_screen);
    RUN_TEST(test_invariants_hold_over_a_random_sequence);
    RUN_TEST(test_invariants_hold_in_absolute_mode);

    printf("%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
