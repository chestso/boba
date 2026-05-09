# src/Makefile.am - Build bloom-boba static library

lib_LIBRARIES = libbloom-boba.a

libbloom_boba_a_SOURCES = \
    msg.c \
    cmd.c \
    input_parser.c \
    runtime.c \
    dynamic_buffer.c \
    ansi_sequences.c \
    unicode.c \
    base64.c \
    style.c \
    components/textinput.c \
    components/textview.c \
    components/viewport.c \
    components/statusbar.c

libbloom_boba_a_CPPFLAGS = \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/src
