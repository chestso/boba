/* tmux_textinput_multi.c - Mini-app for tmux-driven e2e tests.
 *
 * Mounts a multiline TuiTextInput at row 1 with a "> " prompt, runs the
 * standard bubbletea-style runtime, and tracks SIGWINCH by updating the
 * textinput's terminal_width on WINDOW_SIZE messages. No quit key — the
 * driving test tears down with `tmux kill-session`.
 */

#include <bloom-boba/component.h>
#include <bloom-boba/components/textinput.h>
#include <bloom-boba/msg.h>
#include <bloom-boba/runtime.h>

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
    return tui_init_result_none((TuiModel *)a);
}

static TuiUpdateResult app_update(TuiModel *m, TuiMsg msg)
{
    App *a = (App *)m;
    if (msg.type == TUI_MSG_WINDOW_SIZE) {
        tui_textinput_set_terminal_width(a->ti, msg.data.size.width);
        tui_textinput_set_terminal_row(a->ti, 1);
    }
    return tui_textinput_update(a->ti, msg);
}

static TuiView app_view(const TuiModel *m, DynamicBuffer *out)
{
    const App *a = (const App *)m;
    tui_textinput_view(a->ti, out);
    TuiView v = tui_view_default(out);
    v.alt_screen = 1;
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
    /* Alt-screen is now declared on TuiView (see app_view) — no longer
     * a runtime-config field. */
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
