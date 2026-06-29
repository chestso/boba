dnl configure.ac - Autoconf configuration for boba
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([boba],
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
BOBA_VERSION=`"$srcdir/build-aux/git-version.sh" "$srcdir" 2>/dev/null`
AS_IF([test -z "$BOBA_VERSION"],
      [BOBA_VERSION="$PACKAGE_VERSION"])
AC_SUBST([BOBA_VERSION])
AC_MSG_NOTICE([boba version: $BOBA_VERSION])

dnl Check for programs
user_cflags="$CFLAGS"
AC_PROG_CC
AC_PROG_RANLIB
AM_PROG_AR

dnl Optional: tmux for end-to-end terminal tests (`make check-tmux`)
AC_CHECK_PROG([TMUX], [tmux], [yes], [no])
AM_CONDITIONAL([HAVE_TMUX], [test "x$TMUX" = "xyes"])

dnl Build mode selection (--enable-release > --enable-debug > default ASan)
AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug],
    [unsanitized debug build (-O0 -g3, no ASan)])],
  [enable_debug=$enableval],
  [enable_debug=no])

AC_ARG_ENABLE([release],
  [AS_HELP_STRING([--enable-release],
    [optimized release build (-O3 -DNDEBUG)])],
  [enable_release=$enableval],
  [enable_release=no])

if test "x$enable_release" = "xyes"; then
  CFLAGS="-O3 -DNDEBUG"
  AC_MSG_NOTICE([Release mode: CFLAGS set to -O3 -DNDEBUG])
elif test "x$enable_debug" = "xyes"; then
  CFLAGS="-O0 -g3"
  AC_MSG_NOTICE([Debug mode: CFLAGS set to -O0 -g3])
elif test "x$user_cflags" = "x"; then
  CFLAGS="-O1 -g3 -fno-omit-frame-pointer -fno-common -fsanitize=address,undefined -fno-sanitize-recover=undefined"
  LDFLAGS="-fsanitize=address,undefined $LDFLAGS"
  AC_MSG_NOTICE([ASan+UBSan mode (default): CFLAGS=$CFLAGS])
fi

dnl Check for header files
AC_CHECK_HEADERS([stdlib.h string.h])

dnl Check for typedefs and structures
AC_TYPE_SIZE_T

AC_DEFINE_UNQUOTED([BOBA_VERSION], ["$BOBA_VERSION"], [Version string])

dnl Output files
AC_CONFIG_FILES([
    Makefile
    src/Makefile
    include/Makefile
    tests/Makefile
    boba.pc
])

AC_OUTPUT
