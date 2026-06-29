/* component.h - Component interface for boba TUI library
 *
 * Components follow the Elm Architecture pattern:
 * - init: Create initial model state
 * - update: Handle messages and update state
 * - view: Render current state to output buffer
 * - free: Cleanup resources
 */

#ifndef BOBA_COMPONENT_H
#define BOBA_COMPONENT_H

#include "cmd.h"
#include "dynamic_buffer.h"
#include "msg.h"

/* Mark an API as deprecated. Emits a compiler warning on use under
 * GCC/Clang; expands to nothing under other compilers. The argument
 * is a short message naming the recommended replacement. */
#if defined(__GNUC__) || defined(__clang__)
#define BOBA_DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#define BOBA_DEPRECATED(msg)
#endif

/* Forward declaration for model base type */
typedef struct TuiModel TuiModel;

/* Base model structure - all component models should "inherit" from this */
struct TuiModel
{
    int type; /* Component type identifier */
};

/* Cursor position carried on TuiView.
 *
 * Bubbletea v2 alignment: components no longer emit cursor-positioning
 * sequences inside view(); they declare cursor placement on the
 * TuiView returned by view(). Set visible=0 to keep the cursor hidden. */
typedef struct TuiCursor
{
    int row;     /* 1-indexed terminal row */
    int col;     /* 1-indexed terminal column */
    int visible; /* 0 = hidden, 1 = place at row,col */
} TuiCursor;

static inline TuiCursor tui_cursor_hidden(void)
{
    TuiCursor c = { 0, 0, 0 };
    return c;
}

static inline TuiCursor tui_cursor_at(int row, int col)
{
    TuiCursor c = { row, col, 1 };
    return c;
}

/* Mouse tracking modes — declared each frame on TuiView. */
typedef enum
{
    TUI_MOUSE_MODE_NONE = 0,
    TUI_MOUSE_MODE_CELL_MOTION, /* SGR mouse, button events + cell motion */
    TUI_MOUSE_MODE_ALL_MOTION,  /* SGR mouse, all motion regardless of button */
} TuiMouseMode;

/* Keyboard-enhancement bitmask — declared each frame on TuiView. */
typedef enum
{
    TUI_KBD_NONE = 0,
    TUI_KBD_KITTY = 1 << 0, /* Kitty keyboard protocol */
} TuiKeyboardEnhancements;

/* Per-frame view: rendered content plus terminal-mode declarations.
 *
 * Bubbletea v2 alignment: components return a TuiView from their
 * view() slot. The runtime diffs each frame's TuiView against tracked
 * terminal state and emits only the bytes needed to reach the requested
 * state. Terminal-mode imperative commands (TUI_CMD_ENTER_ALT_SCREEN,
 * TUI_CMD_SET_WINDOW_TITLE, etc.) are gone — declare what you want
 * here instead. */
typedef struct TuiView
{
    /* Rendered content. The component writes bytes to this buffer; the
     * runtime owns its lifetime and clears it before each view() call. */
    DynamicBuffer *layer;

    /* Terminal-mode declarations. */
    int alt_screen;                           /* 1 = use alternate screen */
    TuiMouseMode mouse_mode;                  /* mouse tracking */
    TuiKeyboardEnhancements kbd_enhancements; /* keyboard protocol */
    int report_focus;                         /* 1 = enable focus events */
    int bracketed_paste;                      /* 1 = enable bracketed paste */

    /* Window title. NULL = leave alone. */
    const char *window_title;

    /* Cursor placement. visible=0 = hidden. */
    TuiCursor cursor;
} TuiView;

/* Initialise a TuiView with all modes off and the given content buffer. */
static inline TuiView tui_view_default(DynamicBuffer *layer)
{
    TuiView v = { 0 };
    v.layer = layer;
    return v;
}

/* Update result - returned by update function */
typedef struct
{
    TuiCmd *cmd; /* Command to execute (NULL for no command) */
} TuiUpdateResult;

/* Init result - returned by init function (Elm Architecture: init returns (Model, Cmd)) */
typedef struct
{
    TuiModel *model; /* Initialized model (NULL on failure) */
    TuiCmd *cmd;     /* Initial command to execute (NULL for no command) */
} TuiInitResult;

/* Component interface - virtual function table */
typedef struct TuiComponent
{
    /* Initialize and return model + optional initial command */
    TuiInitResult (*init)(void *config);

    /* Update model with message, return optional command */
    TuiUpdateResult (*update)(TuiModel *model, TuiMsg msg);

    /* Render model state. Component writes content bytes to `out` and
     * returns a TuiView whose layer is `out`, populated with the
     * terminal-mode declarations the component wants for this frame
     * (alt-screen, mouse mode, cursor, window title, etc.). */
    TuiView (*view)(const TuiModel *model, DynamicBuffer *out);

    /* Free model and associated resources */
    void (*free)(TuiModel *model);
} TuiComponent;

/* Helper macro to define component type ID */
#define TUI_COMPONENT_TYPE_BASE 100

/* Helper to create an update result with no command */
static inline TuiUpdateResult tui_update_result_none(void)
{
    TuiUpdateResult result = { .cmd = NULL };
    return result;
}

/* Helper to create an update result with a command */
static inline TuiUpdateResult tui_update_result(TuiCmd *cmd)
{
    TuiUpdateResult result = { .cmd = cmd };
    return result;
}

/* Helper to create an init result with model and command */
static inline TuiInitResult tui_init_result(TuiModel *model, TuiCmd *cmd)
{
    TuiInitResult result = { .model = model, .cmd = cmd };
    return result;
}

/* Helper to create an init result with no initial command */
static inline TuiInitResult tui_init_result_none(TuiModel *model)
{
    TuiInitResult result = { .model = model, .cmd = NULL };
    return result;
}

#endif /* BOBA_COMPONENT_H */
