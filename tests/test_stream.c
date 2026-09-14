/* test_stream.c - streaming transcript component tests.
 *
 * Message-driven, no pty: a real TuiRuntime with a tmpfile output
 * drives the transcript as its component; flushes run the commit
 * pass. render_block marks its rows ("R|") so scrollback bytes are
 * distinguishable from frame bytes in the capture; line-mode live
 * content is boba-owned plain text and is asserted through
 * tui_transcript_view().
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/dynamic_buffer.h>
#include <boba/runtime.h>
#include <boba/stream.h>
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

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

#define OUT_CAP (1024 * 1024)

typedef struct
{
    TuiTranscript *t;
    TuiRuntime *rt;
    FILE *out;
    char *text;
    DynamicBuffer *view;
} H;

static void emit_text_rows(TuiRowSink *sink, const char *mark,
                           const char *text, size_t len)
{
    if (len > 0 && text[len - 1] == '\n')
        len--;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            if (mark)
                tui_row_text(sink, mark, strlen(mark));
            if (i > start)
                tui_row_text(sink, text + start, i - start);
            tui_row_end(sink);
            start = i + 1;
        }
    }
}

/* Optional per-test row probe for render_block. */
static void (*g_row_probe)(TuiRowSink *) = NULL;

static void test_render_block(const TuiBlock *blk, const char *text, size_t len,
                              int width, TuiRowSink *sink, void *ud)
{
    (void)blk;
    (void)width;
    (void)ud;
    if (g_row_probe) {
        g_row_probe(sink);
        return;
    }
    emit_text_rows(sink, "R|", text, len);
}

static void test_render_live(const TuiBlock *blk, const char *text, size_t len,
                             int width, int rows_cap, TuiRowSink *sink,
                             void *ud)
{
    (void)blk;
    (void)width;
    (void)rows_cap;
    (void)ud;
    emit_text_rows(sink, "L|", text, len);
}

static const char *h_read(H *h)
{
    fflush(h->out);
    long pos = ftell(h->out);
    rewind(h->out);
    size_t n = fread(h->text, 1, OUT_CAP - 1, h->out);
    h->text[n] = '\0';
    fseek(h->out, pos, SEEK_SET);
    return h->text;
}

/* The live region (what the next view() would emit). */
static const char *h_view(H *h)
{
    dynamic_buffer_clear(h->view);
    tui_transcript_view(h->t, h->view, 60, 10);
    return h->view->data;
}

static void h_free(H *h)
{
    if (h->rt)
        tui_runtime_free(h->rt); /* frees the transcript via component free */
    if (h->out)
        fclose(h->out);
    free(h->text);
    dynamic_buffer_destroy(h->view);
    free(h);
}

static H *h_new(const TuiStreamSpec *streams,
                const TuiClassifier **classifiers, size_t n)
{
    H *h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    h->text = malloc(OUT_CAP);
    h->out = tmpfile();
    h->view = dynamic_buffer_create(512);
    TuiTranscriptConfig cfg = {
        .render_block = test_render_block,
        .render_live = test_render_live,
        .streams = streams,
        .classifiers = classifiers,
        .n_streams = n,
    };
    h->t = tui_transcript_create(&cfg);
    if (!h->text || !h->out || !h->view || !h->t) {
        h_free(h);
        return NULL;
    }
    TuiRuntimeConfig rcfg = { .raw_mode = 0, .output = h->out };
    h->rt = tui_runtime_create((TuiComponent *)tui_transcript_component(h->t),
                               h->t, &rcfg);
    if (!h->rt) {
        h_free(h);
        return NULL;
    }
    tui_runtime_set_transcript(h->rt, h->t);
    tui_runtime_send(h->rt, tui_msg_window_size(60, 10));
    return h;
}

/* feed + flush (flush runs the commit pass) */
static void h_send(H *h, TuiMsg msg)
{
    tui_runtime_send(h->rt, msg);
    tui_msg_free(&msg);
}

static void h_flush(H *h)
{
    tui_runtime_flush(h->rt);
}

static int count_substr(const char *hay, const char *needle)
{
    int n = 0;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Default classifier: one-line lookahead, blank finalizes             */
/* ------------------------------------------------------------------ */

static void test_default_blank_line_flow(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    /* line 1 completes: nothing committed (lookahead holds it) */
    h_send(h, tui_msg_stream_delta(0, "hello\n", 6));
    h_flush(h);
    assert(tui_transcript_commit_count(h->t) == 0);
    assert(strstr(h_read(h), "R|hello") == NULL);
    assert(strstr(h_view(h), "hello") != NULL); /* live, plain */

    /* line 2 completes: line 1 freezes and commits */
    h_send(h, tui_msg_stream_delta(0, "world\n", 6));
    h_flush(h);
    assert(tui_transcript_commit_count(h->t) == 1);
    assert(strstr(h_read(h), "R|hello\r\n") != NULL);
    assert(strstr(h_read(h), "R|world") == NULL); /* still lookahead */
    assert(strstr(h_view(h), "world") != NULL);

    /* blank line: line 2 freezes; nothing else committed */
    h_send(h, tui_msg_stream_delta(0, "\n", 1));
    h_flush(h);
    assert(tui_transcript_commit_count(h->t) == 2);
    const char *out = h_read(h);
    assert(strstr(out, "R|world\r\n") != NULL);
    /* committed exactly once (never re-emitted) */
    assert(count_substr(out, "R|hello\r\n") == 1);
    assert(count_substr(out, "R|world\r\n") == 1);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Coalescing: N units frozen in one batch -> ONE transcript_write     */
/* ------------------------------------------------------------------ */

static void test_coalescing_one_write_per_batch(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    /* l0..l3 in one delta: when l3 completes, l0..l2 are frozen and
     * staged; one flush -> one commit */
    h_send(h, tui_msg_stream_delta(0, "l0\nl1\nl2\nl3\n", 12));
    h_flush(h);
    assert(tui_transcript_commit_count(h->t) == 1);
    const char *out = h_read(h);
    assert(strstr(out, "R|l0\r\n") != NULL);
    assert(strstr(out, "R|l1\r\n") != NULL);
    assert(strstr(out, "R|l2\r\n") != NULL);

    /* a second batch is a second (single) commit */
    h_send(h, tui_msg_stream_delta(0, "m0\nm1\n", 6));
    h_flush(h);
    assert(tui_transcript_commit_count(h->t) == 2);
    out = h_read(h);
    assert(strstr(out, "R|l3\r\n") != NULL);
    assert(strstr(out, "R|m0\r\n") != NULL);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* A non-markdown fake classifier proves boba is app-agnostic          */
/* ------------------------------------------------------------------ */

typedef struct
{
    int reset_calls;
} FakeState;

static TuiLineClass fake_classify(void *state, const char *line, size_t len,
                                  const char *prev, size_t prev_len,
                                  TuiBlockKind *out_kind)
{
    (void)state;
    (void)prev;
    (void)prev_len;
    *out_kind = TUI_BLOCK_LIST;
    if (len == 0)
        return TUI_LINE_BLANK;
    if (line[0] == '*')
        return TUI_LINE_BLOCK_START;
    return TUI_LINE_CONTINUES;
}

static void fake_reset(void *state)
{
    ((FakeState *)state)->reset_calls++;
}

static void test_fake_classifier_drives_component(void)
{
    TuiStreamSpec streams[1] = { { "log" } };
    FakeState st = { 0 };
    const TuiClassifier fake = {
        .state = &st,
        .classify = fake_classify,
        .reset = fake_reset,
    };
    const TuiClassifier *classifiers[1] = { &fake };
    H *h = h_new(streams, classifiers, 1);
    assert(h);

    h_send(h, tui_msg_stream_delta(0, "* one\n* two\n", 12));
    h_flush(h);
    const char *out = h_read(h);
    assert(strstr(out, "R|* one\r\n") != NULL); /* kind LIST, bytes as-is */
    assert(strstr(out, "R|* two") == NULL);     /* still pending */

    h_send(h, tui_msg_stream_end(0));
    h_flush(h);
    out = h_read(h);
    assert(strstr(out, "R|* two\r\n") != NULL);
    assert(st.reset_calls == 1); /* reset at stream end */

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Granularity: byte (system stream) + partial-row extension           */
/* ------------------------------------------------------------------ */

static void test_byte_batch_extends_partial_row(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    h_send(h, tui_msg_stream_text(-1, "abc", 3)); /* no newline */
    h_flush(h);
    assert(h->rt->inline_partial_open == 1);
    assert(h->rt->inline_partial_cols == 3);
    assert(tui_transcript_commit_count(h->t) == 1);
    const char *out = h_read(h);
    assert(strstr(out, "abc\r\n") != NULL); /* cursor moved below the row */

    /* continuation: the seam extends the open row (up 1 + right to
     * col 3) instead of starting a fresh line */
    h_send(h, tui_msg_stream_text(-1, "def\n", 4));
    h_flush(h);
    assert(h->rt->inline_partial_open == 0);
    out = h_read(h);
    assert(strstr(out, "abc\r\ndef") == NULL); /* no fresh-line lie */
    /* the extension sequence, then the continuation bytes */
    assert(strstr(out, "\x1b[1A\r\x1b[3Cdef\r\n") != NULL);

    h_free(h);
}

/* Unlabeled fence: byte-granular container, verbatim, suppresses the
 * classifier's normal verdicts while open */

static TuiLineClass fence_classify(void *state, const char *line, size_t len,
                                   const char *prev, size_t prev_len,
                                   TuiBlockKind *out_kind)
{
    int *open = state;
    (void)prev;
    (void)prev_len;
    *out_kind = TUI_BLOCK_FENCE_PLAIN;
    if (!*open) {
        if (len >= 3 && strncmp(line, "```", 3) == 0) {
            *open = 1;
            return TUI_LINE_CONTAINER_OPEN;
        }
        *out_kind = TUI_BLOCK_PARAGRAPH;
        return TUI_LINE_BLOCK_START;
    }
    if (len >= 3 && strncmp(line, "```", 3) == 0) {
        *open = 0;
        return TUI_LINE_CONTAINER_CLOSE;
    }
    return TUI_LINE_CONTINUES;
}

static void test_unlabeled_fence_streams_verbatim(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    int open = 0;
    const TuiClassifier fence = {
        .state = &open,
        .classify = fence_classify,
        .reset = NULL,
    };
    const TuiClassifier *classifiers[1] = { &fence };
    H *h = h_new(streams, classifiers, 1);
    assert(h);

    static const char body[] = "```\ncode1\ncode2\n";
    h_send(h, tui_msg_stream_delta(0, body, strlen(body)));
    h_flush(h);
    const char *out = h_read(h);
    /* bytes stream verbatim as soon as the container opens */
    assert(strstr(out, "```\r\ncode1\r\ncode2\r\n") != NULL);
    assert(strstr(out, "R|") == NULL); /* never app-rendered */

    h_send(h, tui_msg_stream_delta(0, "```", 3)); /* close marker, no \n */
    h_send(h, tui_msg_stream_end(0));
    h_flush(h);
    out = h_read(h);
    const char *c2 = strstr(out, "code2\r\n");
    assert(c2 != NULL);
    assert(strstr(c2, "```\r\n") != NULL); /* close marker after code2 */
    assert(open == 0);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Granularity: line (labeled fence)                                   */
/* ------------------------------------------------------------------ */

static TuiLineClass labeled_fence_classify(void *state, const char *line,
                                           size_t len, const char *prev,
                                           size_t prev_len,
                                           TuiBlockKind *out_kind)
{
    int *open = state;
    (void)prev;
    (void)prev_len;
    *out_kind = TUI_BLOCK_FENCE;
    if (!*open) {
        if (len >= 3 && strncmp(line, "```", 3) == 0) {
            *open = 1;
            return TUI_LINE_CONTAINER_OPEN;
        }
        *out_kind = TUI_BLOCK_PARAGRAPH;
        return TUI_LINE_BLOCK_START;
    }
    if (len >= 3 && strncmp(line, "```", 3) == 0) {
        *open = 0;
        return TUI_LINE_CONTAINER_CLOSE;
    }
    return TUI_LINE_CONTINUES;
}

static void test_labeled_fence_line_granular(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    int open = 0;
    const TuiClassifier fence = {
        .state = &open,
        .classify = labeled_fence_classify,
        .reset = NULL,
    };
    const TuiClassifier *classifiers[1] = { &fence };
    H *h = h_new(streams, classifiers, 1);
    assert(h);

    static const char open_fence[] = "```rust\nlet x = 1;\n";
    h_send(h, tui_msg_stream_delta(0, open_fence, strlen(open_fence)));
    h_flush(h);
    const char *out = h_read(h);
    /* after the first body line completes, the open line is frozen */
    assert(strstr(out, "R|```rust\r\n") != NULL);
    assert(strstr(out, "R|let x = 1;") == NULL); /* one-line lookahead */
    assert(strstr(h_view(h), "let x = 1;") != NULL);

    static const char body2[] = "let y = 2;\n```\n";
    h_send(h, tui_msg_stream_delta(0, body2, strlen(body2)));
    h_flush(h);
    out = h_read(h);
    assert(strstr(out, "R|let x = 1;\r\n") != NULL);
    assert(strstr(out, "R|let y = 2;\r\n") != NULL);
    assert(strstr(out, "R|```\r\n") != NULL);
    /* committed in order */
    const char *a = strstr(out, "R|```rust\r\n");
    const char *b = strstr(out, "R|let x = 1;\r\n");
    const char *c = strstr(out, "R|let y = 2;\r\n");
    const char *d = strstr(out, "R|```\r\n");
    assert(a && b && c && d && a < b && b < c && c < d);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Granularity: block (table) + windowing                              */
/* ------------------------------------------------------------------ */

static TuiLineClass table_classify(void *state, const char *line, size_t len,
                                   const char *prev, size_t prev_len,
                                   TuiBlockKind *out_kind)
{
    int *in_table = state;
    (void)prev_len;
    if (len == 0) {
        *in_table = 0;
        return TUI_LINE_BLANK;
    }
    /* a delimiter row: only - | : and spaces, with a prev containing
     * a '|' */
    int delim = len > 0;
    for (size_t i = 0; i < len; i++) {
        char c = line[i];
        if (c != '-' && c != '|' && c != ':' && c != ' ') {
            delim = 0;
            break;
        }
    }
    if (delim && !*in_table && prev && memchr(prev, '|', prev_len)) {
        *in_table = 1;
        *out_kind = TUI_BLOCK_TABLE;
        return TUI_LINE_RECLASSIFY_PREV;
    }
    if (*in_table)
        return TUI_LINE_CONTINUES;
    *out_kind = TUI_BLOCK_PARAGRAPH;
    return TUI_LINE_BLOCK_START;
}

static void test_table_block_holds_to_finalize(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    int in_table = 0;
    const TuiClassifier cls = {
        .state = &in_table,
        .classify = table_classify,
        .reset = NULL,
    };
    const TuiClassifier *classifiers[1] = { &cls };
    H *h = h_new(streams, classifiers, 1);
    assert(h);

    /* header, then delimiter: reclassify fires INSIDE the lookahead —
     * the header never commits as a paragraph */
    h_send(h, tui_msg_stream_delta(0, "| a | b |\n", 10));
    h_flush(h);
    assert(strstr(h_read(h), "R|| a | b |") == NULL);
    assert(tui_transcript_commit_count(h->t) == 0);

    h_send(h, tui_msg_stream_delta(0, "| --- |\n", 8));
    h_flush(h);
    assert(strstr(h_read(h), "R|") == NULL);          /* table live: not committed */
    assert(strstr(h_view(h), "L|| a | b |") != NULL); /* live block */

    /* rows grow; still nothing committed (block granularity) */
    static const char rows[] = "| 1 | 2 |\n| 3 | 4 |\n";
    h_send(h, tui_msg_stream_delta(0, rows, strlen(rows)));
    h_flush(h);
    assert(strstr(h_read(h), "R|") == NULL);
    assert(tui_transcript_commit_count(h->t) == 0);

    /* blank finalizes: the WHOLE table commits as one unit */
    h_send(h, tui_msg_stream_delta(0, "\n", 1));
    h_flush(h);
    const char *out = h_read(h);
    assert(strstr(out, "R|| a | b |\r\n") != NULL);
    assert(strstr(out, "R|| --- |\r\n") != NULL);
    assert(strstr(out, "R|| 1 | 2 |\r\n") != NULL);
    assert(strstr(out, "R|| 3 | 4 |\r\n") != NULL);
    assert(tui_transcript_commit_count(h->t) == 1);
    assert(count_substr(out, "R|| a | b |\r\n") == 1); /* exactly once */

    h_free(h);
}

static void test_tall_table_windows_to_tail(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    int in_table = 0;
    const TuiClassifier cls = {
        .state = &in_table,
        .classify = table_classify,
        .reset = NULL,
    };
    const TuiClassifier *classifiers[1] = { &cls };
    H *h = h_new(streams, classifiers, 1);
    assert(h);

    h_send(h, tui_msg_stream_delta(0, "| h |\n| - |\n", 12));
    for (int i = 0; i < 30; i++) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "| r%02d |\n", i);
        h_send(h, tui_msg_stream_delta(0, buf, (size_t)n));
    }
    h_flush(h);

    /* the frame view clips to the tail of the live block */
    assert(strstr(h_view(h), "L|| r28 |") != NULL); /* recent row present */
    assert(strstr(h_view(h), "L|| h |") == NULL);   /* head windowed away */
    assert(strstr(h_view(h), "| r00 |") == NULL);

    /* live_rows and view agree (same planner) */
    int rows = tui_transcript_live_rows(h->t, 60, 4);
    assert(rows == 4);

    /* finalize: ALL rows commit, in order, exactly once */
    h_send(h, tui_msg_stream_delta(0, "\n", 1));
    h_flush(h);
    const char *out = h_read(h);
    assert(strstr(out, "R|| h |\r\n") != NULL);
    assert(strstr(out, "R|| r00 |\r\n") != NULL);
    assert(strstr(out, "R|| r29 |\r\n") != NULL);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Multi-stream: order is global freeze order                          */
/* ------------------------------------------------------------------ */

static void test_multi_stream_global_order(void)
{
    TuiStreamSpec streams[2] = { { "content" }, { "reasoning" } };
    H *h = h_new(streams, NULL, 2);
    assert(h);

    /* reasoning phase then content phase (the wire truth), plus the
     * defensive interleave: all units freeze in one batch; commit
     * order is freeze order */
    h_send(h, tui_msg_stream_delta(1, "think1\nthink2\n", 14));
    h_send(h, tui_msg_stream_delta(0, "answer\n", 7));
    h_send(h, tui_msg_stream_end(1));
    h_send(h, tui_msg_stream_end(0));
    h_flush(h);

    const char *out = h_read(h);
    const char *a = strstr(out, "R|think1\r\n");
    const char *b = strstr(out, "R|think2\r\n");
    const char *c = strstr(out, "R|answer\r\n");
    assert(a && b && c);
    assert(a < b && b < c);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Submit: finalizes LIVE blocks, does NOT echo                        */
/* ------------------------------------------------------------------ */

static void test_submit_finalizes_without_echo(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    h_send(h, tui_msg_stream_delta(0, "partial answer", 14));
    h_flush(h);
    assert(strstr(h_read(h), "R|") == NULL);

    h_send(h, tui_msg_transcript_submit("user line", 9));
    h_flush(h);
    const char *out = h_read(h);
    assert(strstr(out, "R|partial answer\r\n") != NULL);
    assert(strstr(out, "user line") == NULL); /* echo is finish_inline's */

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Clear: resets state, emits nothing, orphans an open partial row     */
/* ------------------------------------------------------------------ */

static void test_clear_resets_and_emits_nothing(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    h_send(h, tui_msg_stream_delta(0, "before\nx\n", 9));
    h_flush(h);
    assert(tui_transcript_commit_count(h->t) == 1);

    /* an open partial row + clear: emits nothing, orphans the row */
    h_send(h, tui_msg_stream_text(-1, "abc", 3));
    h_flush(h);
    assert(h->rt->inline_partial_open == 1);
    unsigned long commits = tui_transcript_commit_count(h->t);
    size_t before_len = strlen(h_read(h));

    h_send(h, tui_msg_transcript_clear());
    h_flush(h);
    h_flush(h);
    assert(tui_transcript_commit_count(h->t) == commits);
    assert(h->rt->inline_partial_open == 0); /* orphaned, not extended */
    /* no transcript content was emitted; and no extension sequence */
    const char *out = h_read(h);
    assert(count_substr(out, "abc") == 1); /* the one from before clear */

    /* the next content does NOT extend the orphaned row */
    h_send(h, tui_msg_stream_text(-1, "def\n", 4));
    h_flush(h);
    out = h_read(h);
    const char *tail = out + before_len;
    assert(strstr(tail, "def\r\n") != NULL);
    assert(strstr(tail, "\x1b[1A\r\x1b[3C") == NULL); /* no extension */

    /* and the streams work fresh */
    h_send(h, tui_msg_stream_delta(0, "fresh\nagain\n", 12));
    h_flush(h);
    out = h_read(h);
    assert(strstr(out, "R|fresh\r\n") != NULL);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Raw-buffer trim                                                     */
/* ------------------------------------------------------------------ */

static void test_trim_after_commit(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    char big[4096];
    size_t off = 0;
    for (int i = 0; i < 100; i++)
        off += (size_t)snprintf(big + off, sizeof(big) - off, "line-%d\n", i);
    h_send(h, tui_msg_stream_delta(0, big, off));
    h_flush(h);

    /* everything but the lookahead line has been released */
    assert(tui_transcript_stream_raw_len(h->t, 0) < 32);
    /* the system stream keeps nothing after a commit */
    h_send(h, tui_msg_stream_text(-1, "sys\n", 4));
    h_flush(h);
    assert(tui_transcript_stream_raw_len(h->t, -1) == 0);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Sink: framing, tabs, wide chars, attrs                              */
/* ------------------------------------------------------------------ */

static void sink_probe_render(const TuiBlock *blk, const char *text, size_t len,
                              int width, TuiRowSink *sink, void *ud)
{
    (void)blk;
    (void)text;
    (void)len;
    (void)width;
    (void)ud;
    TuiAttr dim = { 0 };
    dim.dim = 1;
    tui_row_attr(sink, dim);
    tui_row_text(sink, "a\tb", 3); /* tab stop at col 8 */
    tui_row_end(sink);
    tui_row_attr_reset(sink);
    tui_row_text(sink, "\xe8\xa1\xa8", 3); /* wide char, width 2 */
    tui_row_pad_to(sink, 4);
    tui_row_text(sink, "x", 1);
    tui_row_end(sink);
}

static void test_sink_tabs_wide_pad_attrs(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    TuiTranscriptConfig cfg = {
        .render_block = sink_probe_render,
        .render_live = test_render_live,
        .streams = streams,
        .n_streams = 1,
    };
    TuiTranscript *t = tui_transcript_create(&cfg);
    assert(t);
    H *h = calloc(1, sizeof(*h));
    h->text = malloc(OUT_CAP);
    h->out = tmpfile();
    h->t = t;
    TuiRuntimeConfig rcfg = { .raw_mode = 0, .output = h->out };
    h->rt =
        tui_runtime_create((TuiComponent *)tui_transcript_component(t), t, &rcfg);
    tui_runtime_set_transcript(h->rt, t);
    tui_runtime_send(h->rt, tui_msg_window_size(60, 10));

    h_send(h, tui_msg_stream_delta(0, "unit\nnext\n", 10));
    h_flush(h);
    const char *out = h_read(h);
    /* tab expanded to the next multiple of 8 (col 1 -> col 8); the
     * dim attr opens the row and resets after its terminator */
    assert(strstr(out, "\x1b[0;2ma       b\r\n") != NULL);
    /* wide char occupies 2 cols; pad_to(4) adds 2 spaces; then x */
    assert(strstr(out, "\x1b[0m\xe8\xa1\xa8  x\r\n") != NULL);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Resize: live re-layouts; committed rows stay put                    */
/* ------------------------------------------------------------------ */

static void test_resize_live_relayout(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    h_send(h, tui_msg_stream_delta(0, "committed line\nnext\n", 21));
    h_flush(h);
    const char *out_before = h_read(h);
    size_t len_before = strlen(out_before);

    /* an unterminated tail: live = "next\n" + 28 a's; at width 20 the
     * tail wraps to 3 rows, at width 60 to 2 */
    h_send(h, tui_msg_stream_delta(0, "aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 28));
    assert(tui_transcript_live_rows(h->t, 20, 10) == 3);
    assert(tui_transcript_live_rows(h->t, 60, 10) == 2);

    /* resize + flush: committed bytes never re-laid-out */
    h_send(h, tui_msg_window_size(20, 10));
    h_flush(h);
    const char *out_after = h_read(h);
    assert(strncmp(out_before, out_after, len_before) == 0);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* Escape sequences split across commits stay verbatim + zero width    */
/* ------------------------------------------------------------------ */

/* A sink probe for split-escape column math: pad_to must treat the
 * sequence-final byte as zero width (with the carry state resumed). */
static void sink_split_escape_render(TuiRowSink *sink)
{
    tui_row_text(sink, "\x1b[31", 4); /* ESC [ 3 1 — split mid-CSI */
    tui_row_text(sink, "mAB", 3);     /* 'm' completes the sequence */
    tui_row_pad_to(sink, 5);          /* AB at col 2 -> 3 spaces */
    tui_row_text(sink, "Z", 1);
    tui_row_end(sink);
}

static void test_split_escape_sequence_passthrough(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    /* Byte path: the escape is staged onto the open row; the next
     * commit extends that row (up-1, col 0) rather than starting a
     * fresh line; the sequence and text stay verbatim. */
    static const char csi_head[] = "\x1b[31";
    static const char csi_tail[] = "mred\n";
    h_send(h, tui_msg_stream_text(-1, csi_head, strlen(csi_head)));
    h_flush(h);
    assert(h->rt->inline_partial_open == 1);
    assert(h->rt->inline_partial_cols == 0); /* zero display width */
    h_send(h, tui_msg_stream_text(-1, csi_tail, strlen(csi_tail)));
    h_flush(h);
    const char *out = h_read(h);
    assert(strstr(out, "\x1b[31\r\n") != NULL);       /* on the open row */
    assert(strstr(out, "\x1b[1A\rmred\r\n") != NULL); /* extended + closed */
    assert(strstr(out, "\r\nmred\r\n") == NULL);      /* no fresh-line lie */

    /* Sink path: split across two tui_row_text calls, the carry state
     * must keep the sequence at zero width (pad_to sees col 2). */
    g_row_probe = sink_split_escape_render;
    h_send(h, tui_msg_stream_delta(0, "x\ny\n", 4));
    h_flush(h);
    g_row_probe = NULL;
    out = h_read(h);
    assert(strstr(out, "\x1b[31mAB   Z\r\n") != NULL);

    /* An open byte row holding only zero-width escape bytes still
     * counts as open: the next rendered unit starts below it (boba
     * closes the row inside the extension), not on the same row. */
    h_send(h, tui_msg_stream_text(-1, "\x1b[32", 4));
    h_flush(h);
    h_send(h, tui_msg_stream_delta(0, "A\nB\n", 4));
    h_flush(h);
    out = h_read(h);
    /* extension: up-1 to the open row, CR to col 0, then the row
     * break; the pending "y" (lookahead) commits first, then "A" —
     * never R| rows on the escape's row */
    assert(strstr(out, "\x1b[1A\r\r\n") != NULL);
    assert(strstr(out, "\x1b[1A\rR|") == NULL);
    assert(strstr(out, "\r\nR|y\r\nR|A\r\n") != NULL);

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* No bare LF in anything boba writes                                  */
/* ------------------------------------------------------------------ */

static void test_no_bare_lf_anywhere(void)
{
    TuiStreamSpec streams[1] = { { "content" } };
    H *h = h_new(streams, NULL, 1);
    assert(h);

    static const char tool[] = "tool output\r\nwith\nmixed\rlines\n";
    h_send(h, tui_msg_stream_text(-1, tool, strlen(tool)));
    h_send(h, tui_msg_stream_delta(0, "one\ntwo\n", 8));
    h_send(h, tui_msg_stream_delta(0, "three", 5));
    h_send(h, tui_msg_stream_end(0));
    h_flush(h);
    const char *out = h_read(h);
    for (const char *p = out; *p; p++)
        assert(*p != '\n' || (p > out && p[-1] == '\r'));

    h_free(h);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("test_stream: streaming transcript component\n");
    RUN_TEST(test_default_blank_line_flow);
    RUN_TEST(test_coalescing_one_write_per_batch);
    RUN_TEST(test_fake_classifier_drives_component);
    RUN_TEST(test_byte_batch_extends_partial_row);
    RUN_TEST(test_unlabeled_fence_streams_verbatim);
    RUN_TEST(test_labeled_fence_line_granular);
    RUN_TEST(test_table_block_holds_to_finalize);
    RUN_TEST(test_tall_table_windows_to_tail);
    RUN_TEST(test_multi_stream_global_order);
    RUN_TEST(test_submit_finalizes_without_echo);
    RUN_TEST(test_clear_resets_and_emits_nothing);
    RUN_TEST(test_trim_after_commit);
    RUN_TEST(test_sink_tabs_wide_pad_attrs);
    RUN_TEST(test_resize_live_relayout);
    RUN_TEST(test_split_escape_sequence_passthrough);
    RUN_TEST(test_no_bare_lf_anywhere);
    printf("test_stream: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
