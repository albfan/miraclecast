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
	NEWS \
	res/wpa.conf
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
	-I $(srcdir)/src/shared \
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
# Shared Helpers
#

noinst_LTLIBRARIES += libmiracle-shared.la

libmiracle_shared_la_SOURCES = \
	src/shared/shl_dlist.h \
	src/shared/shl_htable.h \
	src/shared/shl_htable.c \
	src/shared/shl_log.h \
	src/shared/shl_log.c \
	src/shared/shl_macro.h \
	src/shared/shl_util.h \
	src/shared/shl_util.c \
	src/shared/util.h \
	src/shared/wpas.h \
	src/shared/wpas.c
libmiracle_shared_la_CPPFLAGS = $(AM_CPPFLAGS)
libmiracle_shared_la_LDFLAGS = $(AM_LDFLAGS)
libmiracle_shared_la_LIBADD = $(AM_LIBADD)

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
	libmiracle-shared.la \
	-lreadline \
	$(DEPS_LIBS)
miraclectl_LDFLAGS = $(AM_LDFLAGS)

#
# miracle-dhcp
#

bin_PROGRAMS += miracle-dhcp

miracle_dhcp_SOURCES = \
	src/dhcp/dhcp.c \
	src/dhcp/gdhcp.h \
	src/dhcp/unaligned.h \
	src/dhcp/common.h \
	src/dhcp/common.c \
	src/dhcp/ipv4ll.h \
	src/dhcp/ipv4ll.c \
	src/dhcp/client.c \
	src/dhcp/server.c
miracle_dhcp_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(GDHCP_CFLAGS)
miracle_dhcp_LDADD = \
	libmiracle-shared.la \
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
	libmiracle-shared.la \
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
	libmiracle-shared.la \
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
