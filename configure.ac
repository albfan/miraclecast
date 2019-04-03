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

m4_ifdef([PKG_CHECK_MODULES], [
	PKG_CHECK_MODULES([DEPS], [libudev libsystemd > 219])
	PKG_CHECK_MODULES([GLIB], [glib-2.0])
])

AC_CHECK_HEADERS(readline/readline.h,, AC_MSG_ERROR(GNU readline not found))

#
# Test for "check" which we use for our test-suite. If not found, we disable
# all tests.
#

m4_ifdef([PKG_CHECK_MODULES], [
	PKG_CHECK_MODULES([CHECK], [check],
        	          [have_check=yes], [have_check=no])
])
AM_CONDITIONAL([BUILD_HAVE_CHECK], [test "x$have_check" = "xyes"])

if test "x$have_check" = "xyes"
then

  dnl ************************************
  dnl *** Enable lcov coverage reports ***
  dnl ************************************
  
  AC_ARG_ENABLE(gcov,
    AS_HELP_STRING([--enable-gcov],
  		 [Enable gcov]),
    [use_gcov=$enableval], [use_gcov=no])
  
  if test "x$use_gcov" = "xyes"; then
    dnl we need gcc:
    if test "$GCC" != "yes"; then
      AC_MSG_ERROR([GCC is required for --enable-gcov])
    fi
  
    dnl Check if ccache is being used
    AC_CHECK_PROG(SHTOOL, shtool, shtool)
    case `$SHTOOL path $CC` in
      *ccache*[)] gcc_ccache=yes;;
      *[)] gcc_ccache=no;;
    esac
  
    if test "$gcc_ccache" = "yes" && (test -z "$CCACHE_DISABLE" || test "$CCACHE_DISABLE" != "1"); then
      AC_MSG_ERROR([ccache must be disabled when --enable-gcov option is used. You can disable ccache by setting environment variable CCACHE_DISABLE=1.])
    fi
  
    ltp_version_list="1.6 1.7 1.8 1.10 1.11 1.12"
    AC_CHECK_PROG(LTP, lcov, lcov)
    AC_CHECK_PROG(LTP_GENHTML, genhtml, genhtml)
  
    if test "$LTP"; then
      AC_CACHE_CHECK([for ltp version], glib_cv_ltp_version, [
        glib_cv_ltp_version=invalid
        ltp_version=`$LTP -v 2>/dev/null | $SED -e 's/^.* //'`
        for ltp_check_version in $ltp_version_list; do
          if test "$ltp_version" = "$ltp_check_version"; then
            glib_cv_ltp_version="$ltp_check_version (ok)"
          fi
        done
      ])
    else
      ltp_msg="To enable code coverage reporting you must have one of the following LTP versions installed: $ltp_version_list"
      AC_MSG_ERROR([$ltp_msg])
    fi
  
    case $glib_cv_ltp_version in
      ""|invalid[)]
        ltp_msg="You must have one of the following versions of LTP: $ltp_version_list (found: $ltp_version)."
        AC_MSG_ERROR([$ltp_msg])
        LTP="exit 0;"
        ;;
    esac
  
    if test -z "$LTP_GENHTML"; then
      AC_MSG_ERROR([Could not find genhtml from the LTP package])
    fi
  
    AC_DEFINE(HAVE_GCOV, 1, [Whether you have gcov])
  
    dnl Remove all optimization flags from CFLAGS
    changequote({,})
    CFLAGS=`echo "$CFLAGS" | $SED -e 's/-O[0-9]*//g'`
    changequote([,])
  
    dnl Add the special gcc flags
    CFLAGS="$CFLAGS -O0 -fprofile-arcs -ftest-coverage"
    LDFLAGS="$LDFLAGS -lgcov"
  fi
fi
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
       code coverage: $use_gcov

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
