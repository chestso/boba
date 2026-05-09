/* tmux_focus_swap.c - Mini-app for tmux-driven focus-cycle e2e tests.
 *
 * Composes a single-line TuiTextInput (row 1, prompt "> ") and a
 * TuiViewport (rows 3..term_h-1, pre-populated with three labelled
 * lines). A 1-line focus indicator (`[INPUT]` or `[VIEW]`) is rendered
 * on row term_h.
 *
 * Demonstrates the canonical Bubbletea two-child composition pattern:
 * the parent intercepts TUI_KEY_TAB (any modifier) for focus cycling
 * BEFORE forwarding, and forwards everything else to the focused
 * child. There is no return-channel "consumed" / "passthrough" signal
 * — the parent simply doesn't forward keys it wants for itself.
 *
 * No quit key — the driving test tears down with `tmux kill-session`.
 */

#include <bloom-boba/cmd.h>
#include <bloom-boba/component.h>
#include <bloom-boba/components/textinput.h>
#include <bloom-boba/components/viewport.h>
#include <bloom-boba/dynamic_buffer.h>
#include <bloom-boba/msg.h>
#include <bloom-boba/runtime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    TuiModel base;
    TuiTextInput *ti;
    TuiViewport *vp;
    int focus_idx; /* 0 = textinput, 1 = viewport */
    int term_w;
    int term_h;
} App;

static const char *const SAMPLE_LINES = "alpha\nbeta\ngamma\n";

static TuiInitResult app_init(void *cfg)
{
    (void)cfg;
    App *a = (App *)calloc(1, sizeof(App));
    if (!a)
        return tui_init_result_none(NULL);

    TuiTextInputConfig tcfg = { .prompt = "> ", .multiline = 0 };
    a->ti = tui_textinput_create(&tcfg);
    a->vp = tui_viewport_create();
    if (!a->ti || !a->vp) {
        if (a->ti)
            tui_textinput_free(a->ti);
        if (a->vp)
            tui_viewport_free(a->vp);
        free(a);
        return tui_init_result_none(NULL);
    }

    tui_textinput_set_focus(a->ti, 1);
    tui_textinput_set_terminal_row(a->ti, 1);

    tui_viewport_append(a->vp, SAMPLE_LINES, strlen(SAMPLE_LINES));
    tui_viewport_set_render_position(a->vp, 3, 1);

    a->focus_idx = 0;
    return tui_init_result_none((TuiModel *)a);
}

static void swap_focus(App *a)
{
    const TuiComponent *out_c = (a->focus_idx == 0)
                                    ? tui_textinput_component()
                                    : tui_viewport_component();
    TuiModel *out_m =
        (a->focus_idx == 0) ? (TuiModel *)a->ti : (TuiModel *)a->vp;

    a->focus_idx ^= 1;

    const TuiComponent *in_c = (a->focus_idx == 0) ? tui_textinput_component()
                                                   : tui_viewport_component();
    TuiModel *in_m =
        (a->focus_idx == 0) ? (TuiModel *)a->ti : (TuiModel *)a->vp;

    TuiUpdateResult r;
    r = out_c->update(out_m, tui_msg_blur());
    if (r.cmd)
        tui_cmd_free(r.cmd);
    r = in_c->update(in_m, tui_msg_focus());
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

static TuiUpdateResult app_update(TuiModel *m, TuiMsg msg)
{
    App *a = (App *)m;

    if (msg.type == TUI_MSG_WINDOW_SIZE) {
        a->term_w = msg.data.size.width;
        a->term_h = msg.data.size.height;
        tui_textinput_set_terminal_width(a->ti, a->term_w);
        /* row 1 input, row 2 blank, rows 3..term_h-1 viewport, row term_h indicator */
        int vp_h = a->term_h - 3;
        if (vp_h < 1)
            vp_h = 1;
        tui_viewport_set_size(a->vp, a->term_w, vp_h);
        return tui_update_result_none();
    }

    /* Parent intercepts Tab (any modifiers) for focus cycling. The
     * focused child never sees this key — Elm/Bubbletea routing is
     * the parent's responsibility. */
    if (msg.type == TUI_MSG_KEY_PRESS && msg.data.key.key == TUI_KEY_TAB) {
        swap_focus(a);
        return tui_update_result_none();
    }

    /* Forward everything else to the focused child. */
    const TuiComponent *c = (a->focus_idx == 0) ? tui_textinput_component()
                                                : tui_viewport_component();
    TuiModel *child_m =
        (a->focus_idx == 0) ? (TuiModel *)a->ti : (TuiModel *)a->vp;
    return c->update(child_m, msg);
}

static void app_view(const TuiModel *m, DynamicBuffer *out)
{
    const App *a = (const App *)m;

    tui_textinput_view(a->ti, out);
    tui_viewport_view(a->vp, out);

    /* Indicator on row term_h, column 1, regardless of where prior
     * renders left the cursor. */
    int row = a->term_h > 0 ? a->term_h : 1;
    dynamic_buffer_append_printf(out, "\x1b[%d;1H%s", row,
                                 a->focus_idx == 0 ? "[INPUT]" : "[VIEW]");
}

static void app_free(TuiModel *m)
{
    App *a = (App *)m;
    if (!a)
        return;
    if (a->ti)
        tui_textinput_free(a->ti);
    if (a->vp)
        tui_viewport_free(a->vp);
    free(a);
}

static const TuiComponent app_component = {
    .init = app_init,
    .update = app_update,
    .view = app_view,
    .free = app_free,
};

int main(void)
{
    TuiRuntimeConfig rc = {
        .use_alternate_screen = 1,
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
