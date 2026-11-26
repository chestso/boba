# Contributing

How to build, test and change boba. The library's design lives in
`ARCHITECTURE.md`.

## Design policy

**boba is pre-alpha, and it does not owe anyone backward compatibility.** A
simpler design is preferred and SHOULD be chosen when it presents itself — even
when that means changing or deleting an API that already shipped. A cleaner
signature, a smaller struct, one obvious way to do a thing beats a shim that
exists only to keep an old spelling compiling: don't add an `_ex` twin (or a
flags field, or a union member) purely to preserve an existing signature, and
don't route around a bad shape because "someone may call it". There is one
consumer (nevermore) plus the in-tree tests, both of which get updated with the
change. If a better shape costs a compile error somewhere downstream, that is
the intended signal, not a reason to keep the worse shape. `BOBA_DEPRECATED`
(see Code Conventions) is for an API whose replacement is worth a warning, not
an obligation to keep every old entry point alive.

## Build Commands

The project uses GNU Autotools. From a clean checkout:

```sh
./autogen.sh        # bootstrap: regenerate version, then autoreconf -fi
./configure         # configure (see flags below)
make                # build libboba.a
make check          # run unit tests (+ tmux tests if tmux detected)
make format         # clang-format + shfmt + dprint
make bear           # regenerate compile_commands.json (requires bear; runs clean build)
```

Out-of-tree builds are supported: `mkdir build && cd build && ../configure && make`.

### Configure flags

| Flag               | Effect                                             |
| ------------------ | -------------------------------------------------- |
| _(none)_           | ASan + UBSan on Unix; plain debug on Windows/MinGW |
| `--enable-debug`   | Unsanitized debug: `-O0 -g3`, no ASan              |
| `--enable-release` | Optimized: `-O3 -DNDEBUG`                          |

The default (no flag) build has ASan+UBSan enabled. Use `--enable-debug` for a
clean debug build without sanitizers.

### Windows build

```sh
./scripts/build-ucrt64.sh            # autogen + configure + make + check
./scripts/build-ucrt64.sh --install  # build, then make install
./scripts/build-ucrt64.sh --enable-release  # extra args forwarded to configure
```

The script re-execs into an MSYS2 UCRT64 shell and applies workarounds for
MSYS2's `sh` and `ACLOCAL_PATH`.

### Linking against boba after install

```sh
gcc -o myapp myapp.c $(pkg-config --cflags --libs boba)
```

A `boba.pc` pkg-config file is generated and installed alongside the library.

## Testing

- **Unit tests**: 8 plain-C test binaries in `tests/` (`test_ansi`,
  `test_textinput`, `test_viewport`, `test_runtime`,
  `test_focus`, `test_clipboard`, `test_input_parser`, `test_style`). No test
  framework — uses `assert()` and a custom `RUN_TEST(fn)` macro. Each test
  binary has a `main()` that prints pass/fail counts and returns nonzero on
  failure.
- **tmux end-to-end tests**: Shell scripts in `tests/tmux/` drive mini-app
  binaries in `tests/apps/` through a real tmux session. Automatically
  included in `make check` when tmux is detected at configure time. The
  `TMUX_BIN_DIR` env var (set by the test Makefile) points scripts at the
  built mini-apps. Helpers in `tests/tmux/lib.sh` (`tmux_start`, `tmux_send`,
  `tmux_capture`, `tmux_cursor`, `tmux_kill`, `tmux_wait_for`, `dump_pane`,
  `assert_pane_contains`, `assert_pane_lacks`, `assert_cursor_x_lt`).
- Tests link against `src/libboba.a` (static library).
- Tests that exercise deprecated APIs suppress warnings with
  `_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")`.

### Adding a new tmux test

1. Write `tests/apps/foo.c` — a mini-app that wraps a component, handles
   `TUI_MSG_WINDOW_SIZE`, and calls `tui_runtime_run()`.
2. Register the binary under `if HAVE_TMUX` in `tests/Makefile.am`:
   ```makefile
   check_PROGRAMS += apps/foo
   apps_foo_SOURCES = apps/foo.c
   ```
3. Write `tests/tmux/foo.sh`. Source `lib.sh`, launch with `tmux_start`, drive
   with `tmux_send`, assert with `assert_pane_contains` / `assert_cursor_x_lt`.
4. Append the script to `TMUX_TESTS` in `tests/Makefile.am`.

Reusable patterns from existing tests: **sentinel sync** (append a unique char
to a `send-keys` batch, then `tmux_wait_for` it to confirm all keystrokes were
processed before assertions) and **cleanup trap**
(`trap 'tmux_kill "$SESSION"' EXIT` — always fires, even on assertion failure;
`tmux_start` enables `remain-on-exit` so crashed mini-apps leave a debuggable
pane).

## Code Conventions

- `.clang-format` is present; run `make format` before committing.
- **Allman braces**: opening brace on a new line for functions, structs,
  enums, unions, and case labels. Control statements (`if`, `for`, `while`)
  keep the brace on the same line.
- 4-space indentation, no tabs.
- Right-aligned pointers (`char *foo`, not `char* foo`).
- `ColumnLimit: 0` (no line length limit).
- `-Wall -Werror` is enforced via `AM_INIT_AUTOMAKE`.
- Public headers go in `include/boba/`; implementations in `src/`.
- Internal headers (e.g., `src/base64.h`) are not installed and not part of
  the public API.
- `.gitattributes` enforces LF line endings.
- **A tracked file, or a commit message, never points at a file this
  repository does not contain.** Local agent notes and design notes are
  excluded from the repo; the `.gitignore` entries are the exclusion
  mechanism, not a reference to content, and the one place their names
  appear. Another project's paths are not ours either: a comment or a
  commit body naming an absent file sends every other reader to a dead
  end, and the reasoning it was carrying is lost with it. Say the thing
  in place, or point at the tracked header or source that says it.
  Untracked notes may reference other untracked notes — they live and
  die together; the ban is on the tracked side.

### Header include paths

Public headers include each other with relative quoted includes
(`#include "msg.h"`, `#include "../component.h"`), so they resolve without
relying on `-I` flags. Implementation files in `src/` use the installed prefix
(`<boba/stream.h>`) — the same spelling an app consumer uses — and private
internal headers (`src/stream_internal.h`, `src/base64.h`) use plain quoted
includes and are never installed.

### Deprecated APIs

`BOBA_DEPRECATED(msg)` macro (defined in `component.h`) marks legacy imperative
APIs with a compiler warning naming the recommended replacement. When adding
new declarative APIs, mark the old imperative setter deprecated. Current
examples:

- `tui_runtime_quit()` → return `tui_cmd_quit()` from `update()` instead
- `tui_runtime_exec()` → return the command from `update()` instead
- `tui_textinput_set_focus()` → send `tui_msg_focus()` / `tui_msg_blur()`
- `tui_textinput_set_prompt_color()` → use `tui_textinput_set_focused_prompt_style()`

Many `tui_textinput_set_*()` and `tui_viewport_set_*()` setters are still
imperative and don't yet have declarative equivalents. See `TODO.md` for the
deferred design plan on converting these.

### Style builder pattern

`TuiStyle` setters take a value and return a modified copy (value-typed
composition without method chains):

```c
TuiStyle s = tui_style_bold(
    tui_style_foreground(tui_style_new(), tui_color_hex("#ff8800")), 1);
```

Colors: `tui_color_ansi(n)`, `tui_color_rgb(r,g,b)`, `tui_color_hex("#rrggbb")`,
`tui_color_adaptive(light, dark)`. Five prefab `TuiBorder` styles: normal,
rounded, thick, double, hidden. Use `tui_border_render_horizontal()` for
decorative single-line dividers with optional title. `tui_style_get_width()` /
`tui_style_get_height()` measure rendered content without producing bytes.

### Memory ownership

- Components: explicit `*_create()` / `*_free()` pairs.
- `TuiMsg` constructors (`tui_msg_key`, `tui_msg_char`, etc.) return by value.
  Paste messages own heap-allocated `text`; the runtime frees it via
  `tui_msg_free()` after dispatch. Components needing the text beyond
  `update()` must copy it.
- `TuiCmd` is heap-allocated (`tui_cmd_*` returns `TuiCmd *`). Owner must call
  `tui_cmd_free()`. The runtime takes ownership when scheduled.
- `tui_textinput_update()` returns `TuiUpdateResult` containing a `TuiCmd *` —
  the caller must free it (or `NULL`).
- `tui_cmd_batch2()` takes ownership of both inputs (or NULLs) and returns a
  single combined command (or NULL).

## Gotchas

- **Default build has sanitizers**: Running `./configure` without flags
  enables ASan+UBSan. Use `--enable-debug` for a clean debug build.
- **`-Werror` is on**: All warnings are errors. New code must compile cleanly.
- **`tui_textinput_update()` return value**: Returns `TuiUpdateResult` with a
  `TuiCmd *` that the caller must free. Forgetting this leaks memory.
- **Version file**: `version` is regenerated by `build-aux/git-version.sh`
  during `autogen.sh`. The configure step re-reads it so rebuilds after new
  commits pick up the version without re-running `autoreconf`.
- **clangd false errors**: LSP diagnostics may show errors for test files
  due to include path resolution. The actual build works because
  `configure` sets `-I` flags correctly. Run `make bear` to
  regenerate `compile_commands.json` if clangd is confused after a clean
  checkout.
- **Byte-content tests do not prove a render fix.** They assert what bytes a
  frame carries, not how many deliveries it makes — the property ConPTY
  presents per-write. A cursor/paint change needs a
  **deliveries-per-paint** assertion on `rt->out_write_count`
  (`tests/test_runtime.c`, `tests/test_stream.c`). Never conclude a
  render fix from captured bytes alone.
- **Every emitting entry point must be a paint.** If new code writes to the
  terminal directly (a `fputs`/`fwrite` on `runtime->output`), it bypasses
  `out_stage` and can deliver mid-paint — restore the bug. Emit through
  `runtime_emit()` / `runtime_emit_str()`, and open/close a paint around any
  public entry point that emits (`runtime_paint_begin` / `_end`).
