# tests/Makefile.am - Build and run bloom-boba unit tests

check_PROGRAMS = test_textinput test_viewport test_statusbar test_runtime test_focus test_clipboard

TESTS = $(check_PROGRAMS)

AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/src/libbloom-boba.a

test_textinput_SOURCES = test_textinput.c
test_viewport_SOURCES = test_viewport.c
test_statusbar_SOURCES = test_statusbar.c
test_runtime_SOURCES = test_runtime.c
test_focus_SOURCES = test_focus.c
test_clipboard_SOURCES = test_clipboard.c
