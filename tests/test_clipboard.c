/* test_clipboard.c - Tests for clipboard command + OSC 52 emission
 *
 * Covers:
 * - tui_cmd_clipboard_copy() constructor + free roundtrip
 * - ansi_format_osc52() against known base64 vectors
 * - Runtime dispatch via TuiClipboardHandler override
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/ansi_sequences.h>
#include <bloom-boba/cmd.h>
#include <bloom-boba/component.h>
#include <bloom-boba/runtime.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

/* ---------- cmd ctor / free ---------- */

static void test_cmd_ctor_copies_text(void)
{
    const char *src = "hello, clipboard";
    size_t len = strlen(src);
    TuiCmd *cmd = tui_cmd_clipboard_copy(src, len);
    assert(cmd != NULL);
    assert(cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(cmd->payload.clipboard.len == len);
    /* Must be a copy, not the same pointer. */
    assert(cmd->payload.clipboard.text != src);
    assert(memcmp(cmd->payload.clipboard.text, src, len) == 0);
    tui_cmd_free(cmd);
}

static void test_cmd_ctor_empty_text(void)
{
    TuiCmd *cmd = tui_cmd_clipboard_copy(NULL, 0);
    assert(cmd != NULL);
    assert(cmd->type == TUI_CMD_CLIPBOARD_COPY);
    assert(cmd->payload.clipboard.len == 0);
    assert(cmd->payload.clipboard.text == NULL);
    tui_cmd_free(cmd);
}

/* ---------- OSC 52 formatter ---------- */

static void test_osc52_known_vector(void)
{
    /* Base64 of "hello" is "aGVsbG8=" */
    char buf[ANSI_OSC52_BUFSIZE(5)];
    size_t n = ansi_format_osc52(buf, sizeof(buf), "hello", 5);
    /* prefix(7) + base64(8) + suffix(2) = 17 */
    assert(n == 17);
    /* Check framing */
    static const char prefix[] = "\033]52;c;";
    static const char suffix[] = "\033\\";
    assert(memcmp(buf, prefix, sizeof(prefix) - 1) == 0);
    assert(memcmp(buf + 7, "aGVsbG8=", 8) == 0);
    assert(memcmp(buf + 15, suffix, sizeof(suffix) - 1) == 0);
}

static void test_osc52_buffer_too_small(void)
{
    char tiny[4];
    size_t n = ansi_format_osc52(tiny, sizeof(tiny), "hello", 5);
    assert(n == 0); /* Refuses to write a partial sequence */
}

/* ---------- runtime dispatch via clipboard handler ---------- */

static int handler_calls;
static char handler_text[64];
static size_t handler_len;

static void capture_handler(const char *text, size_t len, void *user_data)
{
    (void)user_data;
    handler_calls++;
    if (len < sizeof(handler_text)) {
        memcpy(handler_text, text, len);
        handler_text[len] = '\0';
        handler_len = len;
    }
}

/* Minimal model + component for spinning up the runtime. */
typedef struct
{
    TuiModel base;
} StubModel;

static TuiInitResult stub_init(void *config)
{
    (void)config;
    StubModel *m = (StubModel *)calloc(1, sizeof(StubModel));
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult stub_update(TuiModel *model, TuiMsg msg)
{
    (void)model;
    (void)msg;
    return tui_update_result_none();
}

static TuiView stub_view(const TuiModel *model, DynamicBuffer *out)
{
    (void)model;
    return tui_view_default(out);
}

static void stub_free(TuiModel *model) { free(model); }

static TuiComponent stub_component = {
    .init = stub_init,
    .update = stub_update,
    .view = stub_view,
    .free = stub_free,
};

static void test_runtime_dispatches_to_handler(void)
{
    handler_calls = 0;
    handler_len = 0;
    handler_text[0] = '\0';

    /* Build runtime with a clipboard_handler installed. raw_mode/output set
     * to harmless defaults so we never touch the terminal. */
    FILE *devnull = fopen("/dev/null", "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = { 0 };
    cfg.raw_mode = 0;
    cfg.output = devnull;
    cfg.clipboard_handler = capture_handler;

    TuiRuntime *rt = tui_runtime_create(&stub_component, NULL, &cfg);
    assert(rt != NULL);

    /* Schedule the clipboard cmd and drain. */
    TuiCmd *cmd = tui_cmd_clipboard_copy("xyz", 3);
    tui_runtime_exec(rt, cmd);

    assert(handler_calls == 1);
    assert(handler_len == 3);
    assert(strcmp(handler_text, "xyz") == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}

static void test_runtime_default_emits_osc52(void)
{
    /* Without a clipboard_handler, the runtime should emit OSC 52 to the
     * configured output. We capture by writing to a tmpfile. */
    FILE *tmp = tmpfile();
    assert(tmp != NULL);

    TuiRuntimeConfig cfg = { 0 };
    cfg.raw_mode = 0;
    cfg.output = tmp;
    cfg.clipboard_handler = NULL;

    TuiRuntime *rt = tui_runtime_create(&stub_component, NULL, &cfg);
    assert(rt != NULL);

    TuiCmd *cmd = tui_cmd_clipboard_copy("hi", 2);
    tui_runtime_exec(rt, cmd);
    fflush(tmp);

    /* Read back and check for OSC 52 framing. */
    rewind(tmp);
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    assert(strstr(buf, "\033]52;c;") != NULL);
    /* Base64 of "hi" is "aGk=" */
    assert(strstr(buf, "aGk=") != NULL);
    assert(strstr(buf, "\033\\") != NULL);

    tui_runtime_free(rt);
    fclose(tmp);
}

int main(void)
{
    printf("Running clipboard tests...\n");

    RUN_TEST(test_cmd_ctor_copies_text);
    RUN_TEST(test_cmd_ctor_empty_text);
    RUN_TEST(test_osc52_known_vector);
    RUN_TEST(test_osc52_buffer_too_small);
    RUN_TEST(test_runtime_dispatches_to_handler);
    RUN_TEST(test_runtime_default_emits_osc52);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
