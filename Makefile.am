#
# MiracleCast - Global Makefile
# Copyright (c) 2013-2014 David Herrmann <dh.herrmann@gmail.com>
#

#
# Global Configurations and Initializations
#

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests

SUBDIRS = .

.DELETE_ON_ERROR:

include_HEADERS =
EXTRA_DIST = \
	README \
	COPYING \
	NEWS
CLEANFILES =
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =
TPHONY =

TESTS =
bin_PROGRAMS =
check_PROGRAMS =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =

#
# Default CFlags
# Make all files include "config.h" by default. This shouldn't cause any
# problems and we cannot forget to include it anymore.
# Also make the linker discard all unused symbols.
#

AM_CFLAGS = \
	-Wall \
	-pipe \
	-fno-common \
	-ffast-math \
	-fdiagnostics-show-option \
	-fno-strict-aliasing \
	-fvisibility=hidden \
	-ffunction-sections \
	-fdata-sections
AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-I $(srcdir)/src \
	-I /usr/local/include \
	-DBUILD_ENABLE_DEBUG \
	'-DBUILD_BINDIR="$(bindir)"'
AM_LDFLAGS = \
	-Wl,--as-needed \
	-Wl,--gc-sections \
	-Wl,-z,relro \
	-Wl,-z,now
AM_LIBADD = \
	-lsystemd

#
# SHL - Static Helper Library
# The SHL subsystem contains several small code pieces used all over libwfd and
# other applications.
#

noinst_LTLIBRARIES += libshl.la

libshl_la_SOURCES = \
	src/shl_dlist.h \
	src/shl_htable.h \
	src/shl_htable.c \
	src/shl_log.h \
	src/shl_log.c \
	src/shl_macro.h \
	src/shl_util.h \
	src/shl_util.c
libshl_la_CPPFLAGS = $(AM_CPPFLAGS)
libshl_la_LDFLAGS = $(AM_LDFLAGS)
libshl_la_LIBADD = $(AM_LIBADD)

#
# gdhcp - dhcp implementation
#

noinst_LTLIBRARIES += libgdhcp.la

libgdhcp_la_SOURCES = \
	src/gdhcp/gdhcp.h \
	src/gdhcp/unaligned.h \
	src/gdhcp/common.h \
	src/gdhcp/common.c \
	src/gdhcp/ipv4ll.h \
	src/gdhcp/ipv4ll.c \
	src/gdhcp/client.c \
	src/gdhcp/server.c
libgdhcp_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(GDHCP_CFLAGS)
libgdhcp_la_LDFLAGS = $(AM_LDFLAGS)
libgdhcp_la_LIBADD = \
	$(AM_LIBADD) \
	$(GDHCP_LIBS)

#
# miraclectl
#

bin_PROGRAMS += miraclectl

miraclectl_SOURCES = \
	src/miraclectl.c
miraclectl_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS)
miraclectl_LDADD = \
	libshl.la \
	-lreadline \
	$(DEPS_LIBS)
miraclectl_LDFLAGS = $(AM_LDFLAGS)

#
# miracle-dhcp
#

bin_PROGRAMS += miracle-dhcp

miracle_dhcp_SOURCES = \
	src/miracle-dhcp.c
miracle_dhcp_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(GDHCP_CFLAGS)
miracle_dhcp_LDADD = \
	libshl.la \
	libgdhcp.la \
	$(DEPS_LIBS) \
	$(GDHCP_LIBS)
miracle_dhcp_LDFLAGS = $(AM_LDFLAGS)

#
# miracled
#

bin_PROGRAMS += miracled

miracled_SOURCES = \
	src/miracled.h \
	src/miracled.c \
	src/miracled-dbus.c \
	src/miracled-link.c \
	src/miracled-peer.c \
	src/miracled-wifi.c
miracled_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS)
miracled_LDADD = \
	libshl.la \
	$(DEPS_LIBS)
miracled_LDFLAGS = $(AM_LDFLAGS)

#
# Tests
#

tests =

if BUILD_HAVE_CHECK
check_PROGRAMS += $(tests)
TESTS += $(tests)
endif

test_sources = \
	test/test_common.h
test_libs = \
	libshl.la \
	$(DEPS_LIBS) \
	$(CHECK_LIBS)
test_cflags = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(CHECK_CFLAGS)
test_lflags = \
	$(AM_LDFLAGS)

#
# Phony targets
#

.PHONY: $(TPHONY)

#
# Empty .SECONDARY target causes alle intermediate files to be treated as
# secondary files. That is, they don't get deleted after make finished.
#

.SECONDARY:
