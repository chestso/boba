/* runtime.c - Runtime and event loop implementation */

#include <bloom-boba/ansi_sequences.h>
#include <bloom-boba/runtime.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#define STDIN_READ_BUF_SIZE 256
#define MAX_MSGS_PER_FRAME  STDIN_READ_BUF_SIZE /* one msg per byte worst case */
#define QUEUE_INITIAL_CAP   16

/* --- Signal handling (Unix only) --- */
#ifndef _WIN32
static volatile sig_atomic_t s_sigwinch_received = 0;
static volatile sig_atomic_t s_sigint_received = 0;
static TuiRuntime *s_active_runtime = NULL;

static void runtime_sigwinch_handler(int sig)
{
    (void)sig;
    s_sigwinch_received = 1;
}

static void runtime_sigint_handler(int sig)
{
    (void)sig;
    s_sigint_received = 1;
}
#endif

/* --- Raw mode helpers (Unix only) --- */
#ifndef _WIN32
static int runtime_enable_raw_mode(TuiRuntime *rt)
{
    if (rt->raw_mode_active)
        return 0;

    if (tcgetattr(STDIN_FILENO, &rt->orig_termios) < 0)
        return -1;

    struct termios raw = rt->orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;

    rt->raw_mode_active = 1;
    return 0;
}

static void runtime_disable_raw_mode(TuiRuntime *rt)
{
    if (rt->raw_mode_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &rt->orig_termios);
        rt->raw_mode_active = 0;
    }
}
#endif

/* --- Terminal size helper --- */
#ifndef _WIN32
static void runtime_update_size(TuiRuntime *rt)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0)
            rt->term_width = ws.ws_col;
        if (ws.ws_row > 0)
            rt->term_height = ws.ws_row;
    }
}
#endif

/* --- atexit cleanup --- */
static void runtime_atexit_cleanup(void)
{
#ifndef _WIN32
    if (s_active_runtime && s_active_runtime->started) {
        tui_runtime_stop(s_active_runtime);
        runtime_disable_raw_mode(s_active_runtime);
    }
#endif
}

/* Forward declaration */
static int execute_cmd(TuiRuntime *runtime, TuiCmd *cmd);

/* Create runtime with component */
TuiRuntime *tui_runtime_create(TuiComponent *component, void *component_config,
                               const TuiRuntimeConfig *runtime_config)
{
    if (!component)
        return NULL;

    TuiRuntime *runtime = (TuiRuntime *)malloc(sizeof(TuiRuntime));
    if (!runtime)
        return NULL;

    memset(runtime, 0, sizeof(TuiRuntime));

    /* Store component interface */
    runtime->component = component;

    /* Initialize model (Elm Architecture: init returns (Model, Cmd)) */
    TuiInitResult init_result = component->init(component_config);
    if (!init_result.model) {
        free(runtime);
        return NULL;
    }
    runtime->model = init_result.model;

    /* Create input parser */
    runtime->parser = tui_input_parser_create();
    if (!runtime->parser) {
        component->free(runtime->model);
        free(runtime);
        return NULL;
    }

    /* Create view buffer */
    runtime->view_buf = dynamic_buffer_create(4096);
    if (!runtime->view_buf) {
        tui_input_parser_free(runtime->parser);
        component->free(runtime->model);
        free(runtime);
        return NULL;
    }

    /* Apply configuration */
    if (runtime_config) {
        runtime->config = *runtime_config;
    } else {
        /* Defaults */
        memset(&runtime->config, 0, sizeof(runtime->config));
        runtime->config.raw_mode = 1;
    }

    /* Resolve output target */
    runtime->output = runtime->config.output ? runtime->config.output : stdout;

    runtime->running = 1;
    runtime->quit_requested = 0;

    /* Initialize message queue */
    runtime->msg_queue =
        (TuiMsg *)malloc(QUEUE_INITIAL_CAP * sizeof(TuiMsg));
    runtime->msg_queue_count = 0;
    runtime->msg_queue_cap = runtime->msg_queue ? QUEUE_INITIAL_CAP : 0;

    /* Initialize command queue */
    runtime->cmd_queue =
        (TuiCmd **)malloc(QUEUE_INITIAL_CAP * sizeof(TuiCmd *));
    runtime->cmd_queue_count = 0;
    runtime->cmd_queue_cap = runtime->cmd_queue ? QUEUE_INITIAL_CAP : 0;

    /* Create self-pipe for waking select() */
    runtime->wakeup_pipe[0] = -1;
    runtime->wakeup_pipe[1] = -1;
#ifndef _WIN32
    if (pipe(runtime->wakeup_pipe) == 0) {
        fcntl(runtime->wakeup_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(runtime->wakeup_pipe[1], F_SETFL, O_NONBLOCK);
    }
#endif

    /* Execute initial command if provided (Elm Architecture) */
    if (init_result.cmd) {
        execute_cmd(runtime, init_result.cmd);
    }

    return runtime;
}

/* Free runtime and associated resources */
void tui_runtime_free(TuiRuntime *runtime)
{
    if (!runtime)
        return;

    if (runtime->view_buf)
        dynamic_buffer_destroy(runtime->view_buf);

    if (runtime->parser)
        tui_input_parser_free(runtime->parser);

    if (runtime->model && runtime->component)
        runtime->component->free(runtime->model);

    /* Free unprocessed commands in cmd_queue */
    for (int i = 0; i < runtime->cmd_queue_count; i++)
        tui_cmd_free(runtime->cmd_queue[i]);
    free(runtime->cmd_queue);

    /* Free message queue */
    free(runtime->msg_queue);

    /* Close wakeup pipe */
#ifndef _WIN32
    if (runtime->wakeup_pipe[0] >= 0)
        close(runtime->wakeup_pipe[0]);
    if (runtime->wakeup_pipe[1] >= 0)
        close(runtime->wakeup_pipe[1]);
#endif

    free(runtime);
}

/* Write a string to the runtime's output */
static void runtime_write(TuiRuntime *runtime, const char *s)
{
    if (s) {
        fputs(s, runtime->output);
    }
}

/* Execute a command */
static int execute_cmd(TuiRuntime *runtime, TuiCmd *cmd)
{
    if (!cmd)
        return 1;

    switch (cmd->type) {
    case TUI_CMD_QUIT:
        runtime->quit_requested = 1;
        runtime->running = 0;
        break;

    case TUI_CMD_BATCH:
        for (int i = 0; i < cmd->payload.batch.count; i++) {
            if (!execute_cmd(runtime, cmd->payload.batch.cmds[i])) {
                tui_cmd_free(cmd);
                return 0;
            }
        }
        break;

    case TUI_CMD_NONE:
        break;

    /* Terminal control commands — write sequences to output */
    case TUI_CMD_ENTER_ALT_SCREEN:
        if (!runtime->in_alt_screen) {
            runtime_write(runtime, DECSC);
            runtime_write(runtime, ANSI_ENTER_ALT_SCREEN);
            runtime->in_alt_screen = 1;
            fflush(runtime->output);
        }
        break;

    case TUI_CMD_EXIT_ALT_SCREEN:
        if (runtime->in_alt_screen) {
            runtime_write(runtime, ANSI_EXIT_ALT_SCREEN);
            runtime_write(runtime, DECRC);
            runtime->in_alt_screen = 0;
            fflush(runtime->output);
        }
        break;

    case TUI_CMD_ENABLE_MOUSE:
        runtime_write(runtime, ANSI_ENABLE_MOUSE);
        fflush(runtime->output);
        break;

    case TUI_CMD_DISABLE_MOUSE:
        runtime_write(runtime, ANSI_DISABLE_MOUSE);
        fflush(runtime->output);
        break;

    case TUI_CMD_ENABLE_KEYBOARD_ENHANCEMENT:
        runtime_write(runtime, ANSI_ENABLE_KITTY_KBD);
        fflush(runtime->output);
        break;

    case TUI_CMD_DISABLE_KEYBOARD_ENHANCEMENT:
        runtime_write(runtime, ANSI_DISABLE_KITTY_KBD);
        fflush(runtime->output);
        break;

    case TUI_CMD_SHOW_CURSOR:
        runtime_write(runtime, ANSI_SHOW_CURSOR);
        fflush(runtime->output);
        break;

    case TUI_CMD_HIDE_CURSOR:
        runtime_write(runtime, ANSI_HIDE_CURSOR);
        fflush(runtime->output);
        break;

    case TUI_CMD_SET_WINDOW_TITLE:
    {
        char buf[512];
        ansi_set_window_title(buf, sizeof(buf),
                              cmd->payload.window_title ? cmd->payload.window_title : "");
        runtime_write(runtime, buf);
        fflush(runtime->output);
        break;
    }

    case TUI_CMD_CLIPBOARD_COPY:
    {
        const char *text = cmd->payload.clipboard.text;
        size_t text_len = cmd->payload.clipboard.len;

        if (runtime->config.clipboard_handler) {
            runtime->config.clipboard_handler(
                text, text_len, runtime->config.clipboard_handler_data);
        } else if (text && text_len > 0) {
            /* Default: emit OSC 52 to terminal. */
            size_t needed = ANSI_OSC52_BUFSIZE(text_len);
            char *seq = (char *)malloc(needed);
            if (seq) {
                size_t n = ansi_format_osc52(seq, needed, text, text_len);
                if (n > 0) {
                    fwrite(seq, 1, n, runtime->output);
                    fflush(runtime->output);
                }
                free(seq);
            }
        }
        break;
    }

    default:
        /* Custom command - execute callback and send result message */
        if (cmd->type >= TUI_CMD_CUSTOM_BASE && cmd->payload.custom.callback) {
            TuiMsg result_msg =
                cmd->payload.custom.callback(cmd->payload.custom.data);
            if (result_msg.type != TUI_MSG_NONE) {
                tui_runtime_send(runtime, result_msg);
            }
        } else if (runtime->config.cmd_handler) {
            /* Delegate to app callback (LINE_SUBMIT, TAB_COMPLETE, etc.) */
            runtime->config.cmd_handler(cmd, runtime->config.cmd_handler_data);
            /* cmd_handler is responsible for the cmd, don't free here */
            return runtime->running;
        }
        break;
    }

    tui_cmd_free(cmd);
    return runtime->running;
}

/* Process a single message through the runtime */
int tui_runtime_send(TuiRuntime *runtime, TuiMsg msg)
{
    if (!runtime || !runtime->running || !runtime->component)
        return 0;

    /* Update model */
    TuiUpdateResult result = runtime->component->update(runtime->model, msg);

    /* Execute any returned command */
    if (result.cmd) {
        return execute_cmd(runtime, result.cmd);
    }

    return runtime->running;
}

/* Process raw input bytes */
int tui_runtime_process_input(TuiRuntime *runtime, const unsigned char *input,
                              size_t len)
{
    if (!runtime || !runtime->running || !input || len == 0)
        return runtime ? runtime->running : 0;

    TuiMsg msgs[MAX_MSGS_PER_FRAME];
    int count = tui_input_parser_parse(runtime->parser, input, len, msgs,
                                       MAX_MSGS_PER_FRAME);

    for (int i = 0; i < count; i++) {
        if (!tui_runtime_send(runtime, msgs[i])) {
            return 0;
        }
    }

    return runtime->running;
}

/* Render current state to buffer */
const char *tui_runtime_render(TuiRuntime *runtime)
{
    if (!runtime || !runtime->component || !runtime->model)
        return "";

    dynamic_buffer_clear(runtime->view_buf);
    runtime->component->view(runtime->model, runtime->view_buf);

    return dynamic_buffer_data(runtime->view_buf);
}

/* Get the current model */
TuiModel *tui_runtime_model(TuiRuntime *runtime)
{
    return runtime ? runtime->model : NULL;
}

/* Check if runtime should quit */
int tui_runtime_should_quit(TuiRuntime *runtime)
{
    return runtime ? runtime->quit_requested : 1;
}

/* Request runtime to quit */
void tui_runtime_quit(TuiRuntime *runtime)
{
    if (runtime) {
        runtime->quit_requested = 1;
        runtime->running = 0;
    }
}

/* Start terminal mode: enter alt screen, enable mouse/keyboard per config */
void tui_runtime_start(TuiRuntime *runtime)
{
    if (!runtime || runtime->started)
        return;

    if (runtime->config.use_alternate_screen) {
        runtime_write(runtime, DECSC);
        runtime_write(runtime, ANSI_ENTER_ALT_SCREEN);
        runtime_write(runtime, ED_ENTIRE CUP_HOME);
        runtime->in_alt_screen = 1;
    }

    if (runtime->config.enable_mouse)
        runtime_write(runtime, ANSI_ENABLE_MOUSE);

    if (runtime->config.enable_keyboard_enhancement)
        runtime_write(runtime, ANSI_ENABLE_KITTY_KBD);

    if (runtime->config.hide_cursor)
        runtime_write(runtime, ANSI_HIDE_CURSOR);

    fflush(runtime->output);
    runtime->started = 1;
}

/* Stop terminal mode: reverse of start */
void tui_runtime_stop(TuiRuntime *runtime)
{
    if (!runtime || !runtime->started)
        return;

    runtime_write(runtime, SGR_RESET);
    runtime_write(runtime, ANSI_SHOW_CURSOR);

    if (runtime->config.enable_keyboard_enhancement)
        runtime_write(runtime, ANSI_DISABLE_KITTY_KBD);

    if (runtime->config.enable_mouse)
        runtime_write(runtime, ANSI_DISABLE_MOUSE);

    if (runtime->in_alt_screen) {
        runtime_write(runtime, ANSI_EXIT_ALT_SCREEN);
        runtime_write(runtime, DECRC);
        runtime->in_alt_screen = 0;
    }

    fflush(runtime->output);
    runtime->started = 0;
}

/* Render view, place cursor per component opinion, and write to output.
 *
 * Bubbletea v2 alignment: components write content only in view(); they
 * report where the hardware cursor should land via the optional cursor()
 * slot. The runtime hides the cursor during paint, then either places it
 * at the reported position and shows it, or leaves it hidden. */
void tui_runtime_flush(TuiRuntime *runtime)
{
    if (!runtime || !runtime->component || !runtime->model)
        return;

    dynamic_buffer_clear(runtime->view_buf);

    /* Hide cursor during paint to prevent flicker. */
    dynamic_buffer_append_str(runtime->view_buf, ANSI_HIDE_CURSOR);

    runtime->component->view(runtime->model, runtime->view_buf);

    TuiCursor cursor = tui_cursor_hidden();
    if (runtime->component->cursor)
        cursor = runtime->component->cursor(runtime->model);

    /* config.hide_cursor remains a hard override (always keep hidden). */
    if (cursor.visible && !runtime->config.hide_cursor) {
        char buf[32];
        ansi_format_cursor_pos(buf, sizeof(buf), cursor.row, cursor.col);
        dynamic_buffer_append_str(runtime->view_buf, buf);
        dynamic_buffer_append_str(runtime->view_buf, ANSI_SHOW_CURSOR);
    }

    fwrite(dynamic_buffer_data(runtime->view_buf), 1,
           dynamic_buffer_len(runtime->view_buf), runtime->output);
    fflush(runtime->output);
}

/* Execute a TuiCmd synchronously, outside the runtime's event loop.
 * Escape hatch for embedding scenarios; not for use inside update(). */
void tui_runtime_exec(TuiRuntime *runtime, TuiCmd *cmd)
{
    if (!runtime || !cmd)
        return;
    execute_cmd(runtime, cmd);
}

/* --- Self-pipe helpers --- */

static void runtime_wakeup(TuiRuntime *rt)
{
#ifndef _WIN32
    if (rt->wakeup_pipe[1] >= 0) {
        char c = 'W';
        while (write(rt->wakeup_pipe[1], &c, 1) < 0 && errno == EINTR)
            ;
        /* EAGAIN is fine — pipe already has data, select will fire */
    }
#endif
}

static void runtime_drain_wakeup(TuiRuntime *rt)
{
#ifndef _WIN32
    if (rt->wakeup_pipe[0] >= 0) {
        char buf[64];
        while (read(rt->wakeup_pipe[0], buf, sizeof(buf)) > 0)
            ;
    }
#endif
}

/* --- Message/Command scheduling API --- */

/* Post a message to be processed on the next event loop iteration */
void tui_runtime_post(TuiRuntime *runtime, TuiMsg msg)
{
    if (!runtime)
        return;

    /* Grow queue if full */
    if (runtime->msg_queue_count >= runtime->msg_queue_cap) {
        int new_cap =
            runtime->msg_queue_cap ? runtime->msg_queue_cap * 2 : QUEUE_INITIAL_CAP;
        TuiMsg *new_buf =
            (TuiMsg *)realloc(runtime->msg_queue, new_cap * sizeof(TuiMsg));
        if (!new_buf)
            return; /* OOM — drop the message */
        runtime->msg_queue = new_buf;
        runtime->msg_queue_cap = new_cap;
    }

    runtime->msg_queue[runtime->msg_queue_count++] = msg;
    runtime_wakeup(runtime);
}

/* Defer a command for asynchronous execution by the runtime; any
 * resulting message flows back through update(). */
void tui_runtime_schedule(TuiRuntime *runtime, TuiCmd *cmd)
{
    if (!runtime || !cmd)
        return;

    /* Grow queue if full */
    if (runtime->cmd_queue_count >= runtime->cmd_queue_cap) {
        int new_cap =
            runtime->cmd_queue_cap ? runtime->cmd_queue_cap * 2 : QUEUE_INITIAL_CAP;
        TuiCmd **new_buf = (TuiCmd **)realloc(runtime->cmd_queue,
                                              new_cap * sizeof(TuiCmd *));
        if (!new_buf) {
            tui_cmd_free(cmd); /* OOM — free the command we own */
            return;
        }
        runtime->cmd_queue = new_buf;
        runtime->cmd_queue_cap = new_cap;
    }

    runtime->cmd_queue[runtime->cmd_queue_count++] = cmd;
    runtime_wakeup(runtime);
}

/* Process all pending queued commands and messages (swap-and-drain) */
void tui_runtime_drain(TuiRuntime *runtime)
{
    if (!runtime)
        return;

    runtime_drain_wakeup(runtime);

    for (;;) {
        /* Snapshot and swap command queue */
        TuiCmd **snap_cmds = NULL;
        int snap_cmd_count = 0;

        if (runtime->cmd_queue_count > 0) {
            snap_cmds = runtime->cmd_queue;
            snap_cmd_count = runtime->cmd_queue_count;

            /* Install fresh empty queue */
            runtime->cmd_queue =
                (TuiCmd **)malloc(QUEUE_INITIAL_CAP * sizeof(TuiCmd *));
            runtime->cmd_queue_count = 0;
            runtime->cmd_queue_cap =
                runtime->cmd_queue ? QUEUE_INITIAL_CAP : 0;
        }

        /* Snapshot and swap message queue */
        TuiMsg *snap_msgs = NULL;
        int snap_msg_count = 0;

        if (runtime->msg_queue_count > 0) {
            snap_msgs = runtime->msg_queue;
            snap_msg_count = runtime->msg_queue_count;

            /* Install fresh empty queue */
            runtime->msg_queue =
                (TuiMsg *)malloc(QUEUE_INITIAL_CAP * sizeof(TuiMsg));
            runtime->msg_queue_count = 0;
            runtime->msg_queue_cap =
                runtime->msg_queue ? QUEUE_INITIAL_CAP : 0;
        }

        /* Nothing to process — done */
        if (snap_cmd_count == 0 && snap_msg_count == 0)
            break;

        /* Execute commands first (direct side effects) */
        for (int i = 0; i < snap_cmd_count; i++)
            execute_cmd(runtime, snap_cmds[i]);
        free(snap_cmds);

        /* Process messages through update() */
        for (int i = 0; i < snap_msg_count; i++)
            tui_runtime_send(runtime, snap_msgs[i]);
        free(snap_msgs);

        /* Loop again if processing enqueued more items */
    }
}

/* Get the wakeup FD for use with select()/poll() */
int tui_runtime_wakeup_fd(TuiRuntime *runtime)
{
    if (!runtime)
        return -1;
    return runtime->wakeup_pipe[0];
}

/* Wake the event loop from select() */
void tui_runtime_wakeup(TuiRuntime *runtime)
{
    if (runtime)
        runtime_wakeup(runtime);
}

/* Run the full event loop (blocking). Owns raw mode, signals, select().
 * Returns 0 on normal exit, -1 on error. */
int tui_runtime_run(TuiRuntime *runtime)
{
    if (!runtime)
        return -1;

#ifndef _WIN32
    /* Get initial terminal size and send to component */
    runtime_update_size(runtime);
    TuiMsg size_msg = { .type = TUI_MSG_WINDOW_SIZE,
                        .data.size = { .width = runtime->term_width,
                                       .height = runtime->term_height } };
    tui_runtime_send(runtime, size_msg);
    if (runtime->config.on_resize)
        runtime->config.on_resize(runtime->term_width,
                                  runtime->term_height,
                                  runtime->config.event_data);

    /* Enable raw mode if configured */
    if (runtime->config.raw_mode) {
        if (runtime_enable_raw_mode(runtime) < 0)
            return -1;
    }

    /* Install signal handlers (save old ones for restoration) */
    struct sigaction sa_winch, sa_int, old_winch, old_int;
    s_sigwinch_received = 0;
    s_sigint_received = 0;

    memset(&sa_winch, 0, sizeof(sa_winch));
    sa_winch.sa_handler = runtime_sigwinch_handler;
    sa_winch.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa_winch, &old_winch);

    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = runtime_sigint_handler;
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, &old_int);

    /* Register for atexit safety */
    s_active_runtime = runtime;
    atexit(runtime_atexit_cleanup);

    /* Start terminal mode (alt screen, mouse, keyboard) */
    tui_runtime_start(runtime);
    tui_runtime_flush(runtime);

    /* Main event loop */
    while (runtime->running && !s_sigint_received) {
        /* Check for deferred SIGWINCH */
        if (s_sigwinch_received) {
            s_sigwinch_received = 0;
            runtime_update_size(runtime);
            TuiMsg ws_msg = {
                .type = TUI_MSG_WINDOW_SIZE,
                .data.size = { .width = runtime->term_width,
                               .height = runtime->term_height }
            };
            tui_runtime_send(runtime, ws_msg);
            if (runtime->config.on_resize)
                runtime->config.on_resize(runtime->term_width,
                                          runtime->term_height,
                                          runtime->config.event_data);
            tui_runtime_flush(runtime);
        }

        /* Build fd_set: stdin + optional external FD + wakeup pipe */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        int max_fd = STDIN_FILENO;

        int ext_fd = -1;
        if (runtime->config.get_external_fd) {
            ext_fd =
                runtime->config.get_external_fd(runtime->config.event_data);
            if (ext_fd >= 0) {
                FD_SET(ext_fd, &read_fds);
                if (ext_fd > max_fd)
                    max_fd = ext_fd;
            }
        }

        if (runtime->wakeup_pipe[0] >= 0) {
            FD_SET(runtime->wakeup_pipe[0], &read_fds);
            if (runtime->wakeup_pipe[0] > max_fd)
                max_fd = runtime->wakeup_pipe[0];
        }

        /* Compute tick timeout */
        struct timeval tv;
        struct timeval *tv_ptr;
        if (runtime->config.get_tick_timeout_ms) {
            int ms = runtime->config.get_tick_timeout_ms(
                runtime->config.event_data);
            if (ms < 0) {
                tv_ptr = NULL; /* Block indefinitely */
            } else {
                tv.tv_sec = ms / 1000;
                tv.tv_usec = (ms % 1000) * 1000;
                tv_ptr = &tv;
            }
        } else {
            /* Default 100ms for simple consumers */
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            tv_ptr = &tv;
        }

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, tv_ptr);

        if (ready < 0) {
            if (errno == EINTR)
                continue; /* Will check signal flags next iteration */
            break;        /* Real error */
        }

        /* stdin ready */
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            unsigned char buf[STDIN_READ_BUF_SIZE];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                tui_runtime_process_input(runtime, buf, n);
                if (runtime->config.on_stdin_processed)
                    runtime->config.on_stdin_processed(
                        runtime->config.event_data);
                tui_runtime_flush(runtime);
            } else if (n == 0) {
                /* EOF on stdin */
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break; /* Read error */
            }
        }

        /* External FD ready */
        if (ready > 0 && ext_fd >= 0 && FD_ISSET(ext_fd, &read_fds)) {
            if (runtime->config.on_external_ready)
                runtime->config.on_external_ready(runtime->config.event_data);
        }

        /* Wakeup pipe ready — drain queued messages/commands */
        if (ready > 0 && runtime->wakeup_pipe[0] >= 0 &&
            FD_ISSET(runtime->wakeup_pipe[0], &read_fds)) {
            tui_runtime_drain(runtime);
            tui_runtime_flush(runtime);
        }

        /* Tick — fires on every iteration (timeout, fd activity, wakeup) */
        if (runtime->config.on_tick)
            runtime->config.on_tick(runtime->config.event_data);
    }

    /* Teardown */
    tui_runtime_stop(runtime);
    runtime_disable_raw_mode(runtime);
    s_active_runtime = NULL;

    /* Restore old signal handlers */
    sigaction(SIGWINCH, &old_winch, NULL);
    sigaction(SIGINT, &old_int, NULL);

    return 0;
#else
    /* Windows: not yet implemented */
    return -1;
#endif
}

/* Get current terminal width */
int tui_runtime_get_width(TuiRuntime *runtime)
{
    return runtime ? runtime->term_width : 0;
}

/* Get current terminal height */
int tui_runtime_get_height(TuiRuntime *runtime)
{
    return runtime ? runtime->term_height : 0;
}
