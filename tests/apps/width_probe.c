/* width_probe.c - Mini-app for the width-model end-to-end test.
 *
 * Prints the model the runtime installed (not what the environment would
 * resolve to in isolation) and the width it measures for a
 * text-presentation pair, then idles. The tmux test drives it inside a
 * multiplexer, where the model must be the grid regardless of the
 * terminal outside: tmux owns the columns.
 *
 * The widths are boba's own measurement, so the pane line is a readable
 * trace of what the runtime would lay out; the terminal's rendering of
 * it is verified by eye.
 */

#include <boba/component.h>
#include <boba/dynamic_buffer.h>
#include <boba/msg.h>
#include <boba/runtime.h>
#include <boba/unicode.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    TuiModel base;
    TuiWidthModel model; /* as installed for this runtime */
} Probe;

static TuiInitResult probe_init(void *config)
{
    (void)config;
    Probe *p = (Probe *)calloc(1, sizeof(Probe));
    if (!p)
        return tui_init_result_none(NULL);
    p->base.type = 996;
    p->model = tui_width_model();
    return tui_init_result_none((TuiModel *)p);
}

static TuiUpdateResult probe_update(TuiModel *model, TuiMsg msg)
{
    (void)model;
    (void)msg;
    return tui_update_result_none();
}

static TuiView probe_view(const TuiModel *model, DynamicBuffer *out)
{
    const Probe *p = (const Probe *)model;
    char line[128];
    snprintf(line, sizeof(line), "model=%s bolt_vs15=%d bolt_wide=%d\r\n",
             tui_width_model_name(p->model),
             tui_utf8_display_width("\xe2\x9a\xa1\xef\xb8\x8e"), /* ⚡︎ */
             tui_utf8_display_width("\xe2\x9a\xa1"));            /* ⚡ */
    dynamic_buffer_append_str(out, line);

    TuiView v = tui_view_default(out);
    v.render_mode = TUI_RENDER_INLINE;
    return v;
}

static void probe_free(TuiModel *model)
{
    free(model);
}

static const TuiComponent probe_component = {
    .init = probe_init,
    .update = probe_update,
    .view = probe_view,
    .free = probe_free,
};

int main(void)
{
    TuiRuntimeConfig cfg = { .raw_mode = 1, .output = stdout };
    TuiRuntime *rt =
        tui_runtime_create((TuiComponent *)&probe_component, NULL, &cfg);
    if (!rt) {
        fprintf(stderr, "ERROR: failed to create runtime\n");
        return 1;
    }
    int rc = tui_runtime_run(rt);
    tui_runtime_free(rt);
    return rc < 0 ? 1 : 0;
}
