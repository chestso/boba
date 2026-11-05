/* tmux_textinput_multi.c - Mini-app for tmux-driven e2e tests.
 *
 * Mounts a multiline TuiTextInput at row 1 with a "> " prompt, runs the
 * standard bubbletea-style runtime, and follows SIGWINCH by forwarding
 * WINDOW_SIZE messages to the textinput (its FLOW geometry does the rest). No quit key — the
 * driving test tears down with `tmux kill-session`.
 */

#include <boba/component.h>
#include <boba/components/textinput.h>
#include <boba/msg.h>
#include <boba/runtime.h>

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
    tui_textinput_update(a->ti, tui_msg_focus());
    tui_textinput_set_prompt(a->ti, "> ");
    /* Alt-screen, input pinned to row 1, full width, and no taller than the
     * screen: FLOW derives width from the terminal and rows from what is
     * left below `row`. */
    TuiTextInputGeometry g = { .sizing = TUI_TEXTINPUT_FLOW,
                               .place = TUI_TEXTINPUT_AT_ROW,
                               .row = 1 };
    tui_textinput_set_geometry(a->ti, &g);
    return tui_init_result_none((TuiModel *)a);
}

static TuiUpdateResult app_update(TuiModel *m, TuiMsg msg)
{
    App *a = (App *)m;
    /* The input consumes TUI_MSG_WINDOW_SIZE itself (FLOW sizing), so the
     * resize reaches it in the same pass as any other message. */
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
