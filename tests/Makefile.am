# tests/Makefile.am - Build and run bloom-boba unit tests

UNIT_TESTS = test_textinput test_viewport test_statusbar test_runtime test_focus test_clipboard test_input_parser test_style

check_PROGRAMS = $(UNIT_TESTS)

AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/src/libbloom-boba.a

test_textinput_SOURCES = test_textinput.c
test_viewport_SOURCES = test_viewport.c
test_statusbar_SOURCES = test_statusbar.c
test_runtime_SOURCES = test_runtime.c
test_focus_SOURCES = test_focus.c
test_clipboard_SOURCES = test_clipboard.c
test_input_parser_SOURCES = test_input_parser.c
test_style_SOURCES = test_style.c

# tmux-driven end-to-end tests. Folded into `make check` when tmux was
# detected at configure time, so a regression in shell-driven UI tests
# can never silently slip through. The mini-apps the scripts drive are
# in check_PROGRAMS, so automake builds them before TESTS runs.
TMUX_TESTS = tmux/scroll_multiline.sh tmux/focus_shift_tab.sh

if HAVE_TMUX
check_PROGRAMS += apps/tmux_textinput_multi apps/tmux_focus_swap
apps_tmux_textinput_multi_SOURCES = apps/tmux_textinput_multi.c
apps_tmux_focus_swap_SOURCES = apps/tmux_focus_swap.c
TESTS = $(UNIT_TESTS) $(TMUX_TESTS)
else
TESTS = $(UNIT_TESTS)
endif

TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    TMUX_BIN_DIR="$(abs_builddir)/apps"; \
    export TMUX_BIN_DIR;

EXTRA_DIST = tmux/lib.sh $(TMUX_TESTS)
