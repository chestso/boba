/* tmux_stream_cr.c - Mini-app for the carriage-return e2e test.
 *
 * An inline frame (one prompt row) under a streaming transcript. Each key
 * posts one piece of terminal-style output to the system stream with
 * TUI_STREAM_TERMINAL, so the driving test can watch a real
 * terminal emulator (tmux) draw a progress meter in place:
 *
 *   a  "10%"               b  "\r50%"          c  "\r100%\n"
 *   w  100 'w' (wraps)     s  "\rshort"        n  "\n"
 *   p  "\r"                z  "\r\x1b[32mgreen"
 *   l  "legacy\rbreak\n"   through the plain constructor: CR is a break
 *
 * No quit key; the test tears down with `tmux kill-session`.
 */

#include <boba/cmd.h>
#include <boba/component.h>
#include <boba/dynamic_buffer.h>
#include <boba/msg.h>
#include <boba/runtime.h>
#include <boba/stream.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    TuiModel base;
    TuiTranscript *t;
    int width;
} App;

static TuiInitResult app_init(void *cfg)
{
    App *a = (App *)calloc(1, sizeof(App));
    if (!a)
        return tui_init_result_none(NULL);
    a->t = (TuiTranscript *)cfg;
    a->width = 80;
    return tui_init_result_none((TuiModel *)a);
}

static void post_cr(App *a, const char *text)
{
    TuiMsg m = tui_msg_stream_text(-1, text, strlen(text),
                                   TUI_STREAM_TERMINAL);
    tui_transcript_update(a->t, m);
    tui_msg_free(&m);
}

static TuiUpdateResult app_update(TuiModel *m, TuiMsg msg)
{
    App *a = (App *)m;
    if (msg.type == TUI_MSG_WINDOW_SIZE) {
        a->width = msg.data.size.width;
        return tui_transcript_update(a->t, msg);
    }
    if (msg.type != TUI_MSG_KEY_PRESS)
        return tui_update_result_none();

    switch (msg.data.key.rune) {
    case 'a':
        post_cr(a, "10%");
        break;
    case 'b':
        post_cr(a, "\r50%");
        break;
    case 'c':
        post_cr(a, "\r100%\n");
        break;
    case 'w':
        post_cr(a, "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww"
                   "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww");
        break;
    case 's':
        post_cr(a, "\rshort");
        break;
    case 'n':
        post_cr(a, "\n");
        break;
    case 'p':
        post_cr(a, "\r");
        break;
    case 'z':
        post_cr(a, "\r\x1b[32mgreen\x1b[0m");
        break;
    case 'r':
        /* a colour left OPEN at the end of the row: the transcript closes
         * its writes at default, so the frame below must not inherit it */
        post_cr(a, "\x1b[31mbleed");
        break;
    case 'l':
    {
        TuiMsg t = tui_msg_stream_text(-1, "legacy\rbreak\n", 13, TUI_STREAM_LINES);
        tui_transcript_update(a->t, t);
        tui_msg_free(&t);
        break;
    }
    default:
        break;
    }
    return tui_update_result_none();
}

static TuiView app_view(const TuiModel *m, DynamicBuffer *out)
{
    const App *a = (const App *)m;
    tui_transcript_view(a->t, out, a->width, 4);
    /* A frame row owns its line: \r + EL, as every component's view does,
     * so a repaint over the same row replaces it. */
    dynamic_buffer_append(out, "\r\x1b[K> ready", 12);
    TuiView v = tui_view_default(out);
    v.render_mode = TUI_RENDER_INLINE;
    return v;
}

static void app_free(TuiModel *m)
{
    App *a = (App *)m;
    if (a) {
        tui_transcript_free(a->t);
        free(a);
    }
}

static const TuiComponent app_component = {
    .init = app_init,
    .update = app_update,
    .view = app_view,
    .free = app_free,
};

int main(void)
{
    /* A few rows above the frame, so a rewind that goes one row too far
     * is visible as damage to them instead of clamping at the top edge. */
    printf("pre1\npre2\npre3\npre4\n");
    fflush(stdout);

    TuiTranscriptConfig tc = { 0 };
    TuiTranscript *t = tui_transcript_create(&tc);
    if (!t)
        return 1;

    TuiRuntimeConfig rc = { .raw_mode = 1, .output = stdout };
    TuiRuntime *rt = tui_runtime_create((TuiComponent *)&app_component, t, &rc);
    if (!rt) {
        tui_transcript_free(t);
        fprintf(stderr, "ERROR: failed to create runtime\n");
        return 1;
    }
    tui_runtime_set_transcript(rt, t);
    int rc_run = tui_runtime_run(rt);
    tui_runtime_free(rt);
    return rc_run < 0 ? 1 : 0;
}
