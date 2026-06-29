/* textview.c - Scrolling text view component implementation */

#include <boba/cmd.h>
#include <boba/components/textview.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTVIEW_INITIAL_CAP 4096
#define TEXTVIEW_TYPE_ID     (TUI_COMPONENT_TYPE_BASE + 2)

/* Create a new text view component */
TuiTextView *tui_textview_create(int height)
{
    TuiTextView *view = (TuiTextView *)malloc(sizeof(TuiTextView));
    if (!view)
        return NULL;

    memset(view, 0, sizeof(TuiTextView));
    view->base.type = TEXTVIEW_TYPE_ID;

    view->content_cap = TEXTVIEW_INITIAL_CAP;
    view->content = (char *)malloc(view->content_cap);
    if (!view->content) {
        free(view);
        return NULL;
    }
    view->content[0] = '\0';
    view->content_len = 0;

    view->height = height;
    view->terminal_height = 24;
    view->terminal_width = 80;
    view->auto_scroll = 1;

    return view;
}

/* Free text view component */
void tui_textview_free(TuiTextView *view)
{
    if (!view)
        return;
    if (view->content)
        free(view->content);
    free(view);
}

/* Ensure content buffer has enough capacity */
static int ensure_capacity(TuiTextView *view, size_t needed)
{
    if (view->content_cap >= needed)
        return 0;

    size_t new_cap = view->content_cap;
    while (new_cap < needed)
        new_cap *= 2;

    char *new_content = (char *)realloc(view->content, new_cap);
    if (!new_content)
        return -1;

    view->content = new_content;
    view->content_cap = new_cap;
    return 0;
}

/* Append text to the view */
void tui_textview_append(TuiTextView *view, const char *text, size_t len)
{
    if (!view || !text || len == 0)
        return;

    if (ensure_capacity(view, view->content_len + len + 1) < 0)
        return;

    memcpy(view->content + view->content_len, text, len);
    view->content_len += len;
    view->content[view->content_len] = '\0';

    /* Auto-scroll to bottom if enabled */
    if (view->auto_scroll) {
        view->scroll_offset = 0;
    }
}

/* Append a null-terminated string */
void tui_textview_append_str(TuiTextView *view, const char *text)
{
    if (text)
        tui_textview_append(view, text, strlen(text));
}

/* Clear all content */
void tui_textview_clear(TuiTextView *view)
{
    if (!view)
        return;
    view->content_len = 0;
    if (view->content)
        view->content[0] = '\0';
    view->scroll_offset = 0;
}

/* Set terminal dimensions */
void tui_textview_set_terminal_size(TuiTextView *view, int width, int height)
{
    if (!view)
        return;
    view->terminal_width = width;
    view->terminal_height = height;
}

/* Render the text view to output buffer */
void tui_textview_view(const TuiTextView *view, DynamicBuffer *out)
{
    if (!view || !out)
        return;

    /* For now, just output all content - caller handles positioning */
    if (view->content_len > 0) {
        dynamic_buffer_append(out, view->content, view->content_len);
    }
}

/* Get content length */
size_t tui_textview_len(const TuiTextView *view)
{
    return view ? view->content_len : 0;
}

/* Get content */
const char *tui_textview_content(const TuiTextView *view)
{
    return view ? view->content : NULL;
}

/* Update the text view with a message */
TuiUpdateResult tui_textview_update(TuiTextView *view, TuiMsg msg)
{
    if (!view)
        return tui_update_result_none();

    /* Handle window size message */
    if (msg.type == TUI_MSG_WINDOW_SIZE) {
        view->terminal_width = msg.data.size.width;
        view->terminal_height = msg.data.size.height;
    }

    return tui_update_result_none();
}

/* Component interface wrappers */
static TuiInitResult textview_init(void *config)
{
    const TuiTextViewConfig *cfg = (const TuiTextViewConfig *)config;
    int height = cfg ? cfg->height : 0;
    TuiModel *model = (TuiModel *)tui_textview_create(height);
    return tui_init_result_none(model);
}

static TuiUpdateResult textview_update(TuiModel *model, TuiMsg msg)
{
    return tui_textview_update((TuiTextView *)model, msg);
}

static TuiView textview_view_slot(const TuiModel *model, DynamicBuffer *out)
{
    tui_textview_view((const TuiTextView *)model, out);
    return tui_view_default(out);
}

static void textview_free(TuiModel *model)
{
    tui_textview_free((TuiTextView *)model);
}

/* Static component interface instance */
static const TuiComponent textview_component = {
    .init = textview_init,
    .update = textview_update,
    .view = textview_view_slot,
    .free = textview_free,
};

/* Get component interface for text view */
const TuiComponent *tui_textview_component(void) { return &textview_component; }
