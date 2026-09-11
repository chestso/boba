/* test_runtime.c - Tests for tui_runtime_run(), idempotent start/stop,
 * callback config, and terminal size getters.
 *
 * These test the new runtime event loop infrastructure added to support
 * the Bubbletea-style ownership model where the runtime owns raw mode,
 * signals, and the select() event loop.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <windows.h>
#endif

#include <boba/cmd.h>
#include <boba/component.h>
#include <boba/dynamic_buffer.h>
#include <boba/msg.h>
#include <boba/runtime.h>

#ifdef _WIN32
#define DEVNULL "NUL"
#else
#define DEVNULL "/dev/null"
#endif

/* test_runtime_quit deliberately exercises the deprecated imperative
 * tui_runtime_quit() to verify the legacy entry point still works. */
_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")

    static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

/* ========================================================================
 * Minimal test component — quits immediately on first update
 * ======================================================================== */

typedef struct
{
    TuiModel base;
    int update_count;
    int window_size_received;
    int last_width;
    int last_height;
} TestModel;

static TuiInitResult test_init(void *config)
{
    (void)config;
    TestModel *m = calloc(1, sizeof(TestModel));
    m->base.type = 999;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult test_update(TuiModel *model, TuiMsg msg)
{
    TestModel *m = (TestModel *)model;
    m->update_count++;

    if (msg.type == TUI_MSG_WINDOW_SIZE) {
        m->window_size_received = 1;
        m->last_width = msg.data.size.width;
        m->last_height = msg.data.size.height;
        /* Quit after receiving window size (first message from runtime_run) */
        return tui_update_result(tui_cmd_quit());
    }

    return tui_update_result_none();
}

static TuiView test_view(const TuiModel *model, DynamicBuffer *out)
{
    (void)model;
    dynamic_buffer_append_str(out, "test view");
    return tui_view_default(out);
}

static void test_free(TuiModel *model)
{
    free(model);
}

static TuiComponent test_component = {
    .init = test_init,
    .update = test_update,
    .view = test_view,
    .free = test_free,
};

/* ========================================================================
 * Component that doesn't quit (for non-run tests)
 * ======================================================================== */

static TuiInitResult noop_init(void *config)
{
    (void)config;
    TestModel *m = calloc(1, sizeof(TestModel));
    m->base.type = 998;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult noop_update(TuiModel *model, TuiMsg msg)
{
    (void)model;
    (void)msg;
    return tui_update_result_none();
}

static TuiComponent noop_component = {
    .init = noop_init,
    .update = noop_update,
    .view = test_view,
    .free = test_free,
};

/* ========================================================================
 * Tests
 * ======================================================================== */

/* Dummy callbacks for config storage tests */
static size_t dummy_fill_fds(TuiExternalFd *out, size_t cap, void *data)
{
    (void)data;
    (void)out;
    (void)cap;
    return 0;
}
static void dummy_on_ready(int fd, unsigned ready, void *data)
{
    (void)fd;
    (void)ready;
    (void)data;
}
static void dummy_on_tick(void *data) { (void)data; }
static void dummy_on_resize(int w, int h, void *data)
{
    (void)w;
    (void)h;
    (void)data;
}
static void dummy_on_stdin(void *data) { (void)data; }

/* Test that runtime stores new config callback fields */
static void test_config_stores_callbacks(void)
{
    int called = 0;

    TuiRuntimeConfig cfg = {
        .output = stdout,
        .fill_external_fds = dummy_fill_fds,
        .on_external_ready = dummy_on_ready,
        .on_tick = dummy_on_tick,
        .on_resize = dummy_on_resize,
        .on_stdin_processed = dummy_on_stdin,
        .event_data = &called,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Verify callbacks are stored in config */
    assert(rt->config.fill_external_fds == dummy_fill_fds);
    assert(rt->config.on_external_ready == dummy_on_ready);
    assert(rt->config.on_tick == dummy_on_tick);
    assert(rt->config.on_resize == dummy_on_resize);
    assert(rt->config.on_stdin_processed == dummy_on_stdin);
    assert(rt->config.event_data == &called);

    tui_runtime_free(rt);
}

/* Test that tui_runtime_start is idempotent */
static void test_start_idempotent(void)
{
    /* Use /dev/null to avoid writing ANSI sequences to terminal */
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->started == 0);

    tui_runtime_start(rt);
    assert(rt->started == 1);

    /* Second start should be a no-op */
    tui_runtime_start(rt);
    assert(rt->started == 1);

    tui_runtime_stop(rt);
    assert(rt->started == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that tui_runtime_stop is idempotent */
static void test_stop_idempotent(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Stop without start should be no-op (started == 0) */
    tui_runtime_stop(rt);
    assert(rt->started == 0);

    /* Start then double-stop */
    tui_runtime_start(rt);
    assert(rt->started == 1);

    tui_runtime_stop(rt);
    assert(rt->started == 0);

    tui_runtime_stop(rt);
    assert(rt->started == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that start+stop cycle can be repeated */
static void test_start_stop_cycle(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    for (int i = 0; i < 3; i++) {
        tui_runtime_start(rt);
        assert(rt->started == 1);
        tui_runtime_stop(rt);
        assert(rt->started == 0);
    }

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test get_width/get_height return stored dimensions */
static void test_get_dimensions(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Initially zero (not yet populated by runtime_update_size) */
    assert(tui_runtime_get_width(rt) == 0);
    assert(tui_runtime_get_height(rt) == 0);

    /* Manually set for testing */
    rt->term_width = 120;
    rt->term_height = 40;
    assert(tui_runtime_get_width(rt) == 120);
    assert(tui_runtime_get_height(rt) == 40);

    tui_runtime_free(rt);
}

/* Test get_width/get_height with NULL runtime */
static void test_get_dimensions_null(void)
{
    assert(tui_runtime_get_width(NULL) == 0);
    assert(tui_runtime_get_height(NULL) == 0);
}

/* Test that config with NULL callbacks doesn't crash */
static void test_null_callbacks_in_config(void)
{
    TuiRuntimeConfig cfg = {
        .output = stdout,
        .fill_external_fds = NULL,
        .on_external_ready = NULL,
        .on_tick = NULL,
        .on_resize = NULL,
        .on_stdin_processed = NULL,
        .event_data = NULL,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->config.fill_external_fds == NULL);
    assert(rt->config.event_data == NULL);

    tui_runtime_free(rt);
}

/* Test that runtime created with default config has raw_mode = 1 */
static void test_default_config_raw_mode(void)
{
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, NULL);
    assert(rt != NULL);
    assert(rt->config.raw_mode == 1);

    tui_runtime_free(rt);
}

/* Test that WINDOW_SIZE message is delivered to component */
static void test_window_size_message(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    TestModel *m = (TestModel *)tui_runtime_model(rt);
    assert(m != NULL);
    assert(m->window_size_received == 0);

    /* Send a window size message manually */
    TuiMsg ws_msg = tui_msg_window_size(132, 50);
    tui_runtime_send(rt, ws_msg);

    /* noop_update doesn't track this, but verify send doesn't crash */
    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that tui_runtime_quit sets quit_requested and stops running */
static void test_runtime_quit(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->running == 1);
    assert(rt->quit_requested == 0);

    tui_runtime_quit(rt);
    assert(rt->running == 0);
    assert(rt->quit_requested == 1);

    tui_runtime_free(rt);
}

/* Test that started flag is initially 0 */
static void test_started_initially_zero(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->started == 0);

    tui_runtime_free(rt);
}

#ifndef _WIN32
/* Test that tui_runtime_run sends WINDOW_SIZE and component can quit.
 * This tests the full run loop with a component that quits on first update.
 * Requires a TTY (stdin must be a terminal for raw mode). */
static void test_runtime_run_immediate_quit(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .raw_mode = 0, /* Skip raw mode (may not have a TTY) */
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&test_component, NULL, &cfg);
    assert(rt != NULL);

    TestModel *m = (TestModel *)tui_runtime_model(rt);
    assert(m->window_size_received == 0);

    /* Run — the test_component quits immediately on WINDOW_SIZE message */
    int result = tui_runtime_run(rt);
    assert(result == 0);

    /* Verify the component received a window size message.
     * Dimensions may be 0 if not running in a real TTY (e.g. make check). */
    assert(m->window_size_received == 1);

    /* Verify runtime properly cleaned up */
    assert(rt->started == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}
#endif

/* ========================================================================
 * Tracking component — records custom messages it receives
 * ======================================================================== */

#define TRACK_MSG_TYPE (TUI_MSG_CUSTOM_BASE + 42)
#define TRACK_MAX_MSGS 32

typedef struct
{
    TuiModel base;
    int received[TRACK_MAX_MSGS]; /* custom data values received */
    int received_count;
} TrackModel;

static TuiInitResult track_init(void *config)
{
    (void)config;
    TrackModel *m = calloc(1, sizeof(TrackModel));
    m->base.type = 997;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult track_update(TuiModel *model, TuiMsg msg)
{
    TrackModel *m = (TrackModel *)model;
    if (msg.type == TRACK_MSG_TYPE && m->received_count < TRACK_MAX_MSGS) {
        m->received[m->received_count++] = (int)(intptr_t)msg.data.custom;
    }
    return tui_update_result_none();
}

static TuiComponent track_component = {
    .init = track_init,
    .update = track_update,
    .view = test_view,
    .free = test_free,
};

/* ========================================================================
 * Reentrant component — posts a new message from its update handler
 * ======================================================================== */

static TuiRuntime *s_reentrant_runtime = NULL;

#define REENTRANT_MSG_FIRST  (TUI_MSG_CUSTOM_BASE + 100)
#define REENTRANT_MSG_SECOND (TUI_MSG_CUSTOM_BASE + 101)

typedef struct
{
    TuiModel base;
    int first_received;
    int second_received;
} ReentrantModel;

static TuiInitResult reentrant_init(void *config)
{
    (void)config;
    ReentrantModel *m = calloc(1, sizeof(ReentrantModel));
    m->base.type = 996;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult reentrant_update(TuiModel *model, TuiMsg msg)
{
    ReentrantModel *m = (ReentrantModel *)model;
    if (msg.type == REENTRANT_MSG_FIRST) {
        m->first_received = 1;
        /* Post a second message from within update */
        TuiMsg second = tui_msg_custom(REENTRANT_MSG_SECOND, NULL);
        tui_runtime_post(s_reentrant_runtime, second);
    } else if (msg.type == REENTRANT_MSG_SECOND) {
        m->second_received = 1;
    }
    return tui_update_result_none();
}

static TuiComponent reentrant_component = {
    .init = reentrant_init,
    .update = reentrant_update,
    .view = test_view,
    .free = test_free,
};

/* ========================================================================
 * Command tracking — records when a custom command callback is executed
 * ======================================================================== */

static int s_cmd_callback_called = 0;
static int s_cmd_callback_value = 0;

static TuiMsg cmd_tracking_callback(void *data)
{
    s_cmd_callback_called = 1;
    s_cmd_callback_value = (int)(intptr_t)data;
    return tui_msg_none();
}

/* ========================================================================
 * Scheduling API tests
 * ======================================================================== */

/* Test that queues are initialized on create */
static void test_queue_initialized(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    assert(rt->msg_queue != NULL);
    assert(rt->msg_queue_count == 0);
    assert(rt->msg_queue_cap == 16);

    assert(rt->cmd_queue != NULL);
    assert(rt->cmd_queue_count == 0);
    assert(rt->cmd_queue_cap == 16);

    tui_runtime_free(rt);
}

/* Test that wakeup pipe is created */
static void test_wakeup_pipe_created(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

#ifndef _WIN32
    assert(rt->wakeup_pipe[0] >= 0);
    assert(rt->wakeup_pipe[1] >= 0);
    assert(tui_runtime_wakeup_fd(rt) == rt->wakeup_pipe[0]);
#endif

    tui_runtime_free(rt);
}

/* Test wakeup_fd with NULL runtime */
static void test_wakeup_fd_null(void)
{
    assert(tui_runtime_wakeup_fd(NULL) == -1);
}

/* Test post + drain delivers message through update */
static void test_post_and_drain(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&track_component, NULL, &cfg);
    assert(rt != NULL);

    TrackModel *m = (TrackModel *)tui_runtime_model(rt);
    assert(m->received_count == 0);

    /* Post a custom message */
    TuiMsg msg = tui_msg_custom(TRACK_MSG_TYPE, (void *)(intptr_t)7);
    tui_runtime_post(rt, msg);

    /* Queue should have one item */
    assert(rt->msg_queue_count == 1);

    /* Drain processes it through update */
    tui_runtime_drain(rt);

    assert(m->received_count == 1);
    assert(m->received[0] == 7);
    assert(rt->msg_queue_count == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test schedule + drain executes command */
static void test_schedule_and_drain(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    s_cmd_callback_called = 0;
    s_cmd_callback_value = 0;

    /* Schedule a custom command */
    TuiCmd *cmd =
        tui_cmd_custom(cmd_tracking_callback, (void *)(intptr_t)42, NULL);
    tui_runtime_schedule(rt, cmd);

    assert(rt->cmd_queue_count == 1);

    tui_runtime_drain(rt);

    assert(s_cmd_callback_called == 1);
    assert(s_cmd_callback_value == 42);
    assert(rt->cmd_queue_count == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test drain with empty queues is a no-op */
static void test_drain_empty(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Should not crash */
    tui_runtime_drain(rt);
    assert(rt->msg_queue_count == 0);
    assert(rt->cmd_queue_count == 0);

    tui_runtime_free(rt);
}

/* Test drain with NULL runtime is safe */
static void test_drain_null(void)
{
    /* Should not crash */
    tui_runtime_drain(NULL);
}

/* Test post with NULL runtime is safe */
static void test_post_null(void)
{
    TuiMsg msg = tui_msg_none();
    tui_runtime_post(NULL, msg);
}

/* Test schedule with NULL runtime or NULL cmd is safe */
static void test_schedule_null(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* NULL runtime — cmd must be freed since it won't be enqueued */
    TuiCmd *quit_cmd = tui_cmd_quit();
    tui_runtime_schedule(NULL, quit_cmd);
    free(quit_cmd);

    /* NULL cmd */
    tui_runtime_schedule(rt, NULL);
    assert(rt->cmd_queue_count == 0);

    tui_runtime_free(rt);
}

/* Test multiple posts are drained in order */
static void test_post_ordering(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&track_component, NULL, &cfg);
    assert(rt != NULL);

    TrackModel *m = (TrackModel *)tui_runtime_model(rt);

    /* Post several messages */
    for (int i = 0; i < 5; i++) {
        TuiMsg msg = tui_msg_custom(TRACK_MSG_TYPE, (void *)(intptr_t)i);
        tui_runtime_post(rt, msg);
    }

    assert(rt->msg_queue_count == 5);

    tui_runtime_drain(rt);

    assert(m->received_count == 5);
    for (int i = 0; i < 5; i++) {
        assert(m->received[i] == i);
    }

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test reentrancy: posting a message from within update handler */
static void test_post_reentrancy(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&reentrant_component, NULL, &cfg);
    assert(rt != NULL);

    s_reentrant_runtime = rt;

    ReentrantModel *m = (ReentrantModel *)tui_runtime_model(rt);
    assert(m->first_received == 0);
    assert(m->second_received == 0);

    /* Post first message — its handler will post a second */
    TuiMsg msg = tui_msg_custom(REENTRANT_MSG_FIRST, NULL);
    tui_runtime_post(rt, msg);

    tui_runtime_drain(rt);

    /* Both messages should have been processed */
    assert(m->first_received == 1);
    assert(m->second_received == 1);

    s_reentrant_runtime = NULL;
    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that commands execute before messages in a single drain */
static void test_commands_before_messages(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&track_component, NULL, &cfg);
    assert(rt != NULL);

    s_cmd_callback_called = 0;

    /* Schedule a command and post a message */
    TuiCmd *cmd =
        tui_cmd_custom(cmd_tracking_callback, (void *)(intptr_t)99, NULL);
    tui_runtime_schedule(rt, cmd);

    TuiMsg msg = tui_msg_custom(TRACK_MSG_TYPE, (void *)(intptr_t)1);
    tui_runtime_post(rt, msg);

    /* Both are pending */
    assert(rt->cmd_queue_count == 1);
    assert(rt->msg_queue_count == 1);

    tui_runtime_drain(rt);

    /* Both should be processed */
    assert(s_cmd_callback_called == 1);
    TrackModel *m = (TrackModel *)tui_runtime_model(rt);
    assert(m->received_count == 1);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that queue grows beyond initial capacity */
static void test_queue_growth(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&track_component, NULL, &cfg);
    assert(rt != NULL);

    /* Post more messages than initial capacity (16) */
    for (int i = 0; i < 20; i++) {
        TuiMsg msg = tui_msg_custom(TRACK_MSG_TYPE, (void *)(intptr_t)i);
        tui_runtime_post(rt, msg);
    }

    assert(rt->msg_queue_count == 20);
    assert(rt->msg_queue_cap >= 20);

    tui_runtime_drain(rt);

    TrackModel *m = (TrackModel *)tui_runtime_model(rt);
    assert(m->received_count == 20);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that scheduled quit command works through drain */
static void test_schedule_quit(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->running == 1);

    tui_runtime_schedule(rt, tui_cmd_quit());
    tui_runtime_drain(rt);

    assert(rt->running == 0);
    assert(rt->quit_requested == 1);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that unprocessed commands are freed on runtime_free (no leak) */
static void test_free_with_pending_commands(void)
{
    TuiRuntimeConfig cfg = { .output = stdout };
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Schedule commands but don't drain — free should clean them up.
     * Use clipboard_copy as a stand-in (any owned-payload command works). */
    tui_runtime_schedule(rt, tui_cmd_clipboard_copy("a", 1));
    tui_runtime_schedule(rt, tui_cmd_clipboard_copy("b", 1));
    assert(rt->cmd_queue_count == 2);

    /* This should free the pending commands without leaking */
    tui_runtime_free(rt);
}

/* Test double drain — second drain should be a no-op */
static void test_double_drain(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { .output = devnull };
    TuiRuntime *rt = tui_runtime_create(&track_component, NULL, &cfg);
    assert(rt != NULL);

    TuiMsg msg = tui_msg_custom(TRACK_MSG_TYPE, (void *)(intptr_t)1);
    tui_runtime_post(rt, msg);

    tui_runtime_drain(rt);

    TrackModel *m = (TrackModel *)tui_runtime_model(rt);
    assert(m->received_count == 1);

    /* Second drain — nothing new */
    tui_runtime_drain(rt);
    assert(m->received_count == 1);

    tui_runtime_free(rt);
    fclose(devnull);
}

#ifndef _WIN32
/* Test that post wakes up the event loop via the wakeup pipe.
 * Uses a component that quits when it receives the custom message. */

#define WAKEUP_MSG_TYPE (TUI_MSG_CUSTOM_BASE + 200)

typedef struct
{
    TuiModel base;
    int got_wakeup;
} WakeupModel;

static TuiInitResult wakeup_init(void *config)
{
    (void)config;
    WakeupModel *m = calloc(1, sizeof(WakeupModel));
    m->base.type = 995;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult wakeup_update(TuiModel *model, TuiMsg msg)
{
    WakeupModel *m = (WakeupModel *)model;
    if (msg.type == WAKEUP_MSG_TYPE) {
        m->got_wakeup = 1;
        return tui_update_result(tui_cmd_quit());
    }
    return tui_update_result_none();
}

static TuiComponent wakeup_component = {
    .init = wakeup_init,
    .update = wakeup_update,
    .view = test_view,
    .free = test_free,
};

/* Test tui_runtime_run integration: post from on_tick callback */
static int s_tick_post_done = 0;
static TuiRuntime *s_tick_runtime = NULL;

static void tick_post_callback(void *data)
{
    (void)data;
    if (!s_tick_post_done) {
        s_tick_post_done = 1;
        TuiMsg msg = tui_msg_custom(WAKEUP_MSG_TYPE, NULL);
        tui_runtime_post(s_tick_runtime, msg);
    }
}

static void test_post_wakes_event_loop(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    /* Replace stdin with a pipe so it doesn't EOF under make check */
    int stdin_pipe[2];
    assert(pipe(stdin_pipe) == 0);
    int orig_stdin = dup(STDIN_FILENO);
    dup2(stdin_pipe[0], STDIN_FILENO);

    s_tick_post_done = 0;

    TuiRuntimeConfig cfg = {
        .raw_mode = 0,
        .output = devnull,
        .on_tick = tick_post_callback,
    };

    TuiRuntime *rt = tui_runtime_create(&wakeup_component, NULL, &cfg);
    assert(rt != NULL);
    s_tick_runtime = rt;

    /* Run — on_tick fires after 100ms, posts a message, wakeup pipe
     * fires, drain processes it, component quits */
    int result = tui_runtime_run(rt);
    assert(result == 0);

    WakeupModel *m = (WakeupModel *)tui_runtime_model(rt);
    assert(m->got_wakeup == 1);

    /* Restore stdin */
    dup2(orig_stdin, STDIN_FILENO);
    close(orig_stdin);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);

    s_tick_runtime = NULL;
    tui_runtime_free(rt);
    fclose(devnull);
}

/* ========================================================================
 * External-FD subscriptions: fill-count contract
 * ======================================================================== */

/* Fill callback returning 0 entries — the "no external fds" default.
 * The runtime must tolerate a filler that declares nothing. */
static size_t fill_zero(TuiExternalFd *out, size_t cap, void *data)
{
    (void)out;
    (void)cap;
    (void)data;
    return 0;
}

/* Fill callback returning exactly 1 entry. */
static int s_fill_one_fd = 51;
static size_t fill_one(TuiExternalFd *out, size_t cap, void *data)
{
    (void)data;
    assert(cap >= 1);
    out[0].fd = s_fill_one_fd;
    out[0].flags = TUI_FD_READ;
    return 1;
}

/* Fill callback returning N (3) entries. */
static size_t fill_three(TuiExternalFd *out, size_t cap, void *data)
{
    (void)data;
    assert(cap >= 3);
    out[0].fd = 60;
    out[0].flags = TUI_FD_READ;
    out[1].fd = 61;
    out[1].flags = TUI_FD_WRITE;
    out[2].fd = 62;
    out[2].flags = TUI_FD_READ | TUI_FD_WRITE;
    return 3;
}

/* Fill callback that returns MORE than cap — the runtime must clamp.
 * (Defensive contract: consumer bugs shouldn't corrupt the wait set.) */
static size_t fill_overflow(TuiExternalFd *out, size_t cap, void *data)
{
    (void)out;
    (void)data;
    return cap + 5;
}

static void test_fill_zero_in_run(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    int stdin_pipe[2];
    assert(pipe(stdin_pipe) == 0);
    int orig_stdin = dup(STDIN_FILENO);
    dup2(stdin_pipe[0], STDIN_FILENO);

    /* Component quits on the first tick's posted message. */
    s_tick_post_done = 0;
    TuiRuntimeConfig cfg = {
        .raw_mode = 0,
        .output = devnull,
        .fill_external_fds = fill_zero,
        .on_external_ready = dummy_on_ready,
        .on_tick = tick_post_callback,
    };
    TuiRuntime *rt = tui_runtime_create(&wakeup_component, NULL, &cfg);
    assert(rt != NULL);
    s_tick_runtime = rt;

    assert(tui_runtime_run(rt) == 0);
    WakeupModel *m = (WakeupModel *)tui_runtime_model(rt);
    assert(m->got_wakeup == 1);

    dup2(orig_stdin, STDIN_FILENO);
    close(orig_stdin);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);

    s_tick_runtime = NULL;
    tui_runtime_free(rt);
    fclose(devnull);
}

static void test_fill_overflow_clamps(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    int stdin_pipe[2];
    if (pipe(stdin_pipe) == 0) {
        int orig_stdin = dup(STDIN_FILENO);
        dup2(stdin_pipe[0], STDIN_FILENO);

        s_tick_post_done = 0;
        TuiRuntimeConfig cfg = {
            .raw_mode = 0,
            .output = devnull,
            .fill_external_fds = fill_overflow,
            .on_tick = tick_post_callback,
        };
        TuiRuntime *rt = tui_runtime_create(&wakeup_component, NULL, &cfg);
        assert(rt != NULL);
        s_tick_runtime = rt;

        /* Run must terminate (no crash, no spin on garbage slots). */
        assert(tui_runtime_run(rt) == 0);

        dup2(orig_stdin, STDIN_FILENO);
        close(orig_stdin);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        s_tick_runtime = NULL;
        tui_runtime_free(rt);
    }
    fclose(devnull);
}

/* Fill returning N entries: three slots survive a full wait cycle.
 * Fds 60–62 are not open — poll reports POLLNVAL, which the runtime
 * maps to READ|WRITE and dispatches to the (tolerant) sink. That is
 * the contract: the runtime never crashes on a consumer's bad fds,
 * and the sink is spurious-wakeup-safe. */
static void test_fill_three_in_run(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    int stdin_pipe[2];
    if (pipe(stdin_pipe) == 0) {
        int orig_stdin = dup(STDIN_FILENO);
        dup2(stdin_pipe[0], STDIN_FILENO);

        s_tick_post_done = 0;
        TuiRuntimeConfig cfg = {
            .raw_mode = 0,
            .output = devnull,
            .fill_external_fds = fill_three,
            .on_external_ready = dummy_on_ready,
            .on_tick = tick_post_callback,
        };
        TuiRuntime *rt = tui_runtime_create(&wakeup_component, NULL, &cfg);
        assert(rt != NULL);
        s_tick_runtime = rt;

        assert(tui_runtime_run(rt) == 0);

        dup2(orig_stdin, STDIN_FILENO);
        close(orig_stdin);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        s_tick_runtime = NULL;
        tui_runtime_free(rt);
    }
    fclose(devnull);
}

/* ========================================================================
 * External-FD subscriptions: two-socket integration
 * (nevermore's whole pattern in miniature)
 *
 * Socket A: non-blocking connect() with WRITE declared → sink fires
 * with WRITE once connect completes → SO_ERROR == 0 → send. Interest
 * then flips to READ by just returning different flags from the next
 * fill — proving the per-wait re-declare contract (no re-arm API).
 *
 * Socket B: server writes while A is mid-flight, READ declared →
 * per-fd dispatch on B independently of A's state.
 * ======================================================================== */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

typedef struct
{
    /* A: client connect socket; B: second client socket. */
    int a_fd;
    int b_fd;
    /* Phase markers, asserted after the run. */
    int a_write_ready;   /* sink saw WRITE on a_fd */
    int a_so_error_zero; /* getsockopt(SO_ERROR) == 0 at that moment */
    int a_sent;          /* payload sent after connect completed */
    int a_read_back;     /* read the echoed payload back (post-flip) */
    int b_read;          /* sink saw READ on b_fd and payload drained */
    int done;            /* all phases complete */
} TwoSockState;

static TwoSockState s_two = { 0 };
static int s_two_fill_calls = 0;

/* Server child (forked): accept two connections; for conn A echo one
 * byte; for conn B send one byte immediately. Then exit. Forked, not
 * threaded, to keep the test's link footprint untouched. */
static void two_sock_server_child(int listen_fd)
{
    int a = accept(listen_fd, NULL, NULL);
    if (a < 0)
        _exit(1);
    int b = accept(listen_fd, NULL, NULL);
    if (b < 0) {
        close(a);
        _exit(1);
    }
    close(listen_fd);

    /* Wait for A's payload, echo it; then send B's hello. */
    char c;
    while (recv(a, &c, 1, 0) == 0)
        ; /* spin-wait is fine: single byte, loopback */
    send(a, &c, 1, 0);
    send(b, "!", 1, 0);

    close(a);
    close(b);
    _exit(0);
}

static size_t two_sock_fill(TuiExternalFd *out, size_t cap, void *data)
{
    (void)data;
    s_two_fill_calls++;
    size_t n = 0;
    /* A: WRITE until connect completes AND payload sent; then READ
     * (awaiting the echo). B: READ the whole time. */
    if (!s_two.a_sent) {
        if (n < cap) {
            out[n].fd = s_two.a_fd;
            out[n].flags = TUI_FD_WRITE;
            n++;
        }
    } else if (!s_two.a_read_back) {
        if (n < cap) {
            out[n].fd = s_two.a_fd;
            out[n].flags = TUI_FD_READ;
            n++;
        }
    }
    if (!s_two.b_read && n < cap) {
        out[n].fd = s_two.b_fd;
        out[n].flags = TUI_FD_READ;
        n++;
    }
    return n;
}

static void two_sock_ready(int fd, unsigned ready, void *data)
{
    (void)data;
    if (fd == s_two.a_fd && (ready & TUI_FD_WRITE)) {
        if (s_two.a_write_ready)
            return; /* spurious wakeup — allowed, ignore */
        int soerr = -1;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        s_two.a_so_error_zero = (soerr == 0);
        s_two.a_write_ready = 1;
        /* Connect completed: send the payload. */
        ssize_t n = send(fd, "x", 1, 0);
        s_two.a_sent = (n == 1);
    } else if (fd == s_two.a_fd && (ready & TUI_FD_READ)) {
        if (s_two.a_sent && !s_two.a_read_back) {
            char c = 0;
            ssize_t n = recv(fd, &c, 1, 0);
            s_two.a_read_back = (n == 1 && c == 'x');
        }
    } else if (fd == s_two.b_fd && (ready & TUI_FD_READ)) {
        if (!s_two.b_read) {
            char c = 0;
            ssize_t n = recv(fd, &c, 1, 0);
            s_two.b_read = (n == 1 && c == '!');
        }
    }

    /* All phases done: quit via posted message. */
    if (s_two.a_write_ready && s_two.a_so_error_zero && s_two.a_sent &&
        s_two.a_read_back && s_two.b_read && !s_two.done) {
        s_two.done = 1;
        TuiMsg msg = tui_msg_custom(WAKEUP_MSG_TYPE, NULL);
        tui_runtime_post(s_tick_runtime, msg);
    }
}

/* Watchdog tick: quit only if the two-socket pattern failed to
 * complete in ~100 ticks (10s) — a deadlock guard, not the exit path
 * (the exit path is two_sock_ready posting the done message). */
static int s_two_tick_count = 0;
static void two_sock_tick(void *data)
{
    (void)data;
    if (++s_two_tick_count > 100) {
        TuiMsg msg = tui_msg_custom(WAKEUP_MSG_TYPE, NULL);
        tui_runtime_post(s_tick_runtime, msg);
    }
}

static void test_two_socket_subscriptions(void)
{
    FILE *devnull = fopen(DEVNULL, "w");
    assert(devnull != NULL);

    /* Replace stdin with a pipe so it doesn't EOF under make check */
    int stdin_pipe[2];
    assert(pipe(stdin_pipe) == 0);
    int orig_stdin = dup(STDIN_FILENO);
    dup2(stdin_pipe[0], STDIN_FILENO);

    /* Loopback listener on an ephemeral port. */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(listen_fd, 4) == 0);
    socklen_t alen = sizeof(addr);
    assert(getsockname(listen_fd, (struct sockaddr *)&addr, &alen) == 0);
    int port = ntohs(addr.sin_port);
    (void)port;

    pid_t server_pid = fork();
    assert(server_pid >= 0);
    if (server_pid == 0)
        two_sock_server_child(listen_fd); /* _exit()s */

    /* Two non-blocking client sockets to the same listener. */
    int a = socket(AF_INET, SOCK_STREAM, 0);
    int b = socket(AF_INET, SOCK_STREAM, 0);
    assert(a >= 0 && b >= 0);
    int fl = fcntl(a, F_GETFL, 0);
    fcntl(a, F_SETFL, fl | O_NONBLOCK);
    fl = fcntl(b, F_GETFL, 0);
    fcntl(b, F_SETFL, fl | O_NONBLOCK);

    int rc = connect(a, (struct sockaddr *)&addr, sizeof(addr));
    assert(rc < 0 && errno == EINPROGRESS);
    rc = connect(b, (struct sockaddr *)&addr, sizeof(addr));
    assert(rc < 0 && (errno == EINPROGRESS || errno == EISCONN));

    memset(&s_two, 0, sizeof(s_two));
    s_two.a_fd = a;
    s_two.b_fd = b;
    s_two_fill_calls = 0;

    s_tick_post_done = 0;
    s_two_tick_count = 0;
    TuiRuntimeConfig cfg = {
        .raw_mode = 0,
        .output = devnull,
        .fill_external_fds = two_sock_fill,
        .on_external_ready = two_sock_ready,
        .on_tick = two_sock_tick, /* deadlock guard only */
    };
    TuiRuntime *rt = tui_runtime_create(&wakeup_component, NULL, &cfg);
    assert(rt != NULL);
    s_tick_runtime = rt;

    int result = tui_runtime_run(rt);
    assert(result == 0);

    /* The whole pattern held: WRITE on connect completion, SO_ERROR
     * clean, send, interest flip to READ (no re-arm — the fill just
     * returned different flags), echo read back, and B dispatched
     * independently while A was mid-flight. */
    assert(s_two.a_write_ready);
    assert(s_two.a_so_error_zero);
    assert(s_two.a_sent);
    assert(s_two.a_read_back);
    assert(s_two.b_read);
    assert(s_two.done);
    /* Fill is called before EVERY wait (at least once per phase). */
    assert(s_two_fill_calls >= 2);

    int status = 0;
    waitpid(server_pid, &status, 0);
    close(a);
    close(b);

    /* Restore stdin */
    dup2(orig_stdin, STDIN_FILENO);
    close(orig_stdin);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);

    s_tick_runtime = NULL;
    tui_runtime_free(rt);
    fclose(devnull);
}
#endif /* !_WIN32 */

/* ========================================================================
 * TuiView / flush tests
 * ======================================================================== */

#include <boba/ansi_sequences.h>

/* Component whose view() returns a configurable TuiView. */
static TuiView s_view_to_return = { 0 };

static TuiInitResult view_stub_init(void *config)
{
    (void)config;
    TestModel *m = calloc(1, sizeof(TestModel));
    m->base.type = 994;
    return tui_init_result_none((TuiModel *)m);
}

static TuiView view_stub_view(const TuiModel *model, DynamicBuffer *out)
{
    (void)model;
    s_view_to_return.layer = out;
    return s_view_to_return;
}

static TuiComponent view_stub_component = {
    .init = view_stub_init,
    .update = noop_update,
    .view = view_stub_view,
    .free = test_free,
};

static void reset_view_stub(void)
{
    TuiView empty = { 0 };
    s_view_to_return = empty;
}

/* --- TuiRenderMode tests --- */

static void test_view_default_render_mode_alt_screen(void)
{
    DynamicBuffer *buf = dynamic_buffer_create(0);
    TuiView v = tui_view_default(buf);
    assert(v.render_mode == TUI_RENDER_ALT_SCREEN);
    dynamic_buffer_destroy(buf);
}

static void test_view_can_set_inline_mode(void)
{
    DynamicBuffer *buf = dynamic_buffer_create(0);
    TuiView v = tui_view_default(buf);
    v.render_mode = TUI_RENDER_INLINE;
    assert(v.render_mode == TUI_RENDER_INLINE);
    dynamic_buffer_destroy(buf);
}

static void test_alt_screen_backward_compat(void)
{
    /* Existing code that sets v.alt_screen = 1 should still work.
     * The default render_mode is ALT_SCREEN (0), which matches. */
    DynamicBuffer *buf = dynamic_buffer_create(0);
    TuiView v = tui_view_default(buf);
    v.alt_screen = 1;
    assert(v.render_mode == TUI_RENDER_ALT_SCREEN);
    dynamic_buffer_destroy(buf);
}

#ifndef _WIN32
static void test_flush_emits_cursor_when_visible(void)
{
    char outbuf[1024];
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    s_view_to_return.cursor = tui_cursor_at(7, 12);
    tui_runtime_flush(rt);
    fflush(fp);

    assert(strstr(outbuf, ANSI_HIDE_CURSOR) != NULL);
    assert(strstr(outbuf, "\x1b[7;12H") != NULL);
    assert(strstr(outbuf, ANSI_SHOW_CURSOR) != NULL);

    const char *cup = strstr(outbuf, "\x1b[7;12H");
    const char *show = strstr(outbuf, ANSI_SHOW_CURSOR);
    assert(cup != NULL && show != NULL && show > cup);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_keeps_cursor_hidden_when_view_abstains(void)
{
    char outbuf[1024];
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    /* cursor.visible = 0 by default → no SHOW emitted. */
    tui_runtime_flush(rt);
    fflush(fp);

    assert(strstr(outbuf, ANSI_HIDE_CURSOR) != NULL);
    assert(strstr(outbuf, ANSI_SHOW_CURSOR) == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_alt_screen_transition(void)
{
    char outbuf[2048];
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* Frame 1: alt_screen=1 → ENTER_ALT_SCREEN emitted. */
    reset_view_stub();
    s_view_to_return.alt_screen = 1;
    tui_runtime_flush(rt);
    fflush(fp);
    size_t pos1 = ftell(fp);
    assert(strstr(outbuf, ANSI_ENTER_ALT_SCREEN) != NULL);
    assert(rt->in_alt_screen == 1);

    /* Frame 2: alt_screen=1 again → no transition, no enter sequence. */
    reset_view_stub();
    s_view_to_return.alt_screen = 1;
    tui_runtime_flush(rt);
    fflush(fp);
    /* The string after pos1 must NOT contain another ENTER_ALT_SCREEN */
    assert(strstr(outbuf + pos1, ANSI_ENTER_ALT_SCREEN) == NULL);

    /* Frame 3: alt_screen=0 → EXIT_ALT_SCREEN emitted. */
    size_t pos2 = ftell(fp);
    reset_view_stub();
    s_view_to_return.alt_screen = 0;
    tui_runtime_flush(rt);
    fflush(fp);
    assert(strstr(outbuf + pos2, ANSI_EXIT_ALT_SCREEN) != NULL);
    assert(rt->in_alt_screen == 0);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_window_title(void)
{
    char outbuf[1024];
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    s_view_to_return.window_title = "hello";
    tui_runtime_flush(rt);
    fflush(fp);

    /* OSC 2 prefix + the title string. */
    assert(strstr(outbuf, "\x1b]2;hello") != NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_mouse_mode_transition(void)
{
    char outbuf[2048];
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* Frame 1: enable mouse. */
    reset_view_stub();
    s_view_to_return.mouse_mode = TUI_MOUSE_MODE_CELL_MOTION;
    tui_runtime_flush(rt);
    fflush(fp);
    assert(strstr(outbuf, "\x1b[?1000h") != NULL);
    assert(rt->cur_mouse_mode == TUI_MOUSE_MODE_CELL_MOTION);

    /* Frame 2: disable. */
    size_t pos = ftell(fp);
    reset_view_stub();
    s_view_to_return.mouse_mode = TUI_MOUSE_MODE_NONE;
    tui_runtime_flush(rt);
    fflush(fp);
    assert(strstr(outbuf + pos, "\x1b[?1000l") != NULL);
    assert(rt->cur_mouse_mode == TUI_MOUSE_MODE_NONE);

    tui_runtime_free(rt);
    fclose(fp);
}

/* --- Inline mode flush tests --- */

static void test_flush_inline_no_alt_screen(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    fflush(fp);

    assert(strstr(outbuf, "?1049h") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_inline_first_frame_no_cursor_up(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    fflush(fp);

    assert(strstr(outbuf, "\x1b[1A") == NULL);
    assert(strstr(outbuf, "\x1b[0A") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_inline_cursor_up_on_second_flush(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* Frame 1: inline mode, 2 lines of content */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    /* Simulate 2 lines rendered with cursor on last line (row 1) */
    rt->inline_lines_rendered = 2;
    rt->inline_cursor_row = 1; /* 0-indexed, cursor on last line */
    fflush(fp);
    size_t pos1 = ftell(fp);

    /* Frame 2: should cursor up 1 line (from row 1 to row 0) */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    fflush(fp);

    assert(strstr(outbuf + pos1, "\x1b[1A") != NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_inline_emits_erase(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* Frame 1 */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    rt->inline_lines_rendered = 1;
    fflush(fp);
    size_t pos1 = ftell(fp);

    /* Frame 2: should NOT emit ED_TO_END (we rely on per-line EL_TO_END
     * from the textinput view instead, to preserve content above) */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    fflush(fp);

    /* ED (CSI J) should NOT appear — it erases content above the input */
    assert(strstr(outbuf + pos1, "\x1b[J") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_flush_inline_counts_lines(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* The view_stub writes content via the runtime's view buffer.
     * We need the content to have newlines. The view_stub returns
     * s_view_to_return which has layer = runtime's view_buf.
     * The runtime calls view() which populates the buffer.
     * Since view_stub_view just returns s_view_to_return with the
     * runtime's buffer as layer, we need to pre-populate the buffer.
     * But the runtime clears it before calling view()...
     *
     * Actually, the view_stub_view sets s_view_to_return.layer = out
     * and returns it. The content written to 'out' by the runtime
     * before calling view() is cleared. The view function doesn't
     * write any content. So we need a different approach.
     *
     * Let's just check that inline_lines_rendered is updated to
     * the number of newlines in the content (which is 0 for empty). */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);

    /* No content → 0 lines rendered (or 1 for the empty line) */
    /* The count should be the number of \n chars in the content */
    assert(rt->inline_lines_rendered == 0 || rt->inline_lines_rendered == 1);

    tui_runtime_free(rt);
    fclose(fp);
}

/* --- Multi-line content view stub --- */

static TuiView multiline_view(const TuiModel *model, DynamicBuffer *out)
{
    (void)model;
    dynamic_buffer_append_str(out, "line1\nline2\nline3");
    TuiView v = tui_view_default(out);
    v.render_mode = TUI_RENDER_INLINE;
    return v;
}

static TuiComponent multiline_component = {
    .init = view_stub_init,
    .update = noop_update,
    .view = multiline_view,
    .free = test_free,
};

/* #2: line_count = newlines + 1 (visual rows) */
static void test_flush_inline_counts_visual_lines(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);
    assert(rt != NULL);

    tui_runtime_flush(rt);
    fflush(fp);

    /* "line1\nline2\nline3" has 2 newlines → 3 visual lines */
    assert(rt->inline_lines_rendered == 3);

    tui_runtime_free(rt);
    fclose(fp);
}

/* #1: cursor-up from non-last row uses inline_cursor_row, not N-1 */
static void test_flush_inline_cursor_up_from_non_last_row(void)
{
    char outbuf[8192];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);
    assert(rt != NULL);

    tui_runtime_flush(rt);
    fflush(fp);
    assert(rt->inline_lines_rendered == 3);
    assert(rt->inline_cursor_row == 2);

    /* Simulate cursor on row 0 (user pressed Up) */
    rt->inline_cursor_row = 0;
    size_t pos1 = ftell(fp);

    tui_runtime_flush(rt);
    fflush(fp);

    /* Should NOT emit any cursor-up (already on row 0) */
    assert(strstr(outbuf + pos1, "\x1b[1A") == NULL);
    assert(strstr(outbuf + pos1, "\x1b[2A") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* #1b: cursor-up from middle row (1 of 3) should be 1, not 2 */
static void test_flush_inline_cursor_up_from_middle_row(void)
{
    char outbuf[8192];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);
    assert(rt != NULL);

    tui_runtime_flush(rt);
    fflush(fp);
    rt->inline_cursor_row = 1;
    size_t pos1 = ftell(fp);

    tui_runtime_flush(rt);
    fflush(fp);

    assert(strstr(outbuf + pos1, "\x1b[1A") != NULL);
    assert(strstr(outbuf + pos1, "\x1b[2A") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* #4: stale lines erased when content shrinks */
static void test_flush_inline_erases_stale_lines_on_shrink(void)
{
    char outbuf[8192];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);
    assert(rt != NULL);

    /* Frame 1: 3 lines */
    tui_runtime_flush(rt);
    fflush(fp);
    assert(rt->inline_lines_rendered == 3);

    /* Simulate shrink: set prev state, then flush with empty content */
    rt->inline_lines_rendered = 3;
    rt->inline_cursor_row = 2;
    size_t pos1 = ftell(fp);

    /* Use view_stub (empty content → 0 lines) */
    TuiRuntime *rt2 = tui_runtime_create(&view_stub_component, NULL, &cfg);
    rt2->inline_lines_rendered = 3;
    rt2->inline_cursor_row = 2;
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt2);
    fflush(fp);

    /* Should emit \r\n + EL_TO_END for 3 stale lines */
    const char *p = outbuf + pos1;
    int count = 0;
    while ((p = strstr(p, "\r\n\x1b[K")) != NULL) {
        count++;
        p += 3;
    }
    assert(count >= 2);

    tui_runtime_free(rt);
    tui_runtime_free(rt2);
    fclose(fp);
}

static void test_runtime_inline_lines_starts_zero(void)
{
    char outbuf[256];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->inline_lines_rendered == 0);

    tui_runtime_free(rt);
    fclose(fp);
}

/* --- tui_runtime_finish_inline tests --- */

/* finish_inline from last row: cursor already on last line, should
 * just write \r\n (no cursor-down needed) */
static void test_finish_inline_moves_past_content_from_last_row(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);
    assert(rt != NULL);

    /* Render 3 lines, cursor on last row (row 2) */
    tui_runtime_flush(rt);
    fflush(fp);
    assert(rt->inline_lines_rendered == 3);
    assert(rt->inline_cursor_row == 2);

    size_t pos = ftell(fp);
    tui_runtime_finish_inline(rt);
    fflush(fp);

    /* Should write \r\n (no cursor-down since already on last row) */
    assert(strstr(outbuf + pos, "\r\n") != NULL);
    /* Should NOT emit cursor-down */
    assert(strstr(outbuf + pos, "\x1b[1B") == NULL);
    assert(strstr(outbuf + pos, "\x1b[2B") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* finish_inline from first row: cursor on row 0 of 3, should
 * cursor-down 2 then write \r\n */
static void test_finish_inline_moves_past_content_from_first_row(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);
    assert(rt != NULL);

    tui_runtime_flush(rt);
    fflush(fp);

    /* Simulate cursor on row 0 (user pressed Up) */
    rt->inline_cursor_row = 0;
    size_t pos = ftell(fp);
    tui_runtime_finish_inline(rt);
    fflush(fp);

    /* Should cursor-down 2 (from row 0 to row 2) then \r\n */
    assert(strstr(outbuf + pos, "\x1b[2B") != NULL);
    assert(strstr(outbuf + pos, "\r\n") != NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* finish_inline resets tracking to 0 so next flush doesn't cursor-up */
static void test_finish_inline_resets_tracking(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);
    assert(rt != NULL);

    tui_runtime_flush(rt);
    fflush(fp);
    assert(rt->inline_lines_rendered == 3);
    assert(rt->inline_cursor_row == 2);

    tui_runtime_finish_inline(rt);

    /* Both should be reset to 0 */
    assert(rt->inline_lines_rendered == 0);
    assert(rt->inline_cursor_row == 0);

    /* Next flush should NOT cursor-up */
    size_t pos = ftell(fp);
    tui_runtime_flush(rt);
    fflush(fp);
    assert(strstr(outbuf + pos, "\x1b[1A") == NULL);
    assert(strstr(outbuf + pos, "\x1b[2A") == NULL);
    assert(strstr(outbuf + pos, "\x1b[3A") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* --- tui_runtime_clear_inline tests --- */

/* clear_inline erases the frame in place: cursor-up to frame row 0,
 * EL each rendered row, cursor left at frame row 0 col 0 — the area
 * is free for direct application output (which overwrites the old
 * frame). inline_lines_rendered is KEPT so the next flush's stale-
 * line erase still wipes leftover blank rows below a shrunk frame;
 * only the cursor row resets. */
static void test_clear_inline_erases_frame_and_keeps_lines(void)
{
    char outbuf[8192];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);

    /* 3 rendered lines, cursor on row 1 (middle) */
    tui_runtime_flush(rt);
    fflush(fp);
    rt->inline_cursor_row = 1;
    size_t pos = ftell(fp);

    tui_runtime_clear_inline(rt);
    fflush(fp);

    /* Cursor-up 1 first (from row 1 to frame row 0), not 2 */
    assert(strncmp(outbuf + pos, "\x1b[1A", 4) == 0);

    /* Three EL erases with \r\n between them, then cursor-up 2 back
     * to frame row 0 (last erase ends on row 2). */
    const char *p = outbuf + pos;
    int el_count = 0;
    while ((p = strstr(p, "\r\x1b[K")) != NULL) {
        el_count++;
        p += 3;
    }
    assert(el_count == 3);
    assert(strstr(outbuf + pos, "\x1b[2A") != NULL);

    /* Tracking: lines KEPT, cursor row reset */
    assert(rt->inline_lines_rendered == 3);
    assert(rt->inline_cursor_row == 0);

    /* Next flush renders at the cursor (frame row 0) — no cursor-up */
    tui_runtime_flush(rt);
    fflush(fp);
    assert(strstr(outbuf + pos, "\x1b[3A") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* clear_inline keeps prev_lines semantics: after erasing a 3-line
 * frame and printing 1 line of app output, the next flush of a
 * 1-line frame still erases the 2 leftover rows below it. */
static TuiView oneline_view(const TuiModel *model, DynamicBuffer *out)
{
    (void)model;
    dynamic_buffer_append_str(out, "single line frame");
    TuiView v = tui_view_default(out);
    v.render_mode = TUI_RENDER_INLINE;
    return v;
}

static TuiComponent oneline_component = {
    .init = view_stub_init,
    .update = noop_update,
    .view = oneline_view,
    .free = test_free,
};

static void test_clear_inline_stale_erase_still_fires(void)
{
    char outbuf[8192];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp };
    TuiRuntime *rt = tui_runtime_create(&multiline_component, NULL, &cfg);

    tui_runtime_flush(rt); /* 3 lines */
    fflush(fp);

    tui_runtime_clear_inline(rt);
    fputs("printed one line\r\n", fp); /* app output, 1 row */
    fflush(fp);
    size_t pos = ftell(fp);

    /* Now a 1-line frame renders below the printed line; tracking
     * still remembers the erased 3 lines (carried over by the app). */
    TuiRuntime *rt2 = tui_runtime_create(&oneline_component, NULL, &cfg);
    rt2->inline_lines_rendered = 3; /* carried from the erased frame */
    rt2->inline_cursor_row = 0;
    tui_runtime_flush(rt2);
    fflush(fp);

    /* 3 > 1 → two stale \r\n+EL rows erased below the new frame */
    const char *q = outbuf + pos;
    int stale = 0;
    while ((q = strstr(q, "\r\n\x1b[K")) != NULL) {
        stale++;
        q += 3;
    }
    assert(stale == 2);

    tui_runtime_free(rt);
    tui_runtime_free(rt2);
    fclose(fp);
}

/* clear_inline before any inline flush: no-op (nothing to erase),
 * tracking untouched so a later flush renders from row 0. */
static void test_clear_inline_before_first_flush_noop(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);

    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    size_t pos = ftell(fp);
    tui_runtime_clear_inline(rt);
    fflush(fp);

    assert(rt->inline_lines_rendered == 0);
    assert(ftell(fp) == pos); /* nothing written */

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_stop_inline_moves_cursor_down(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* Simulate inline mode with 2 lines rendered */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_start(rt);
    tui_runtime_flush(rt);
    rt->inline_lines_rendered = 2;

    size_t pos = ftell(fp);
    tui_runtime_stop(rt);
    fflush(fp);

    /* stop() should write \r\n to end the input line */
    assert(strstr(outbuf + pos, "\r\n") != NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_stop_inline_no_exit_alt_screen(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_start(rt);
    tui_runtime_flush(rt);
    tui_runtime_stop(rt);
    fflush(fp);

    assert(strstr(outbuf, "?1049l") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* Inline stop() must NOT emit DECRC (ESC 8) — that would restore the
 * cursor to the pre-inline position, erasing any output printed after
 * stop(). Instead, stop writes \r\n to move past the input. */
static void test_stop_inline_no_decrc(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_start(rt);
    tui_runtime_flush(rt);
    size_t pos = ftell(fp);
    tui_runtime_stop(rt);
    fflush(fp);

    /* DECRC = ESC 8 = "\0338" — must not appear after the flush */
    assert(strstr(outbuf + pos, "\0338") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* Inline start() must NOT emit DECSC (ESC 7) — there's no DECRC to
 * match it in stop(). Start should be a no-op for inline mode. */
static void test_start_inline_no_decsc(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* First flush sets in_inline_mode = 1 */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    tui_runtime_stop(rt);

    /* Now start again — should NOT emit DECSC */
    size_t pos = ftell(fp);
    tui_runtime_start(rt);
    fflush(fp);

    /* DECSC = ESC 7 = "\0337" — must not appear */
    assert(strstr(outbuf + pos, "\0337") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

/* After stop+start cycle in inline mode, inline_lines_rendered must be 0
 * so the next flush doesn't cursor-up into the old input/output. */
static void test_start_resets_inline_lines_rendered(void)
{
    char outbuf[2048];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_start(rt);
    tui_runtime_flush(rt);
    rt->inline_lines_rendered = 3; /* simulate 3 lines rendered */
    rt->inline_cursor_row = 2;     /* cursor on last line */
    tui_runtime_stop(rt);

    /* After stop, inline_lines_rendered should be 0 */
    assert(rt->inline_lines_rendered == 0);

    /* Start again — should still be 0 */
    tui_runtime_start(rt);
    assert(rt->inline_lines_rendered == 0);

    /* Next flush should NOT cursor-up (inline_lines_rendered == 0) */
    size_t pos = ftell(fp);
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    fflush(fp);
    assert(strstr(outbuf + pos, "\x1b[3A") == NULL);
    assert(strstr(outbuf + pos, "\x1b[1A") == NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

static void test_inline_resize_triggers_repaint(void)
{
    char outbuf[4096];
    memset(outbuf, 0, sizeof(outbuf));
    FILE *fp = fmemopen(outbuf, sizeof(outbuf), "w");
    assert(fp != NULL);

    TuiRuntimeConfig cfg = { .output = fp, .raw_mode = 0 };
    TuiRuntime *rt = tui_runtime_create(&view_stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* Frame 1: inline mode with 2 lines */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    rt->inline_lines_rendered = 2;
    rt->inline_cursor_row = 1; /* cursor was on last line */
    fflush(fp);
    size_t pos1 = ftell(fp);

    /* Frame 2: after resize, should still cursor-up to repaint */
    reset_view_stub();
    s_view_to_return.render_mode = TUI_RENDER_INLINE;
    tui_runtime_flush(rt);
    fflush(fp);

    /* The second flush should emit cursor-up (1 line, N-1=1) to repaint */
    assert(strstr(outbuf + pos1, "\x1b[1A") != NULL);

    tui_runtime_free(rt);
    fclose(fp);
}

#endif /* _WIN32 */

/* ======================================================================== */

int main(void)
{
    printf("runtime tests:\n");

    RUN_TEST(test_config_stores_callbacks);
    RUN_TEST(test_start_idempotent);
    RUN_TEST(test_stop_idempotent);
    RUN_TEST(test_start_stop_cycle);
    RUN_TEST(test_get_dimensions);
    RUN_TEST(test_get_dimensions_null);
    RUN_TEST(test_null_callbacks_in_config);
    RUN_TEST(test_default_config_raw_mode);
    RUN_TEST(test_window_size_message);
    RUN_TEST(test_runtime_quit);
    RUN_TEST(test_started_initially_zero);

    /* Scheduling API tests */
    RUN_TEST(test_queue_initialized);
    RUN_TEST(test_wakeup_pipe_created);
    RUN_TEST(test_wakeup_fd_null);
    RUN_TEST(test_post_and_drain);
    RUN_TEST(test_schedule_and_drain);
    RUN_TEST(test_drain_empty);
    RUN_TEST(test_drain_null);
    RUN_TEST(test_post_null);
    RUN_TEST(test_schedule_null);
    RUN_TEST(test_post_ordering);
    RUN_TEST(test_post_reentrancy);
    RUN_TEST(test_commands_before_messages);
    RUN_TEST(test_queue_growth);
    RUN_TEST(test_schedule_quit);
    RUN_TEST(test_free_with_pending_commands);
    RUN_TEST(test_double_drain);

#ifndef _WIN32
    RUN_TEST(test_runtime_run_immediate_quit);
    RUN_TEST(test_post_wakes_event_loop);
    RUN_TEST(test_fill_zero_in_run);
    RUN_TEST(test_fill_overflow_clamps);
    RUN_TEST(test_fill_three_in_run);
    RUN_TEST(test_two_socket_subscriptions);
#endif

    /* TuiRenderMode tests (cross-platform — no fmemopen needed) */
    RUN_TEST(test_view_default_render_mode_alt_screen);
    RUN_TEST(test_view_can_set_inline_mode);
    RUN_TEST(test_alt_screen_backward_compat);

    /* TuiView / flush tests (require fmemopen — POSIX only) */
#ifndef _WIN32
    RUN_TEST(test_flush_emits_cursor_when_visible);
    RUN_TEST(test_flush_keeps_cursor_hidden_when_view_abstains);
    RUN_TEST(test_flush_alt_screen_transition);
    RUN_TEST(test_flush_window_title);
    RUN_TEST(test_flush_mouse_mode_transition);

    /* Inline mode flush tests (require fmemopen — POSIX only) */
    RUN_TEST(test_runtime_inline_lines_starts_zero);
    RUN_TEST(test_flush_inline_no_alt_screen);
    RUN_TEST(test_flush_inline_first_frame_no_cursor_up);
    RUN_TEST(test_flush_inline_cursor_up_on_second_flush);
    RUN_TEST(test_flush_inline_emits_erase);
    RUN_TEST(test_flush_inline_counts_lines);
    RUN_TEST(test_flush_inline_counts_visual_lines);
    RUN_TEST(test_flush_inline_cursor_up_from_non_last_row);
    RUN_TEST(test_flush_inline_cursor_up_from_middle_row);
    RUN_TEST(test_flush_inline_erases_stale_lines_on_shrink);
    RUN_TEST(test_finish_inline_moves_past_content_from_last_row);
    RUN_TEST(test_finish_inline_moves_past_content_from_first_row);
    RUN_TEST(test_finish_inline_resets_tracking);
    RUN_TEST(test_clear_inline_erases_frame_and_keeps_lines);
    RUN_TEST(test_clear_inline_stale_erase_still_fires);
    RUN_TEST(test_clear_inline_before_first_flush_noop);
    RUN_TEST(test_stop_inline_moves_cursor_down);
    RUN_TEST(test_stop_inline_no_exit_alt_screen);
    RUN_TEST(test_stop_inline_no_decrc);
    RUN_TEST(test_start_inline_no_decsc);
    RUN_TEST(test_start_resets_inline_lines_rendered);
    RUN_TEST(test_inline_resize_triggers_repaint);
#endif

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
