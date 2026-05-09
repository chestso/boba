/* component.h - Component interface for bloom-boba TUI library
 *
 * Components follow the Elm Architecture pattern:
 * - init: Create initial model state
 * - update: Handle messages and update state
 * - view: Render current state to output buffer
 * - free: Cleanup resources
 */

#ifndef BLOOM_BOBA_COMPONENT_H
#define BLOOM_BOBA_COMPONENT_H

#include "cmd.h"
#include "dynamic_buffer.h"
#include "msg.h"

/* Forward declaration for model base type */
typedef struct TuiModel TuiModel;

/* Base model structure - all component models should "inherit" from this */
struct TuiModel
{
    int type; /* Component type identifier */
};

/* Cursor position reported by a component to the runtime.
 *
 * Bubbletea v2 alignment: components no longer emit cursor-positioning
 * sequences inside view(); they instead return where they want the
 * hardware cursor placed. Set visible=0 (or leave the slot NULL on the
 * TuiComponent vtable) to abstain — the runtime keeps the cursor hidden. */
typedef struct TuiCursor
{
    int row;     /* 1-indexed terminal row */
    int col;     /* 1-indexed terminal column */
    int visible; /* 0 = abstain (cursor hidden), 1 = place at row,col */
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

    /* Render model state to output buffer */
    void (*view)(const TuiModel *model, DynamicBuffer *out);

    /* Optional: where the focused cursor should be placed for this frame.
     * Return tui_cursor_hidden() (or leave the slot NULL on the vtable) to
     * abstain — the runtime keeps the cursor hidden. */
    TuiCursor (*cursor)(const TuiModel *model);

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

#endif /* BLOOM_BOBA_COMPONENT_H */
