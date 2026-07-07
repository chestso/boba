/* list_popup.h - Popup list selector component for boba
 *
 * A scrollable, keyboard-navigable popup list designed for tab completion
 * in inline REPLs. Renders below the textinput with a rounded border,
 * highlighted selection, and optional title.
 *
 * The component is purely presentational — the parent component handles
 * key routing and calls the navigation/show/hide functions. The popup
 * does not process TuiMsg or emit TuiCmd on its own.
 */

#ifndef BOBA_LIST_POPUP_H
#define BOBA_LIST_POPUP_H

#include "../component.h"
#include "../dynamic_buffer.h"
#include "../style.h"

#include <stddef.h>

/* Component type ID */
#define TUI_LIST_POPUP_TYPE_ID (TUI_COMPONENT_TYPE_BASE + 2)

/* Popup model */
typedef struct TuiListPopup
{
    TuiModel base;

    /* Items (owned) */
    char **items; /* Array of owned strings */
    int item_count;
    int capacity;

    /* Selection + scrolling */
    int selected;      /* Highlighted index, 0-based; -1 = none */
    int scroll_offset; /* First visible item index */

    /* Layout */
    int width;       /* Total popup width (border inclusive), 0 = auto */
    int max_visible; /* Max visible rows before scrolling (0 = auto) */
    int terminal_width;
    int terminal_height;

    /* State */
    int visible;
    int word_start; /* Byte offset in parent textinput */

    /* Title (owned, may be NULL) */
    char *title;

    /* Filter prefix for title display (owned, may be NULL) */
    char *filter_prefix;

    /* Colors (all default to NONE = inherit terminal defaults).
     * Set via tui_list_popup_set_colors() using CharmTone values. */
    TuiColor border_color;
    TuiColor title_color;
    TuiColor selected_bg;
    TuiColor selected_fg;
    TuiColor selected_marker_color;
    TuiColor item_color;
} TuiListPopup;

/* Create a new popup. Returns NULL on failure. */
TuiListPopup *tui_list_popup_create(void);

/* Free popup and all owned memory. NULL-safe. */
void tui_list_popup_free(TuiListPopup *popup);

/* Set items from a NULL-terminated string array. Copies the strings;
 * caller retains ownership of the source array. Passing NULL/0 clears. */
void tui_list_popup_set_items(TuiListPopup *popup,
                              const char *const *texts, int count);

/* Clear all items and reset selection. */
void tui_list_popup_clear(TuiListPopup *popup);

/* Show popup. word_start is the byte offset in the parent textinput
 * where the completion word begins. Resets selection to first item. */
void tui_list_popup_show(TuiListPopup *popup, int word_start);

/* Hide popup. Does not clear items. */
void tui_list_popup_hide(TuiListPopup *popup);

/* Returns 1 if popup is visible. */
int tui_list_popup_is_visible(const TuiListPopup *popup);

/* Navigation. Returns 1 if selection changed, 0 otherwise.
 * Down/Up wrap around. Page moves by max_visible rows. */
int tui_list_popup_move_down(TuiListPopup *popup);
int tui_list_popup_move_up(TuiListPopup *popup);
int tui_list_popup_move_page_down(TuiListPopup *popup);
int tui_list_popup_move_page_up(TuiListPopup *popup);
int tui_list_popup_move_top(TuiListPopup *popup);
int tui_list_popup_move_bottom(TuiListPopup *popup);

/* Query the currently selected item. Returns NULL if no selection. */
const char *tui_list_popup_selected_text(const TuiListPopup *popup);
int tui_list_popup_selected_index(const TuiListPopup *popup);

/* Get the word_start set by show(). Returns -1 if never shown. */
int tui_list_popup_word_start(const TuiListPopup *popup);

/* Configuration */
void tui_list_popup_set_size(TuiListPopup *popup, int width, int max_visible);
void tui_list_popup_set_terminal_size(TuiListPopup *popup, int w, int h);
void tui_list_popup_set_title(TuiListPopup *popup, const char *title);
void tui_list_popup_set_filter(TuiListPopup *popup, const char *prefix);

/* Set colors for popup elements. Pass TUI_COLOR_NONE for any element
 * to use terminal defaults. All arguments are copied (no allocation). */
void tui_list_popup_set_colors(TuiListPopup *popup, TuiColor border_color,
                               TuiColor title_color, TuiColor selected_bg,
                               TuiColor selected_fg,
                               TuiColor selected_marker_color,
                               TuiColor item_color);

/* Render popup to output buffer. Produces no output if not visible. */
void tui_list_popup_view(const TuiListPopup *popup, DynamicBuffer *out);

/* Component interface for generic use */
const TuiComponent *tui_list_popup_component(void);

#endif /* BOBA_LIST_POPUP_H */
