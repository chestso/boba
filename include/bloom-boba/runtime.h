/* runtime.h - Runtime and event loop for bloom-boba TUI library
 *
 * The runtime handles:
 * - Terminal setup/teardown (raw mode, alternate screen)
 * - Reading input and parsing to messages
 * - Executing commands
 * - Rendering views
 *
 * Two usage modes:
 * 1. tui_runtime_run() — owns the event loop, raw mode, signals (Bubbletea-style)
 * 2. Lower-level functions — caller owns the event loop and calls process_input/flush
 */

#ifndef BLOOM_BOBA_RUNTIME_H
#define BLOOM_BOBA_RUNTIME_H

#include "cmd.h"
#include "component.h"
#include "dynamic_buffer.h"
#include "input_parser.h"
#include "msg.h"

#include <stdio.h>

#ifndef _WIN32
#include <termios.h>
#endif

/* Forward declaration */
typedef struct TuiRuntime TuiRuntime;

/* Callback for commands the runtime doesn't handle natively */
typedef void (*TuiCmdHandler)(TuiCmd *cmd, void *user_data);

/* Callback: return an external FD to poll alongside stdin (-1 = none) */
typedef int (*TuiGetExternalFd)(void *user_data);

/* Callback: external FD is ready for reading */
typedef void (*TuiOnExternalReady)(void *user_data);

/* Callback: called every tick (select timeout, ~100ms) */
typedef void (*TuiOnTick)(void *user_data);

/* Return ms until next tick is needed, or -1 to block indefinitely.
 * Called before each select(). */
typedef int (*TuiGetTickTimeoutMs)(void *user_data);

/* Callback: terminal resized (width, height already updated in runtime) */
typedef void (*TuiOnResize)(int width, int height, void *user_data);

/* Callback: called after stdin input is processed through the runtime */
typedef void (*TuiOnStdinProcessed)(void *user_data);

/* Callback: handle a clipboard-copy command. If installed, the runtime calls
 * this instead of emitting OSC 52 to the output. Useful when running inside
 * a terminal that does not implement OSC 52 (notably VTE-based terminals
 * such as GNOME Terminal, XFCE, Terminator), where the app should shell out
 * to xclip / wl-copy / pbcopy or use another mechanism.
 *
 * The text is owned by the runtime (do not free). */
typedef void (*TuiClipboardHandler)(const char *text, size_t len,
                                    void *user_data);

/* Runtime configuration.
 *
 * Bubbletea v2 alignment: terminal-mode flags (alt screen, mouse,
 * keyboard enhancements, cursor visibility) are no longer set here.
 * Components declare them on the TuiView returned from view() and the
 * runtime reconciles each frame. */
typedef struct TuiRuntimeConfig
{
    int raw_mode;              /* Enable raw terminal mode */
    FILE *output;              /* Output target (NULL = stdout) */
    TuiCmdHandler cmd_handler; /* App command callback */
    void *cmd_handler_data;    /* Callback context */

    /* Event loop callbacks (used by tui_runtime_run) */
    TuiGetExternalFd get_external_fd;        /* External FD to poll */
    TuiOnExternalReady on_external_ready;    /* External FD ready */
    TuiOnTick on_tick;                       /* Tick (~100ms timeout) */
    TuiGetTickTimeoutMs get_tick_timeout_ms; /* Dynamic tick timeout */
    TuiOnResize on_resize;                   /* Terminal resized */
    TuiOnStdinProcessed on_stdin_processed;  /* After stdin processed */
    void *event_data;                        /* Context pointer for event callbacks */

    /* Clipboard override. If non-NULL, the runtime calls this for
     * TUI_CMD_CLIPBOARD_COPY instead of emitting OSC 52. */
    TuiClipboardHandler clipboard_handler;
    void *clipboard_handler_data;
} TuiRuntimeConfig;

/* Runtime state */
struct TuiRuntime
{
    TuiComponent *component; /* Component interface */
    TuiModel *model;         /* Current model state */
    TuiInputParser *parser;  /* Input parser */
    DynamicBuffer *view_buf; /* Buffer for view rendering */
    TuiRuntimeConfig config; /* Runtime configuration */
    FILE *output;            /* Resolved output target */
    int running;             /* Whether runtime is running */
    int quit_requested;      /* Quit has been requested */
    int started;             /* Idempotent start/stop guard */
    int term_width;          /* Current terminal width */
    int term_height;         /* Current terminal height */

    /* Tracked terminal state — diffed against TuiView each flush. */
    int in_alt_screen;
    TuiMouseMode cur_mouse_mode;
    TuiKeyboardEnhancements cur_kbd_enhancements;
    int cur_report_focus;
    int cur_bracketed_paste;
#ifndef _WIN32
    struct termios orig_termios; /* Saved terminal settings */
    int raw_mode_active;         /* Raw mode currently enabled */
#endif

    /* Message queue (for tui_runtime_post) */
    TuiMsg *msg_queue;
    int msg_queue_count;
    int msg_queue_cap;

    /* Command queue (for tui_runtime_schedule) */
    TuiCmd **cmd_queue;
    int cmd_queue_count;
    int cmd_queue_cap;

    /* Self-pipe for waking select() */
    int wakeup_pipe[2]; /* [0]=read, [1]=write; -1 if unavailable */
};

/* Create runtime with component
 *
 * Parameters:
 *   component: Component interface (init/update/view/free)
 *   config: Optional configuration (NULL for defaults)
 *
 * Returns: New runtime, or NULL on failure
 */
TuiRuntime *tui_runtime_create(TuiComponent *component, void *component_config,
                               const TuiRuntimeConfig *runtime_config);

/* Free runtime and associated resources */
void tui_runtime_free(TuiRuntime *runtime);

/* Process a single message through the runtime
 *
 * Parameters:
 *   runtime: Runtime state
 *   msg: Message to process
 *
 * Returns: 1 if should continue, 0 if should quit
 */
int tui_runtime_send(TuiRuntime *runtime, TuiMsg msg);

/* Process raw input bytes
 *
 * Parameters:
 *   runtime: Runtime state
 *   input: Input bytes
 *   len: Number of bytes
 *
 * Returns: 1 if should continue, 0 if should quit
 */
int tui_runtime_process_input(TuiRuntime *runtime, const unsigned char *input,
                              size_t len);

/* Render current state to buffer
 *
 * Parameters:
 *   runtime: Runtime state
 *
 * Returns: Rendered view as string (owned by runtime, do not free)
 */
const char *tui_runtime_render(TuiRuntime *runtime);

/* Get the current model */
TuiModel *tui_runtime_model(TuiRuntime *runtime);

/* Check if runtime should quit */
int tui_runtime_should_quit(TuiRuntime *runtime);

/* Request runtime to quit */
void tui_runtime_quit(TuiRuntime *runtime);

/* Start terminal mode: enter alt screen, enable mouse/keyboard per config */
void tui_runtime_start(TuiRuntime *runtime);

/* Stop terminal mode: reverse of start */
void tui_runtime_stop(TuiRuntime *runtime);

/* Render view and write to output (with cursor hide/show) */
void tui_runtime_flush(TuiRuntime *runtime);

/* Execute a TuiCmd synchronously, outside the runtime's event loop.
 *
 * Escape hatch for callers that are NOT inside an update() / view()
 * invocation — typically embedding scenarios (e.g. a Lisp builtin
 * reaching in to set the window title). For effects driven by message
 * handling, return the Cmd from update() and let the runtime execute
 * it; do not call this from inside update(). */
void tui_runtime_exec(TuiRuntime *runtime, TuiCmd *cmd);

/* Run the full event loop (blocking). Owns raw mode, signals, select().
 * Returns 0 on normal exit, -1 on error. */
int tui_runtime_run(TuiRuntime *runtime);

/* Get current terminal dimensions */
int tui_runtime_get_width(TuiRuntime *runtime);
int tui_runtime_get_height(TuiRuntime *runtime);

/* Post a message to be processed on the next event loop iteration.
 * Goes through update() → cmd execution (full Elm Architecture cycle).
 * Wakes up the event loop if blocked in select(). */
void tui_runtime_post(TuiRuntime *runtime, TuiMsg msg);

/* Schedule a command to be executed on the next event loop iteration.
 * Defers the command for asynchronous execution; any resulting message
 * flows back through update(). Runtime takes ownership of the command. */
void tui_runtime_schedule(TuiRuntime *runtime, TuiCmd *cmd);

/* Process all pending queued commands and messages.
 * Called automatically by tui_runtime_run().
 * Lower-level API users should call this in their own event loop. */
void tui_runtime_drain(TuiRuntime *runtime);

/* Get the wakeup FD for use with select()/poll().
 * Returns -1 if unavailable. When readable, call tui_runtime_drain(). */
int tui_runtime_wakeup_fd(TuiRuntime *runtime);

/* Wake the event loop from select(). Thread-safe.
 * Use when external state (e.g. timers) changes and select() should
 * recompute its timeout. */
void tui_runtime_wakeup(TuiRuntime *runtime);

#endif /* BLOOM_BOBA_RUNTIME_H */
