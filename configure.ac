dnl configure.ac - Autoconf configuration for bloom-boba
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([bloom-boba],
        m4_esyscmd_s([cat version 2>/dev/null || echo "0.0.0-unknown"]),
        [thomasc1971@hotmail.com])
AC_CONFIG_SRCDIR([src/msg.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.14 foreign subdir-objects -Wall -Werror])
AM_SILENT_RULES([yes])

dnl Recompute the version at configure time so a rebuild after new commits
dnl picks up the current version without needing to re-run autoreconf. Falls
dnl back to PACKAGE_VERSION (set at autoreconf time via m4_esyscmd_s above)
dnl for tarball builds where build-aux/git-version.sh has nothing to read.
BLOOM_BOBA_VERSION=`"$srcdir/build-aux/git-version.sh" "$srcdir" 2>/dev/null`
AS_IF([test -z "$BLOOM_BOBA_VERSION"],
      [BLOOM_BOBA_VERSION="$PACKAGE_VERSION"])
AC_SUBST([BLOOM_BOBA_VERSION])
AC_MSG_NOTICE([bloom-boba version: $BLOOM_BOBA_VERSION])

dnl Check for programs
AC_PROG_CC
AC_PROG_RANLIB
AM_PROG_AR

dnl Optional: tmux for end-to-end terminal tests (`make check-tmux`)
AC_CHECK_PROG([TMUX], [tmux], [yes], [no])
AM_CONDITIONAL([HAVE_TMUX], [test "x$TMUX" = "xyes"])

dnl Debug option
AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug],
    [enable debug symbols and disable optimizations])],
  [enable_debug=$enableval],
  [enable_debug=no])

if test "x$enable_debug" = "xyes"; then
  CFLAGS="-O0 -g3 -DDEBUG $CFLAGS"
  AC_MSG_NOTICE([Debug mode enabled: CFLAGS set to -O0 -g3 -DDEBUG])
else
  if test "x$CFLAGS" = "x"; then
    CFLAGS="-O2"
  fi
fi

dnl Check for header files
AC_CHECK_HEADERS([stdlib.h string.h])

dnl Check for typedefs and structures
AC_TYPE_SIZE_T

AC_DEFINE_UNQUOTED([BLOOM_BOBA_VERSION], ["$BLOOM_BOBA_VERSION"], [Version string])

dnl Output files
AC_CONFIG_FILES([
    Makefile
    src/Makefile
    include/Makefile
    tests/Makefile
    bloom-boba.pc
])

AC_OUTPUT
