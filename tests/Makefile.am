# tests/Makefile.am - Build and run bloom-boba unit tests

UNIT_TESTS = test_textinput test_viewport test_statusbar test_runtime test_focus test_clipboard

check_PROGRAMS = $(UNIT_TESTS)

TESTS = $(UNIT_TESTS)

AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/src/libbloom-boba.a

test_textinput_SOURCES = test_textinput.c
test_viewport_SOURCES = test_viewport.c
test_statusbar_SOURCES = test_statusbar.c
test_runtime_SOURCES = test_runtime.c
test_focus_SOURCES = test_focus.c
test_clipboard_SOURCES = test_clipboard.c

# tmux-driven end-to-end tests: separate target so the fast unit suite stays
# hermetic. `make check-tmux` builds the mini-apps and runs the shell drivers.
if HAVE_TMUX
check_PROGRAMS += apps/tmux_textinput_multi apps/tmux_focus_swap
apps_tmux_textinput_multi_SOURCES = apps/tmux_textinput_multi.c
apps_tmux_focus_swap_SOURCES = apps/tmux_focus_swap.c
endif

TMUX_TESTS = tmux/scroll_multiline.sh tmux/focus_shift_tab.sh

EXTRA_DIST = tmux/lib.sh $(TMUX_TESTS)

check-tmux: $(check_PROGRAMS)
if HAVE_TMUX
	@set -e; \
	failed=0; \
	for t in $(TMUX_TESTS); do \
	  echo "  TMUX  $$t"; \
	  TMUX_BIN_DIR=$(builddir)/apps $(SHELL) $(srcdir)/$$t || failed=1; \
	done; \
	test $$failed -eq 0
else
	@echo "  SKIP  tmux not found at configure time; skipping check-tmux"
endif

.PHONY: check-tmux
