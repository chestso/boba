/* tmux_textinput_inline.c - Mini-app for inline-mode soft-wrap e2e tests.
 *
 * Mirrors the nevermore/ditty REPL shape: a soft-wrapping multiline
 * TuiTextInput rendered in TUI_RENDER_INLINE (no alt screen, no
 * terminal_row). It prints a few "pre" lines before entering the runtime so
 * the live frame does not start at the top of the pane — the inline-row
 * drift the test guards against is only observable when the frame has room
 * to move up. No quit key — the driving test tears down with
 * `tmux kill-session`.
 */

#include <boba/component.h>
#include <boba/components/textinput.h>
#include <boba/msg.h>
#include <boba/runtime.h>

/* App uses the legacy convenience setter to focus the textinput at startup. */
_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")

#include <stdio.h>
#include <stdlib.h>

    typedef struct
{
    TuiModel base;
    TuiTextInput *ti;
} App;

static TuiInitResult app_init(void *cfg)
{
    (void)cfg;
    App *a = (App *)calloc(1, sizeof(App));
    if (!a)
        return tui_init_result_none(NULL);
    TuiTextInputConfig tcfg = { .multiline = 1 };
    a->ti = tui_textinput_create(&tcfg);
    if (!a->ti) {
        free(a);
        return tui_init_result_none(NULL);
    }
    tui_textinput_set_focus(a->ti, 1);
    tui_textinput_set_prompt(a->ti, "> ");
    tui_textinput_set_continuation_prompt(a->ti, "  ");
    tui_textinput_set_soft_wrap(a->ti, 1);
    return tui_init_result_none((TuiModel *)a);
}

static TuiUpdateResult app_update(TuiModel *m, TuiMsg msg)
{
    App *a = (App *)m;
    if (msg.type == TUI_MSG_WINDOW_SIZE)
        tui_textinput_set_terminal_width(a->ti, msg.data.size.width);
    return tui_textinput_update(a->ti, msg);
}

static TuiView app_view(const TuiModel *m, DynamicBuffer *out)
{
    const App *a = (const App *)m;
    tui_textinput_view(a->ti, out);
    TuiView v = tui_view_default(out);
    v.render_mode = TUI_RENDER_INLINE;
    v.bracketed_paste = 1;
    v.cursor = tui_textinput_cursor_pos(a->ti);
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

int main(void)
{
    /* Prefill the scrollback so the inline frame starts below the top edge;
     * a one-row-too-far cursor-up is then visible as the prompt drifting up
     * instead of clamping at row 0. */
    printf("pre1\npre2\npre3\npre4\npre5\npre6\n");
    fflush(stdout);

    TuiRuntimeConfig rc = {
        .raw_mode = 1,
        .output = stdout,
    };
    TuiRuntime *rt =
        tui_runtime_create((TuiComponent *)&app_component, NULL, &rc);
    if (!rt) {
        fprintf(stderr, "ERROR: failed to create runtime\n");
        return 1;
    }
    int rc_run = tui_runtime_run(rt);
    tui_runtime_free(rt);
    return rc_run < 0 ? 1 : 0;
}
