/* tmux_textinput_overflow.c - Mini-app for tmux-driven overflow tests.
 *
 * A single TuiTextInput pre-filled with more lines than the screen has,
 * sized by its own FLOW geometry: the input follows the window, so the
 * driving test can shrink and grow the terminal and assert that the frame
 * never leaves the screen.
 *
 * Flags (parsed in main, handed to init through the component config):
 *
 *   --alt             absolute placement at row 1 (alt screen)
 *   --inline          relative placement (TUI_RENDER_INLINE)
 *   --lines N         prefill N logical lines, labelled n00..nN
 *   --rows N          cap the painted rows (FIXED-ish behaviour inside FLOW)
 *   --wrap            soft-wrap instead of clipping
 *   --reserve-above N rows the app keeps above the input (status line)
 *   --reserve-below N rows the app keeps below it (hint line)
 *
 * No quit key — the driving test tears down with `tmux kill-session`.
 */

#include <boba/component.h>
#include <boba/components/textinput.h>
#include <boba/msg.h>
#include <boba/runtime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int alt;
    int lines;
    int rows;
    int wrap;
    int reserve_above;
    int reserve_below;
} AppOpts;

typedef struct
{
    TuiModel base;
    TuiTextInput *ti;
    AppOpts opts;
} App;

/* Prefill `lines` logical lines named n00, n01, ... so a capture can say
 * exactly which rows are on screen. */
static void prefill(App *a)
{
    size_t cap = (size_t)a->opts.lines * 8 + 8;
    char *s = (char *)malloc(cap);
    if (!s)
        return;
    s[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < a->opts.lines; i++) {
        int n = snprintf(s + used, cap - used, i ? "\nn%02d" : "n%02d", i);
        used += (size_t)n;
    }
    tui_textinput_set_text(a->ti, s);
    free(s);
}

static TuiInitResult app_init(void *cfg)
{
    const AppOpts *opts = (const AppOpts *)cfg;

    App *a = (App *)calloc(1, sizeof(App));
    if (!a)
        return tui_init_result_none(NULL);
    a->opts = *opts;

    TuiTextInputConfig tcfg = {
        .prompt = "> ",
        .multiline = 1,
        .wrap = opts->wrap ? TUI_TEXTINPUT_WRAP : TUI_TEXTINPUT_CLIP,
        .geometry = {
            .sizing = TUI_TEXTINPUT_FLOW,
            .place = opts->alt ? TUI_TEXTINPUT_AT_ROW : TUI_TEXTINPUT_INLINE,
            .row = opts->alt ? 1 : 0,
            .rows = opts->rows,
            .reserve_above = opts->reserve_above,
            .reserve_below = opts->reserve_below,
        },
    };
    a->ti = tui_textinput_create(&tcfg);
    if (!a->ti) {
        free(a);
        return tui_init_result_none(NULL);
    }
    tui_textinput_set_continuation_prompt(a->ti, "  ");
    tui_textinput_update(a->ti, tui_msg_focus());
    prefill(a);

    return tui_init_result_none((TuiModel *)a);
}

static TuiUpdateResult app_update(TuiModel *m, TuiMsg msg)
{
    App *a = (App *)m;

    /* The FLOW recipe, and the whole of it: forward the message. The input
     * consumes TUI_MSG_WINDOW_SIZE itself and derives its width and height
     * from the window, so nothing here has to know the terminal size. */
    return tui_textinput_update(a->ti, msg);
}

static TuiView app_view(const TuiModel *m, DynamicBuffer *out)
{
    const App *a = (const App *)m;
    tui_textinput_view(a->ti, out);

    TuiView v = tui_view_default(out);
    v.cursor = tui_textinput_cursor_pos(a->ti);

    if (a->opts.alt) {
        v.alt_screen = 1;
    } else {
        v.render_mode = TUI_RENDER_INLINE;
        v.bracketed_paste = 1;
    }
    return v;
}

static void app_free(TuiModel *m)
{
    App *a = (App *)m;
    if (a) {
        tui_textinput_free(a->ti);
        free(a);
    }
}

static const TuiComponent app_component = {
    .init = app_init,
    .update = app_update,
    .view = app_view,
    .free = app_free,
};

int main(int argc, char **argv)
{
    static AppOpts opts = { .alt = 0, .lines = 40, .rows = 0 };
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--alt"))
            opts.alt = 1;
        else if (!strcmp(argv[i], "--inline"))
            opts.alt = 0;
        else if (!strcmp(argv[i], "--wrap"))
            opts.wrap = 1;
        else if (!strcmp(argv[i], "--lines") && i + 1 < argc)
            opts.lines = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc)
            opts.rows = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reserve-above") && i + 1 < argc)
            opts.reserve_above = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reserve-below") && i + 1 < argc)
            opts.reserve_below = atoi(argv[++i]);
    }

    TuiRuntimeConfig rc = {
        .raw_mode = 1,
        .output = stdout,
    };
    TuiRuntime *rt =
        tui_runtime_create((TuiComponent *)&app_component, &opts, &rc);
    if (!rt) {
        fprintf(stderr, "ERROR: failed to create runtime\n");
        return 1;
    }
    int rc_run = tui_runtime_run(rt);
    tui_runtime_free(rt);
    return rc_run < 0 ? 1 : 0;
}
