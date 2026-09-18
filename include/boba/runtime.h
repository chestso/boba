/* runtime.h - Runtime and event loop for boba TUI library
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

#ifndef BOBA_RUNTIME_H
#define BOBA_RUNTIME_H

#include "cmd.h"
#include "component.h"
#include "dynamic_buffer.h"
#include "input_parser.h"
#include "msg.h"
#include "terminal_profile.h"

#include <stdio.h>

#ifndef _WIN32
#include <termios.h>
#else
#include <winsock2.h>
#include <windows.h>
#endif

/* Forward declaration */
typedef struct TuiRuntime TuiRuntime;

/* Forward declaration: streaming transcript component (stream.h). */
typedef struct TuiTranscript TuiTranscript;

/* Callback for commands the runtime doesn't handle natively */
typedef void (*TuiCmdHandler)(TuiCmd *cmd, void *user_data);

/* ---- I/O sources (Elm subscriptions, C idiom) ----
 *
 * The app declares its live I/O sources BEFORE EVERY WAIT via a fill
 * callback; the runtime reconciles the diff (rebinds what changed).
 * Interest/connection changes need no register/unregister call — just
 * return a different set next time. This is Elm's
 * `subscriptions : Model -> Sub Msg` spelled as a pull callback:
 * "model, what do you want now".
 *
 * A SOURCE is (handle, kind, flags). The kind says what `handle`
 * names and therefore HOW the platform can wait on it; the POSIX loop
 * treats every source as a pollable descriptor, so only Windows
 * consults the kind. This is the seam that lets an app hand the loop
 * something that is NOT a socket — a pipe's readiness event, say —
 * without the loop knowing how that readiness is produced. */

/* What `handle` names. */
#define TUI_SRC_FD     0 /* a POSIX file descriptor (poll) */
#define TUI_SRC_SOCKET 1 /* a Windows SOCKET (WSAEventSelect readiness) */
#define TUI_SRC_HANDLE 2 /* a Windows waitable HANDLE (signaled = ready) */

/* Per-source interest flags. */
#define TUI_IO_READ  (1u << 0) /* readable / EOF (a closed peer IS readable) */
#define TUI_IO_WRITE (1u << 1) /* pending connect finished (writable =   \
                                * completion signal), or send buffer has \
                                * room again */

#ifndef TUI_IO_SOURCE_MAX
#define TUI_IO_SOURCE_MAX 32
#endif

/* One declared I/O source: the object to wait on, what to wait for,
 * and how the platform should wait. */
typedef struct TuiIoSource
{
    intptr_t handle; /* fd / SOCKET / HANDLE; < 0 = no entry (rest of
                      * struct ignored). POSIX: an int fd. */
    unsigned flags;  /* TUI_IO_READ / TUI_IO_WRITE / both */
    int kind;        /* TUI_SRC_FD / TUI_SRC_SOCKET / TUI_SRC_HANDLE
                      * (0 = TUI_SRC_FD, so a zero-initialized struct is
                      * a plain descriptor) */
} TuiIoSource;

/* Fill `out` with the sources to wait on this cycle, at most `cap`.
 * Return the number filled (<= cap). Called before EVERY wait: the
 * app re-declares its live set each cycle — connections or interests
 * that went away simply stop being returned; new ones appear. This is
 * the subscriptions function (see above). Zero = nothing to wait on.
 *
 * Consumer obligations:
 * - Spurious wakeups are allowed; re-check state (getsockopt(SO_ERROR)
 *   after connect completes, EAGAIN-safe reads/writes).
 * - Clear TUI_IO_WRITE when drained: a perpetually-writable idle source
 *   with WRITE declared busy-loops the runtime.
 * - Do not declare the same handle twice across slots (undefined).
 * - EOF is readable — a closed peer must be dispatched READ.
 * - TUI_SRC_HANDLE sources must be auto-reset events (or events the app
 *   resets itself): the loop resets nothing it did not create. A
 *   manual-reset event left signaled wakes the loop forever. */
typedef size_t (*TuiFillIoSources)(TuiIoSource *out, size_t cap,
                                   void *user_data);

/* Dispatched once PER source with activity, carrying everything that
 * fired on that source. `ready` contains only bits the app declared
 * for that source (a failed connect is dispatched READ|WRITE so the
 * app's write path learns of it). `handle` is the same value the fill
 * returned. Runs on the loop thread only; reach update() via
 * tui_runtime_post from inside. */
typedef void (*TuiOnIoReady)(intptr_t handle, unsigned ready,
                             void *user_data);

/* Callback: called every tick (wait timeout, ~100ms) */
typedef void (*TuiOnTick)(void *user_data);

/* Return ms until next tick is needed, or -1 to block indefinitely.
 * Called before each select(). */
typedef int (*TuiGetTickTimeoutMs)(void *user_data);

/* Callback: terminal resized (width, height already updated in runtime) */
typedef void (*TuiOnResize)(int width, int height, void *user_data);

/* Callback: called after stdin input is processed through the runtime */
typedef void (*TuiOnStdinProcessed)(void *user_data);

/* Callback: the terminal capability probe reached its verdict — a
 * profile arrived, or the deadline passed (see terminal_profile.h).
 * Fires exactly once per probe, on the loop thread. The profile is
 * borrowed and lives as long as the runtime. Components then commit
 * any content that was gated on capability (e.g. IMAGE blocks) by
 * posting a message; they must not commit from inside this callback. */
typedef void (*TuiOnTermReply)(const struct TuiTerminalProfile *profile,
                               void *user_data);

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
    TuiFillIoSources fill_io_sources;        /* Declare I/O sources (per wait) */
    TuiOnIoReady on_io_ready;                /* Per-source: (handle, bits) fired */
    TuiOnTick on_tick;                       /* Tick (~100ms timeout) */
    TuiGetTickTimeoutMs get_tick_timeout_ms; /* Dynamic tick timeout */
    TuiOnResize on_resize;                   /* Terminal resized */
    TuiOnStdinProcessed on_stdin_processed;  /* After stdin processed */
    TuiOnTermReply on_term_reply;            /* Capability probe resolved */
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
    int inline_lines_rendered; /* lines drawn last frame in inline mode */
    int inline_cursor_row;     /* 0-indexed row where cursor was placed */
    int in_inline_mode;        /* 1 if last flush was inline mode */
    /* Inline transcript partial row (byte-granular commits end
     * mid-row): the seam can extend the open row on the next write.
     * inline_partial_cols is the display column of the row's end;
     * zero/off when no row is open. */
    int inline_partial_open;
    int inline_partial_cols;
    /* Monotonic time (ms) of the last on_tick dispatch. on_tick is a
     * periodic timer (Elm's Time.every), so it fires only once per
     * get_tick_timeout_ms interval even when the loop wakes early for
     * fds or wakeups — otherwise a dispatch that touches the terminal
     * spins the loop (a wakeup re-arms the wait, the wait fires the
     * tick, the tick wakes again). */
    long long last_tick_ms;
    TuiMouseMode cur_mouse_mode;
    TuiKeyboardEnhancements cur_kbd_enhancements;
    int cur_report_focus;
    int cur_bracketed_paste;
#ifndef _WIN32
    struct termios orig_termios; /* Saved terminal settings */
    int raw_mode_active;         /* Raw mode currently enabled */
#else
    DWORD orig_input_mode;  /* Saved console input mode (real console) */
    DWORD orig_output_mode; /* Saved console output mode */
    int is_pty;             /* 1 = ConPTY/pipe, 0 = real console */
    HANDLE wakeup_event;    /* Event object for waking WaitForMultipleObjects */
    /* I/O-source subscription pool: one WSAEVENT per socket slot,
     * reused across waits. Each cycle the fill callback declares up to
     * TUI_IO_SOURCE_MAX sources; the runtime waits on a socket slot's
     * WSAEVENT (bound via WSAEventSelect) or, for a TUI_SRC_HANDLE
     * source, on the app's own (auto-reset) handle directly. A socket
     * slot whose (handle, flags) differs from last cycle is rebound —
     * the rebind is the reconcile diff. FD_CLOSE is always armed so a
     * failed connect can't be missed. */
    HANDLE io_events[TUI_IO_SOURCE_MAX];
    intptr_t io_handles[TUI_IO_SOURCE_MAX]; /* Last handle bound per slot */
    unsigned io_flags[TUI_IO_SOURCE_MAX];   /* Last flags bound per slot */
    int io_kinds[TUI_IO_SOURCE_MAX];        /* Kind bound per slot */
    int io_count;                           /* Slots bound last cycle (0 = none) */
    /* Stdin reader thread: ReadFile on a console handle blocks for
     * non-key events (focus, resize) even when WaitForMultipleObjects
     * signals it. A background thread does the blocking read and
     * signals stdin_event when actual bytes arrive, so the main loop
     * can wait on stdin_event alongside socket_event without starving. */
    HANDLE stdin_thread;
    HANDLE stdin_event;    /* Manual-reset, signaled when data is ready */
    HANDLE stdin_consumed; /* Auto-reset, signaled by main loop after consuming */
    HANDLE stdin_done;     /* Manual-reset, signaled to stop the thread */
    CRITICAL_SECTION stdin_lock;
    unsigned char stdin_buf[256]; /* Buffered data from reader thread */
    size_t stdin_buf_len;         /* Valid bytes in stdin_buf */
    int stdin_eof;                /* Set when ReadFile returns 0 bytes */
#endif

    /* Message queue (for tui_runtime_post) */
    TuiMsg *msg_queue;
    int msg_queue_count;
    int msg_queue_cap;

    /* Attached streaming transcript (see tui_runtime_set_transcript).
     * Not owned. The commit pass runs at the top of tui_runtime_flush. */
    TuiTranscript *transcript;
    int committing; /* re-entrancy guard for the commit pass */

    /* Terminal capability probe (see terminal_profile.h).
     *
     * probe_state: 0 = idle, 1 = declared and about to be emitted,
     * 2 = outstanding (replies claimed, deadline armed),
     * 3 = resolved (callback fired).
     * probe_deadline_ms is a monotonic absolute time (0 = not armed). */
    TuiTerminalProfile profile;
    int probe_state;
    long long probe_deadline_ms;

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
BOBA_DEPRECATED("return tui_cmd_quit() from update() instead")
void tui_runtime_quit(TuiRuntime *runtime);

/* Start terminal mode: enter alt screen, enable mouse/keyboard per config */
void tui_runtime_start(TuiRuntime *runtime);

/* Stop terminal mode: reverse of start */
void tui_runtime_stop(TuiRuntime *runtime);

/* Finish inline mode: move cursor past all rendered content and write
 * \r\n so application output appears below the input. Resets
 * inline_lines_rendered and inline_cursor_row to 0 so the next flush
 * renders on a fresh line. Called before writing output directly. */
void tui_runtime_finish_inline(TuiRuntime *runtime);

/* Clear inline frame: erase the rendered frame in place (cursor-up to
 * frame row 0, EL each rendered row) and leave the cursor at frame
 * row 0, col 0 — the area the frame occupied is free for direct
 * application output, which overwrites it instead of abandoning stale
 * frame lines in the scrollback. inline_lines_rendered is KEPT so the
 * next flush's stale-line erase still wipes leftover blank rows; only
 * the cursor row resets. No-op before the first inline flush. */
void tui_runtime_clear_inline(TuiRuntime *runtime);

/* Transcript write: the ATOMIC inline-print seam — erase the live
 * frame in place, write whole transcript lines (each ending \r\n)
 * over the erased area, and re-render the live region below them in
 * the same call. This is the correct way to print scrollback lines
 * while an inline frame is live: the erase, the write, and the
 * repaint share one geometry baseline, so no interleaving (a second
 * print, an event-loop step between the write and the next flush)
 * can strand frame rows in the scrollback. Bytes must be line-safe
 * (every line \r\n-terminated); keep partial lines in the live
 * region. See also tui_runtime_clear_inline for the erase-only form.
 *
 * Partial-row extension (byte-granular transcripts): when `bytes`
 * does NOT end on a row terminator, the last written row stays OPEN
 * and the next transcript_write moves back up and continues it —
 * one call, one geometry baseline, same atomicity family. The
 * transcript component drives this via its commit pass; direct
 * callers should end with \r\n. */
void tui_runtime_transcript_write(TuiRuntime *runtime, const char *bytes,
                                  size_t len);

/* Attach a streaming transcript (stream.h) to the runtime. The
 * transcript's commit pass then runs at the top of every
 * tui_runtime_flush: all emission units staged since the last flush
 * reach the scrollback through exactly ONE transcript_write (one
 * geometry baseline per batch). The runtime does not own the
 * transcript; pass NULL to detach. Inline mode only. */
void tui_runtime_set_transcript(TuiRuntime *runtime, TuiTranscript *transcript);

/* Resolve the outstanding capability probe now, conservatively, and
 * fire on_term_reply if it has not fired yet. Called by the runtime
 * itself on timeout and on teardown; exposed because embedding
 * consumers that drive their own event loop must call it from their
 * timer, exactly as they call tui_runtime_drain() on the wakeup fd. */
void tui_runtime_probe_check(TuiRuntime *runtime);

/* Guarantee a profile verdict right now (conservative resolution if no
 * probe is outstanding). The transcript commit gate calls this when it
 * is holding an IMAGE unit, so a gated batch can never deadlock against
 * a probe the component forgot to declare. */
void tui_runtime_probe_ensure(TuiRuntime *runtime);

/* Terminal capability profile accessor. The profile lives in
 * terminal_profile.h; declared here so runtime.h clients get it
 * without an extra include. Never NULL; `resolved` says whether the
 * probe reached its verdict. */
const TuiTerminalProfile *tui_runtime_terminal_profile(TuiRuntime *rt);

/* Render view and write to output (with cursor hide/show) */
void tui_runtime_flush(TuiRuntime *runtime);

/* Execute a TuiCmd synchronously, outside the runtime's event loop.
 *
 * Escape hatch for callers that are NOT inside an update() / view()
 * invocation — typically embedding scenarios (e.g. a Lisp builtin
 * reaching in to set the window title). For effects driven by message
 * handling, return the Cmd from update() and let the runtime execute
 * it; do not call this from inside update(). */
BOBA_DEPRECATED(
    "return the command from update() instead of executing imperatively")
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

#endif /* BOBA_RUNTIME_H */
