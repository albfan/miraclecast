#
# MiracleCast - build configuration script
# Copyright (c) 2013-2014 David Herrmann <dh.herrmann@gmail.com>
#

AC_PREREQ(2.68)

AC_INIT([miracle],
        [1],
        [http://www.freedesktop.org/wiki/Software/miracle],
        [miracle],
        [http://www.freedesktop.org/wiki/Software/miracle])
AC_CONFIG_SRCDIR([src/miracled.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER(config.h)
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([foreign 1.11 subdir-objects dist-xz no-dist-gzip tar-pax -Wall -Werror -Wno-portability])
AM_SILENT_RULES([yes])

AC_SUBST(PACKAGE_DESCRIPTION, ["Wifi-Display/Miracast Implementation"])

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
AC_PROG_SED
AC_PROG_MKDIR_P
AC_PROG_LN_S
AC_PROG_GREP
AC_PROG_AWK

LT_PREREQ(2.2)
LT_INIT
LT_LIB_M

AC_ARG_ENABLE([rely-udev],
              AS_HELP_STRING([--enable-rely-udev], [Use tagged device with miraclecast]), AC_DEFINE([RELY_UDEV], [], [Rely on udev to find miraclecast device]))
AC_ARG_ENABLE([log-debug],
                  AS_HELP_STRING([--disable-log-debug], [Disable log debug]), , AC_DEFINE([BUILD_ENABLE_DEBUG], [], [Enable debug log level]))
#
# Mandatory dependencies
#

PKG_CHECK_MODULES([DEPS], [libudev libsystemd > 219])
PKG_CHECK_MODULES([GLIB], [glib-2.0])

AC_CHECK_HEADERS(readline/readline.h,, AC_MSG_ERROR(GNU readline not found))

#
# Test for "check" which we use for our test-suite. If not found, we disable
# all tests.
#

PKG_CHECK_MODULES([CHECK], [check],
                  [have_check=yes], [have_check=no])
AM_CONDITIONAL([BUILD_HAVE_CHECK], [test "x$have_check" = "xyes"])

#
# Makefile vars
# After everything is configured, we create all makefiles.
#

AC_CONFIG_FILES([Makefile
                 res/Makefile
                 src/Makefile
                 src/ctl/Makefile
                 src/dhcp/Makefile
                 src/shared/Makefile
                 src/wifi/Makefile
                 src/uibc/Makefile
                 test/Makefile])
AC_OUTPUT

#
# Configuration output
# Show configuration to the user so they can check whether everything was
# configured as expected.
#

if test "x$have_check" = "xyes"
then
AC_MSG_NOTICE([Build configuration:
       prefix: $prefix
       exec-prefix: $exec_prefix
       bindir: $bindir
       libdir: $libdir
       includedir: $includedir

Miscellaneous Options:
       building tests: $have_check

Compilation
       mkdir build && cd build
       "${MAKE-make}" to start compilation process
       "${MAKE-make}" check to pass test])
else
AC_MSG_NOTICE([Build configuration:
       prefix: $prefix
       exec-prefix: $exec_prefix
       bindir: $bindir
       libdir: $libdir
       includedir: $includedir

Miscellaneous Options:
       building tests: $have_check

Compilation
       mkdir build && cd build
       "${MAKE-make}" to start compilation process])
fi
