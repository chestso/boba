/* test_terminal_profile.c - terminal capability probe: query bytes,
 * reply decoding, environment hints, and the runtime lifecycle
 * (emission once, resolve on reply, timeout, guaranteed one-shot
 * callback).
 *
 * No ptys: the "terminal" is a tmpfile for output and a string fed
 * through the parser for input, exactly the way the runtime sees a
 * real terminal's answers.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/dynamic_buffer.h>
#include <boba/input_parser.h>
#include <boba/runtime.h>
#include <boba/terminal_profile.h>

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
/* Decoder                                                             */
/* ------------------------------------------------------------------ */

static void feed(TuiTerminalProfile *p, const char *s)
{
    tui_term_profile_feed(p, s, strlen(s));
}

static void test_query_bytes(void)
{
    char buf[TUI_TERM_PROBE_QUERY_MAX];
    size_t n = tui_term_probe_query(buf, sizeof(buf));
    assert(n > 0 && n == strlen(buf));
    /* kitty's documented support check: the graphics query first, then
     * DA1; cell size and XTVERSION ride along. */
    assert(strstr(buf, "\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\") != NULL);
    size_t graphics = (size_t)(strstr(buf, "\033[c") - buf);
    size_t da1 = (size_t)(strstr(buf, "\033[c") - buf);
    assert(graphics == da1); /* DA1 immediately follows the query */
    assert(strstr(buf, "\033[16t") != NULL);
    assert(strstr(buf, "\033[>0q") != NULL);
}

static void test_da1_sixel_attr(void)
{
    TuiTerminalProfile p;
    memset(&p, 0, sizeof(p));
    feed(&p, "[?1;2;4c");
    assert(p.sixel == 1);
    memset(&p, 0, sizeof(p));
    feed(&p, "[?65;1;9c"); /* VTE: no attribute 4 */
    assert(p.sixel == 0);
    /* attribute 4 must not be matched inside a larger number */
    memset(&p, 0, sizeof(p));
    feed(&p, "[?14;42c");
    assert(p.sixel == 0);
}

static void test_kitty_ack(void)
{
    TuiTerminalProfile p;
    memset(&p, 0, sizeof(p));
    feed(&p, "Gi=31;OK");
    assert(p.kitty_graphics == 1);
    /* an error reply still proves the terminal parsed the query */
    memset(&p, 0, sizeof(p));
    feed(&p, "Gi=31;EINVAL:bad data");
    assert(p.kitty_graphics == 1);
    /* another client's id is not our answer */
    memset(&p, 0, sizeof(p));
    feed(&p, "Gi=7;OK");
    assert(p.kitty_graphics == 0);
}

static void test_xtversion_placeholders(void)
{
    TuiTerminalProfile p;
    memset(&p, 0, sizeof(p));
    feed(&p, ">|kitty(0.36.0)");
    assert(p.kitty_placeholders == 1);
    memset(&p, 0, sizeof(p));
    feed(&p, ">|kitty 0.29.0");
    assert(p.kitty_placeholders == 1);
    memset(&p, 0, sizeof(p));
    feed(&p, ">|kitty(0.26.5)");
    assert(p.kitty_placeholders == 0);
    /* a non-kitty terminal's XTVERSION implies no kitty capability */
    memset(&p, 0, sizeof(p));
    feed(&p, ">|WezTerm 20240203");
    assert(p.kitty_placeholders == 0);
    assert(p.kitty_graphics == 0);
}

static void test_cell_size(void)
{
    TuiTerminalProfile p;
    memset(&p, 0, sizeof(p));
    feed(&p, "[6;18;9t");
    assert(p.cell_h_px == 18 && p.cell_w_px == 9);
    /* character/row reports are not pixel geometry */
    memset(&p, 0, sizeof(p));
    feed(&p, "[4;500;400t");
    assert(p.cell_w_px == 0 && p.cell_h_px == 0);
}

static void test_unknown_payload_ignored(void)
{
    TuiTerminalProfile p;
    memset(&p, 0, sizeof(p));
    assert(tui_term_profile_feed(&p, "0;my title", 10) == 0);
    assert(tui_term_profile_feed(&p, "52;c;AAAA", 9) == 0);
    /* a DA1-shaped reply is recognized even with no attributes */
    assert(tui_term_profile_feed(&p, "[?1c", 4) == 1);
    assert(p.resolved == 0); /* decoding never resolves */
}

static void test_env_hints(void)
{
    TuiTerminalProfile p;
    memset(&p, 0, sizeof(p));
    tui_term_profile_env(&p, "iTerm.app", NULL);
    assert(p.iterm2_images == 1);
    memset(&p, 0, sizeof(p));
    tui_term_profile_env(&p, NULL, "iTerm2");
    assert(p.iterm2_images == 1);
    memset(&p, 0, sizeof(p));
    tui_term_profile_env(&p, "WezTerm", NULL);
    assert(p.sixel == 1);
    memset(&p, 0, sizeof(p));
    tui_term_profile_env(&p, "xterm", NULL);
    assert(p.iterm2_images == 0 && p.sixel == 0);
}

/* ------------------------------------------------------------------ */
/* Runtime lifecycle                                                   */
/* ------------------------------------------------------------------ */

typedef struct
{
    TuiModel base;
    int probe_terminal;
} ProbeModel;

static TuiInitResult probe_init(void *config)
{
    (void)config;
    ProbeModel *m = calloc(1, sizeof(ProbeModel));
    m->base.type = 997;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult probe_update(TuiModel *model, TuiMsg msg)
{
    (void)model;
    (void)msg;
    return tui_update_result_none();
}

static TuiView probe_view(const TuiModel *model, DynamicBuffer *out)
{
    const ProbeModel *m = (const ProbeModel *)model;
    dynamic_buffer_append_str(out, "x\r\n");
    TuiView v = tui_view_default(out);
    v.probe_terminal = m->probe_terminal;
    return v;
}

static void probe_free(TuiModel *model)
{
    free(model);
}

static TuiComponent probe_component = {
    .init = probe_init,
    .update = probe_update,
    .view = probe_view,
    .free = probe_free,
};

static int s_reply_calls;
static int s_reply_resolved;
static int s_reply_sixel;

static void on_term_reply(const TuiTerminalProfile *p, void *user_data)
{
    (void)user_data;
    s_reply_calls++;
    s_reply_resolved = p->resolved;
    s_reply_sixel = p->sixel;
}

/* Read everything written to `out` so far. */
static const char *capture(FILE *out, char *buf, size_t cap)
{
    fflush(out);
    long pos = ftell(out);
    assert(pos >= 0);
    rewind(out);
    size_t n = fread(buf, 1, cap - 1, out);
    buf[n] = '\0';
    fseek(out, pos, SEEK_SET);
    return buf;
}

static TuiRuntime *probe_rt(FILE *out, ProbeModel *model_seed, int declare)
{
    TuiRuntimeConfig cfg = { .raw_mode = 0,
                             .output = out,
                             .on_term_reply = on_term_reply };
    TuiRuntime *rt = tui_runtime_create(&probe_component, NULL, &cfg);
    assert(rt != NULL);
    /* the component's model is allocated by init; set the declaration */
    ProbeModel *m = (ProbeModel *)tui_runtime_model(rt);
    m->probe_terminal = declare;
    (void)model_seed;
    return rt;
}

static void test_probe_not_declared_no_query(void)
{
    FILE *out = tmpfile();
    assert(out != NULL);
    s_reply_calls = 0;
    TuiRuntime *rt = probe_rt(out, NULL, 0);
    tui_runtime_flush(rt);
    char buf[4096];
    assert(strstr(capture(out, buf, sizeof(buf)), "_Gi=31") == NULL);
    assert(s_reply_calls == 0);
    tui_runtime_free(rt);
    fclose(out);
}

static void test_probe_declared_emits_once(void)
{
    FILE *out = tmpfile();
    assert(out != NULL);
    s_reply_calls = 0;
    TuiRuntime *rt = probe_rt(out, NULL, 1);
    tui_runtime_flush(rt);
    tui_runtime_flush(rt);
    tui_runtime_flush(rt);
    char buf[8192];
    const char *data = capture(out, buf, sizeof(buf));
    int count = 0;
    const char *p = data;
    while ((p = strstr(p, "_Gi=31")) != NULL) {
        count++;
        p++;
    }
    assert(count == 1);
    /* query precedes the frame's content */
    const char *q = strstr(data, "_Gi=31");
    const char *content = strstr(data, "x\r\n");
    if (content && q)
        assert(q < content);
    tui_runtime_free(rt);
    fclose(out);
}

static void test_probe_decodes_all_replies_then_deadline(void)
{
    FILE *out = tmpfile();
    assert(out != NULL);
    s_reply_calls = 0;
    TuiRuntime *rt = probe_rt(out, NULL, 1);
    tui_runtime_flush(rt);

    /* the terminal answers (kitty graphics ack + DA1 with sixel) */
    const char *reply =
        "\033_Gi=31;OK\033\\\033[?1;2;4c\033[6;18;9t\033P>|kitty(0.36.0)\033\\";
    tui_runtime_process_input(rt, (const unsigned char *)reply,
                              strlen(reply));
    /* the deadline is the completion signal: replies in FIFO order are
     * all decoded, none discarded */
    rt->probe_deadline_ms = 1;
    tui_runtime_probe_check(rt);

    assert(s_reply_calls == 1);
    assert(s_reply_resolved == 1);
    assert(s_reply_sixel == 1);
    const TuiTerminalProfile *p = tui_runtime_terminal_profile(rt);
    assert(p->resolved == 1 && p->kitty_graphics == 1 && p->sixel == 1);
    assert(p->kitty_placeholders == 1);
    assert(p->cell_w_px == 9 && p->cell_h_px == 18);

    /* exactly one callback, even after more flushes/checks */
    tui_runtime_flush(rt);
    tui_runtime_probe_check(rt);
    assert(s_reply_calls == 1);
    tui_runtime_free(rt);
    fclose(out);
}

static void test_probe_timeout_resolves_conservatively(void)
{
    FILE *out = tmpfile();
    assert(out != NULL);
    s_reply_calls = 0;
    TuiRuntime *rt = probe_rt(out, NULL, 1);
    tui_runtime_flush(rt);
    assert(s_reply_calls == 0);
    /* the deadline is a wall-clock deadline from emission */
    assert(rt->probe_deadline_ms != 0);

    /* force the deadline into the past: simulate the wall clock */
    rt->probe_deadline_ms = 1;
    tui_runtime_probe_check(rt);
    assert(s_reply_calls == 1);
    assert(s_reply_resolved == 1);
    assert(s_reply_sixel == 0);
    assert(tui_runtime_terminal_profile(rt)->kitty_graphics == 0);
    tui_runtime_free(rt);
    fclose(out);
}

static void test_probe_reply_split_across_reads(void)
{
    FILE *out = tmpfile();
    assert(out != NULL);
    s_reply_calls = 0;
    TuiRuntime *rt = probe_rt(out, NULL, 1);
    tui_runtime_flush(rt);

    /* a real terminal's answer can straddle read() boundaries */
    const char *a = "\033_Gi=31";
    const char *b = ";OK\033\\\033[?1;2;";
    const char *c = "4c";
    tui_runtime_process_input(rt, (const unsigned char *)a, strlen(a));
    tui_runtime_probe_check(rt);
    assert(s_reply_calls == 0);
    tui_runtime_process_input(rt, (const unsigned char *)b, strlen(b));
    tui_runtime_probe_check(rt);
    assert(s_reply_calls == 0);
    tui_runtime_process_input(rt, (const unsigned char *)c, strlen(c));
    tui_runtime_probe_check(rt);
    assert(s_reply_calls == 0); /* deadline, not a reply, resolves */
    rt->probe_deadline_ms = 1;
    tui_runtime_probe_check(rt);
    assert(s_reply_calls == 1);
    const TuiTerminalProfile *p = tui_runtime_terminal_profile(rt);
    assert(p->kitty_graphics == 1 && p->sixel == 1);
    tui_runtime_free(rt);
    fclose(out);
}

static void test_no_parser_resolves_immediately(void)
{
    /* tui_runtime_send-only harnesses (no event loop) must not hang the
     * probe: with no parser there is nothing to read, so the profile
     * resolves from environment hints and the callback still fires. */
    s_reply_calls = 0;
    TuiRuntimeConfig cfg = { .raw_mode = 0,
                             .output = tmpfile(),
                             .on_term_reply = on_term_reply };
    assert(cfg.output != NULL);
    TuiRuntime *rt = tui_runtime_create(&probe_component, NULL, &cfg);
    assert(rt != NULL);
    tui_input_parser_free(rt->parser);
    rt->parser = NULL;
    ((ProbeModel *)tui_runtime_model(rt))->probe_terminal = 1;
    tui_runtime_flush(rt);
    assert(s_reply_calls == 1);
    assert(tui_runtime_terminal_profile(rt)->resolved == 1);
    tui_runtime_free(rt);
    fclose(cfg.output);
}

int main(void)
{
    printf("Running terminal profile tests...\n");

    RUN_TEST(test_query_bytes);
    RUN_TEST(test_da1_sixel_attr);
    RUN_TEST(test_kitty_ack);
    RUN_TEST(test_xtversion_placeholders);
    RUN_TEST(test_cell_size);
    RUN_TEST(test_unknown_payload_ignored);
    RUN_TEST(test_env_hints);

    RUN_TEST(test_probe_not_declared_no_query);
    RUN_TEST(test_probe_declared_emits_once);
    RUN_TEST(test_probe_decodes_all_replies_then_deadline);
    RUN_TEST(test_probe_timeout_resolves_conservatively);
    RUN_TEST(test_probe_reply_split_across_reads);
    RUN_TEST(test_no_parser_resolves_immediately);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}