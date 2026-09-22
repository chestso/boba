# tests/Makefile.am - Build and run boba unit tests

UNIT_TESTS = test_ansi test_textinput test_viewport test_runtime test_focus test_clipboard test_input_parser test_style test_list_popup test_stream test_terminal_profile test_unicode

check_PROGRAMS = $(UNIT_TESTS)

AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/src/libboba.a $(SOCKET_LIBS)

# The unit tests assert() their expectations, so they must be built with
# assertions enabled whatever the build mode. A release-shaped tree (the
# monorepo's RELEASE=1 sets CFLAGS='-O3 -DNDEBUG', as does
# --enable-release) would otherwise compile every assertion out and a
# green `make check` would verify nothing. GNU make's `override` appends
# after CFLAGS, so -UNDEBUG wins over the -DNDEBUG that came in.
override CFLAGS += -UNDEBUG

# Per-test runtime cap (house rule: a unit test is ms-fast, so a hang is
# a bug, not a condition to wait out). The watchdog wraps every test:
# BOBA_TEST_TIMEOUT seconds (default 10), then a stack snapshot into the
# .log and a core-dumping kill, so a hung test FAILS loudly, naming
# itself, instead of stalling the workflow until the job's own timeout
# cancels it with nothing to read (the 2026-09-22 MSYS2 run: the Windows
# job sat in `make check` past its 20-minute cap and the live log was not
# fetchable, so which test hung was unknowable). The START/END pairs in
# test-progress.log - uploaded as a CI artifact - answer that even when
# no debugger is installed. BOBA_TEST_TIMEOUT=0 turns the cap off for
# interactive debugging.
#
# LOG_COMPILER is what the parallel harness uses for the suffix-less unit
# binaries. The tmux e2e scripts are `.sh` tests and go through
# SH_LOG_COMPILER instead — see the TEST_EXTENSIONS block below, where
# that variable already lives (automake defaults it to plain $(SHELL)).
LOG_COMPILER = $(SHELL) $(top_srcdir)/scripts/test-watchdog.sh

test_ansi_SOURCES = test_ansi.c
test_textinput_SOURCES = test_textinput.c
test_viewport_SOURCES = test_viewport.c
test_runtime_SOURCES = test_runtime.c
test_focus_SOURCES = test_focus.c
test_clipboard_SOURCES = test_clipboard.c
test_input_parser_SOURCES = test_input_parser.c
test_style_SOURCES = test_style.c
test_list_popup_SOURCES = test_list_popup.c
test_stream_SOURCES = test_stream.c
test_terminal_profile_SOURCES = test_terminal_profile.c
test_unicode_SOURCES = test_unicode.c

# tmux-driven end-to-end tests. Folded into `make check` when tmux was
# detected at configure time, so a regression in shell-driven UI tests
# can never silently slip through. The mini-apps the scripts drive are
# in check_PROGRAMS, so automake builds them before TESTS runs.
TMUX_TESTS = tmux/scroll_multiline.sh tmux/focus_shift_tab.sh tmux/wrap_multiline.sh tmux/wrap_inline.sh

if HAVE_TMUX
check_PROGRAMS += apps/tmux_textinput_multi apps/tmux_focus_swap apps/tmux_textinput_wrap apps/tmux_textinput_inline
apps_tmux_textinput_multi_SOURCES = apps/tmux_textinput_multi.c
apps_tmux_focus_swap_SOURCES = apps/tmux_focus_swap.c
apps_tmux_textinput_wrap_SOURCES = apps/tmux_textinput_wrap.c
apps_tmux_textinput_inline_SOURCES = apps/tmux_textinput_inline.c
TESTS = $(UNIT_TESTS) $(TMUX_TESTS)
else
TESTS = $(UNIT_TESTS)
endif

TEST_EXTENSIONS = .sh
# The tmux e2e scripts run under the same watchdog, with a longer cap:
# each one starts a tmux server and a TUI app. $(SHELL) is automake's
# default for `.sh` tests and the wrapper keeps that shape.
SH_LOG_COMPILER = env BOBA_TEST_TIMEOUT=30 $(SHELL) \
	$(top_srcdir)/scripts/test-watchdog.sh

AM_TESTS_ENVIRONMENT = \
    TMUX_BIN_DIR="$(abs_builddir)/apps"; \
    export TMUX_BIN_DIR;

EXTRA_DIST = tmux/lib.sh $(TMUX_TESTS)

# The watchdog appends START/END pairs to test-progress.log by default
# (a hang is diagnosable from CI artifacts). It is a generated file; list
# it so `make clean`/`make distclean` removes it and distcheck's
# distcleancheck passes.
CLEANFILES = test-progress.log
