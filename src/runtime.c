/* runtime.c - Runtime and event loop implementation */

#include <boba/ansi_sequences.h>
#include <boba/runtime.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#else
#include <io.h>
#include <windows.h>
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

/* --- Windows helpers --- */
#ifdef _WIN32
static int runtime_enable_raw_mode_win(TuiRuntime *rt)
{
    HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (GetConsoleMode(h_in, &mode)) {
        /* Real console — save and set VT input mode */
        rt->is_pty = 0;
        rt->orig_input_mode = mode;
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                  ENABLE_PROCESSED_INPUT);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(h_in, mode);

        /* Enable VT processing on output */
        HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD out_mode;
        if (GetConsoleMode(h_out, &out_mode)) {
            rt->orig_output_mode = out_mode;
            out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h_out, out_mode);
        }
    } else {
        /* ConPTY/pipe — no console mode to set, bytes arrive raw */
        rt->is_pty = 1;
    }
    return 0;
}

static void runtime_disable_raw_mode_win(TuiRuntime *rt)
{
    if (!rt->is_pty) {
        HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleMode(h_in, rt->orig_input_mode);
        HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleMode(h_out, rt->orig_output_mode);
    }
}

static void runtime_update_size_win(TuiRuntime *rt)
{
    /* Try GetConsoleScreenBufferInfo first (real console) */
    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h_out, &csbi)) {
        rt->term_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rt->term_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        /* ConPTY/pipe — use environment variables */
        const char *cols = getenv("COLUMNS");
        const char *lines = getenv("LINES");
        if (cols)
            rt->term_width = atoi(cols);
        if (lines)
            rt->term_height = atoi(lines);
        if (rt->term_width <= 0)
            rt->term_width = 80;
        if (rt->term_height <= 0)
            rt->term_height = 24;
    }
}

static void runtime_wakeup_win(TuiRuntime *rt)
{
    if (rt->wakeup_event)
        SetEvent(rt->wakeup_event);
}
#endif

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
        int continuing = tui_runtime_send(runtime, msgs[i]);
        tui_msg_free(&msgs[i]);
        if (!continuing) {
            /* Free remaining un-dispatched messages so paste payloads
             * don't leak when the app quits mid-batch. */
            for (int j = i + 1; j < count; j++) {
                tui_msg_free(&msgs[j]);
            }
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

/* Start terminal mode. Just guards idempotent start/stop and saves
 * cursor state for restoration. Actual mode bytes (alt-screen, mouse,
 * etc.) are emitted by tui_runtime_flush based on the first View. */
void tui_runtime_start(TuiRuntime *runtime)
{
    if (!runtime || runtime->started)
        return;
    /* In inline mode, don't save cursor — stop() moves past content
     * and writes a newline; start() just begins from current position.
     * Reset inline_lines_rendered so the first flush doesn't cursor-up
     * into the old content (which is now above us in the scrollback). */
    if (!runtime->in_inline_mode)
        runtime_write(runtime, DECSC);
    else
        runtime->inline_lines_rendered = 0;
    fflush(runtime->output);
    runtime->started = 1;
}

/* Stop terminal mode: undo whatever the last View asked for. */
void tui_runtime_stop(TuiRuntime *runtime)
{
    if (!runtime || !runtime->started)
        return;

    runtime_write(runtime, SGR_RESET);
    runtime_write(runtime, ANSI_SHOW_CURSOR);

    if (runtime->in_inline_mode) {
        /* Inline mode: write \r\n to end the current input line, then
         * disable bracketed paste. Output printed after stop() will
         * appear on fresh lines below. Do NOT do DECRC — we want the
         * cursor to stay here, not jump back to where start() saved it. */
        runtime_write(runtime, "\r\n");
        if (runtime->cur_bracketed_paste) {
            runtime_write(runtime, ANSI_DISABLE_BRACKETED_PASTE);
            runtime->cur_bracketed_paste = 0;
        }
        runtime->inline_lines_rendered = 0;
        runtime->in_inline_mode = 0;
        fflush(runtime->output);
        runtime->started = 0;
        return;
    }

    if (runtime->cur_kbd_enhancements & TUI_KBD_KITTY) {
        runtime_write(runtime, ANSI_DISABLE_KITTY_KBD);
        runtime->cur_kbd_enhancements = TUI_KBD_NONE;
    }
    if (runtime->cur_bracketed_paste) {
        runtime_write(runtime, ANSI_DISABLE_BRACKETED_PASTE);
        runtime->cur_bracketed_paste = 0;
    }
    if (runtime->cur_mouse_mode != TUI_MOUSE_MODE_NONE) {
        runtime_write(runtime, ANSI_DISABLE_MOUSE);
        runtime->cur_mouse_mode = TUI_MOUSE_MODE_NONE;
    }
    if (runtime->in_alt_screen) {
        runtime_write(runtime, ANSI_EXIT_ALT_SCREEN);
        runtime->in_alt_screen = 0;
    }
    runtime_write(runtime, DECRC); /* Restore saved cursor position */

    fflush(runtime->output);
    runtime->started = 0;
}

/* Finish inline mode: move cursor past all rendered content so
 * application output appears below the input. The cursor may be on
 * any row (e.g. the user moved it up); we cursor-down to the last
 * rendered row, then write \r\n to start a fresh line for output.
 * Resets tracking so the next flush renders on a fresh line. */
void tui_runtime_finish_inline(TuiRuntime *runtime)
{
    if (!runtime || !runtime->in_inline_mode)
        return;

    FILE *fp = runtime->output;

    /* Move from current cursor row to the last rendered row */
    int last_row = runtime->inline_lines_rendered - 1;
    int down = last_row - runtime->inline_cursor_row;
    if (down > 0) {
        char down_buf[16];
        ansi_format_cursor_down(down_buf, sizeof(down_buf), down);
        fputs(down_buf, fp);
    }

    /* End the input line and start a fresh line for output */
    fputs("\r\n", fp);

    /* Reset so the next flush doesn't cursor-up into old content */
    runtime->inline_lines_rendered = 0;
    runtime->inline_cursor_row = 0;

    fflush(fp);
}

/* Render view, reconcile terminal mode against the View's declarations,
 * and write the resulting bytes.
 *
 * Bubbletea v2 alignment: components return a TuiView from view(). The
 * order matters — mode toggles MUST precede content so the content
 * lands on the right screen buffer / sees the right mouse mode.
 *
 * Output sequence:
 *   HIDE_CURSOR → mode toggles → content → window title → cursor placement → SHOW_CURSOR
 */
void tui_runtime_flush(TuiRuntime *runtime)
{
    if (!runtime || !runtime->component || !runtime->model)
        return;

    /* Render content into the view buffer. */
    DynamicBuffer *content = runtime->view_buf;
    dynamic_buffer_clear(content);
    TuiView v = runtime->component->view(runtime->model, content);

    FILE *fp = runtime->output;

    /* 1. Hide cursor while painting to prevent flicker. */
    fputs(ANSI_HIDE_CURSOR, fp);

    if (v.render_mode == TUI_RENDER_INLINE) {
        runtime->in_inline_mode = 1;

        /* Inline mode: no alt screen, no mouse, no kbd enhancements.
         * Only reconcile bracketed paste. */
        if (v.bracketed_paste != runtime->cur_bracketed_paste) {
            fputs(v.bracketed_paste ? ANSI_ENABLE_BRACKETED_PASTE
                                    : ANSI_DISABLE_BRACKETED_PASTE,
                  fp);
            runtime->cur_bracketed_paste = v.bracketed_paste;
        }

        /* Save previous state before overwriting */
        int prev_lines = runtime->inline_lines_rendered;
        int prev_cursor_row = runtime->inline_cursor_row;

        /* Move cursor up to where the previous frame's content started.
         * The cursor was placed at prev_cursor_row (0-indexed) after
         * the last flush. Go up prev_cursor_row lines to reach row 0. */
        if (prev_cursor_row > 0) {
            char up_buf[16];
            ansi_format_cursor_up(up_buf, sizeof(up_buf),
                                  prev_cursor_row);
            fputs(up_buf, fp);
        }

        /* Write content. */
        fwrite(dynamic_buffer_data(content), 1, dynamic_buffer_len(content), fp);

        /* Count visual lines rendered for next frame's cursor-up.
         * Each \n produces a line break, plus the initial line = \n count + 1.
         * But if content is empty, it's 1 line (the prompt only). */
        const char *data = dynamic_buffer_data(content);
        size_t len = dynamic_buffer_len(content);
        int line_count = 0;
        for (size_t i = 0; i < len; i++) {
            if (data[i] == '\n')
                line_count++;
        }
        /* Visual rows = newlines + 1 (the first line has no preceding \n) */
        if (len > 0)
            line_count++;
        runtime->inline_lines_rendered = line_count;

        /* Erase stale lines below the new content if the previous frame
         * had more lines than the current one. prev_lines was saved
         * before we overwrote inline_lines_rendered above. */
        if (prev_lines > line_count) {
            int stale = prev_lines - line_count;
            for (int i = 0; i < stale; i++) {
                fputs("\r\n" EL_TO_END, fp);
            }
            /* Move back up to the last content line */
            char up_buf[16];
            ansi_format_cursor_up(up_buf, sizeof(up_buf), stale);
            fputs(up_buf, fp);
        }

        /* Cursor placement — relative positioning in inline mode.
         * After writing content, cursor is at end of last line (row N-1
         * where N = line_count). Move up to the cursor's row, then
         * carriage return + forward to the cursor's column.
         * Save the cursor row for next frame's cursor-up. */
        if (v.cursor.visible) {
            int cursor_row_in_content = v.cursor.row - 1; /* 0-indexed */
            int last_row = line_count - 1;                /* 0-indexed last row */
            int lines_to_move_up = last_row - cursor_row_in_content;
            if (lines_to_move_up > 0) {
                char up_buf[16];
                ansi_format_cursor_up(up_buf, sizeof(up_buf),
                                      lines_to_move_up);
                fputs(up_buf, fp);
            }
            runtime->inline_cursor_row = cursor_row_in_content;
            /* Move to column: \r + cursor forward */
            fputs("\r", fp);
            if (v.cursor.col > 1) {
                char fwd_buf[16];
                ansi_format_cursor_fwd(fwd_buf, sizeof(fwd_buf),
                                       v.cursor.col - 1);
                fputs(fwd_buf, fp);
            }
            fputs(ANSI_SHOW_CURSOR, fp);
        } else {
            /* Cursor hidden — stays at end of last line */
            runtime->inline_cursor_row = line_count - 1;
        }

        fflush(fp);
        return;
    }

    /* --- Alt-screen mode (default) --- */

    /* 2. Reconcile mode toggles before content. */
    if (v.alt_screen != runtime->in_alt_screen) {
        fputs(v.alt_screen ? ANSI_ENTER_ALT_SCREEN : ANSI_EXIT_ALT_SCREEN, fp);
        runtime->in_alt_screen = v.alt_screen;
    }
    if (v.mouse_mode != runtime->cur_mouse_mode) {
        if (runtime->cur_mouse_mode != TUI_MOUSE_MODE_NONE)
            fputs(ANSI_DISABLE_MOUSE, fp);
        if (v.mouse_mode != TUI_MOUSE_MODE_NONE)
            fputs(ANSI_ENABLE_MOUSE, fp);
        runtime->cur_mouse_mode = v.mouse_mode;
    }
    if (v.kbd_enhancements != runtime->cur_kbd_enhancements) {
        if (runtime->cur_kbd_enhancements & TUI_KBD_KITTY)
            fputs(ANSI_DISABLE_KITTY_KBD, fp);
        if (v.kbd_enhancements & TUI_KBD_KITTY)
            fputs(ANSI_ENABLE_KITTY_KBD, fp);
        runtime->cur_kbd_enhancements = v.kbd_enhancements;
    }
    if (v.bracketed_paste != runtime->cur_bracketed_paste) {
        fputs(v.bracketed_paste ? ANSI_ENABLE_BRACKETED_PASTE
                                : ANSI_DISABLE_BRACKETED_PASTE,
              fp);
        runtime->cur_bracketed_paste = v.bracketed_paste;
    }

    /* 3. Content. */
    fwrite(dynamic_buffer_data(content), 1, dynamic_buffer_len(content), fp);

    /* 4. Window title. */
    if (v.window_title) {
        char title_buf[512];
        ansi_set_window_title(title_buf, sizeof(title_buf), v.window_title);
        fputs(title_buf, fp);
    }

    /* 5. Cursor placement. */
    if (v.cursor.visible) {
        char cur_buf[32];
        ansi_format_cursor_pos(cur_buf, sizeof(cur_buf), v.cursor.row,
                               v.cursor.col);
        fputs(cur_buf, fp);
        fputs(ANSI_SHOW_CURSOR, fp);
    }

    fflush(fp);
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
#else
    if (rt->wakeup_event)
        SetEvent(rt->wakeup_event);
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
    /* Windows event loop using WaitForMultipleObjects */
    runtime_update_size_win(runtime);
    TuiMsg size_msg = { .type = TUI_MSG_WINDOW_SIZE,
                        .data.size = { .width = runtime->term_width,
                                       .height = runtime->term_height } };
    tui_runtime_send(runtime, size_msg);
    if (runtime->config.on_resize)
        runtime->config.on_resize(runtime->term_width,
                                  runtime->term_height,
                                  runtime->config.event_data);

    /* Enable raw mode if configured */
    if (runtime->config.raw_mode)
        runtime_enable_raw_mode_win(runtime);

    /* Create wakeup event */
    runtime->wakeup_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    /* Start terminal mode */
    tui_runtime_start(runtime);
    tui_runtime_flush(runtime);

    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);

    while (runtime->running) {
        HANDLE handles[2];
        DWORD n_handles = 0;
        handles[n_handles++] = h_stdin;
        if (runtime->wakeup_event)
            handles[n_handles++] = runtime->wakeup_event;

        /* Compute timeout (100ms default) */
        DWORD timeout_ms = 100;
        if (runtime->config.get_tick_timeout_ms) {
            int ms = runtime->config.get_tick_timeout_ms(
                runtime->config.event_data);
            if (ms < 0)
                timeout_ms = INFINITE;
            else
                timeout_ms = (DWORD)ms;
        }

        DWORD wait = WaitForMultipleObjects(n_handles, handles, FALSE,
                                            timeout_ms);

        if (wait == WAIT_OBJECT_0) {
            /* stdin ready */
            unsigned char buf[STDIN_READ_BUF_SIZE];
            DWORD bytes_read = 0;
            if (ReadFile(h_stdin, buf, sizeof(buf), &bytes_read, NULL) &&
                bytes_read > 0) {
                tui_runtime_process_input(runtime, buf, (size_t)bytes_read);
                if (runtime->config.on_stdin_processed)
                    runtime->config.on_stdin_processed(
                        runtime->config.event_data);
                tui_runtime_flush(runtime);
            } else if (bytes_read == 0) {
                /* EOF */
                break;
            }
        } else if (wait == WAIT_OBJECT_0 + 1) {
            /* Wakeup event */
            ResetEvent(runtime->wakeup_event);
            tui_runtime_drain(runtime);
            tui_runtime_flush(runtime);
        }

        /* Tick */
        if (runtime->config.on_tick)
            runtime->config.on_tick(runtime->config.event_data);
    }

    /* Teardown */
    tui_runtime_stop(runtime);
    runtime_disable_raw_mode_win(runtime);

    if (runtime->wakeup_event) {
        CloseHandle(runtime->wakeup_event);
        runtime->wakeup_event = NULL;
    }

    return 0;
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
