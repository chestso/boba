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
#include <unistd.h>
#else
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
static int dummy_get_fd(void *data)
{
    (void)data;
    return -1;
}
static void dummy_on_ready(void *data) { (void)data; }
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
        .get_external_fd = dummy_get_fd,
        .on_external_ready = dummy_on_ready,
        .on_tick = dummy_on_tick,
        .on_resize = dummy_on_resize,
        .on_stdin_processed = dummy_on_stdin,
        .event_data = &called,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Verify callbacks are stored in config */
    assert(rt->config.get_external_fd == dummy_get_fd);
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
        .get_external_fd = NULL,
        .on_external_ready = NULL,
        .on_tick = NULL,
        .on_resize = NULL,
        .on_stdin_processed = NULL,
        .event_data = NULL,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->config.get_external_fd == NULL);
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
#endif

    /* TuiView / flush tests (require fmemopen — POSIX only) */
#ifndef _WIN32
    RUN_TEST(test_flush_emits_cursor_when_visible);
    RUN_TEST(test_flush_keeps_cursor_hidden_when_view_abstains);
    RUN_TEST(test_flush_alt_screen_transition);
    RUN_TEST(test_flush_window_title);
    RUN_TEST(test_flush_mouse_mode_transition);
#endif

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
