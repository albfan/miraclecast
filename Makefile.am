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
MEMTESTS =
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
	src/shared/shl_ring.h \
	src/shared/shl_ring.c \
	src/shared/shl_util.h \
	src/shared/shl_util.c \
	src/shared/util.h \
	src/shared/wpas.h \
	src/shared/wpas.c
libmiracle_shared_la_CPPFLAGS = $(AM_CPPFLAGS)
libmiracle_shared_la_LDFLAGS = $(AM_LDFLAGS)
libmiracle_shared_la_LIBADD = $(AM_LIBADD)

#
# miracle-wifid
#

bin_PROGRAMS += miracle-wifid

miracle_wifid_SOURCES = \
	src/wifi/wifid.h \
	src/wifi/wifid.c \
	src/wifi/wifid-dbus.c \
	src/wifi/wifid-link.c \
	src/wifi/wifid-peer.c \
	src/wifi/wifid-supplicant.c
miracle_wifid_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS)
miracle_wifid_LDADD = \
	libmiracle-shared.la \
	$(DEPS_LIBS)
miracle_wifid_LDFLAGS = $(AM_LDFLAGS)

#
# miracle-wifictl
#

bin_PROGRAMS += miracle-wifictl

miracle_wifictl_SOURCES = \
	src/ctl/ctl.h \
	src/ctl/ctl-cli.c \
	src/ctl/ctl-wifi.c \
	src/ctl/wifictl.c
miracle_wifictl_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS)
miracle_wifictl_LDADD = \
	libmiracle-shared.la \
	-lreadline \
	$(DEPS_LIBS)
miracle_wifictl_LDFLAGS = $(AM_LDFLAGS)

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
	src/miracled.c
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

tests = \
	test_wpas

if BUILD_HAVE_CHECK
check_PROGRAMS += $(tests) test_valgrind
TESTS += $(tests) test_valgrind
MEMTESTS += $(tests)
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

test_rtsp_SOURCES = test/test_rtsp.c $(test_sources)
test_rtsp_CPPFLAGS = $(test_cflags)
test_rtsp_LDADD = $(test_libs)
test_rtsp_LDFLAGS = $(test_lflags)

test_valgrind_SOURCES = test/test_valgrind.c $(test_sources)
test_valgrind_CPPFLAGS = $(test_cflags)
test_valgrind_LDADD = $(test_libs)
test_valgrind_LDFLAGS = $(test_lflags)

test_wpas_SOURCES = test/test_wpas.c $(test_sources)
test_wpas_CPPFLAGS = $(test_cflags)
test_wpas_LDADD = $(test_libs)
test_wpas_LDFLAGS = $(test_lflags)

VALGRIND = CK_FORK=no valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --leak-resolution=high --error-exitcode=1 --suppressions=$(top_builddir)/test.supp

# verify that test_valgrind actually leaks data
memcheck-verify: check
	$(AM_V_GEN)$(VALGRIND) --log-file=/dev/null ./test_valgrind >/dev/null ; test 1 = $$?

TPHONY += memcheck-verify

# run memcheck tests via valgrind
memcheck: memcheck-verify
	$(AM_V_GEN)for i in $(MEMTESTS) ; do \
		$(VALGRIND) --log-file=$(top_builddir)/$$i.memlog \
			$(top_builddir)/$$i >/dev/null || (echo "memcheck failed on: $$i" ; exit 1) ; \
	done

TPHONY += memcheck

distcheck-hook: memcheck

#
# Phony targets
#

.PHONY: $(TPHONY)

#
# Empty .SECONDARY target causes alle intermediate files to be treated as
# secondary files. That is, they don't get deleted after make finished.
#

.SECONDARY:
