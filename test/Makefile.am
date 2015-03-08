include $(top_srcdir)/common.am
tests = \
	test_rtsp \
	test_wpas

if BUILD_HAVE_CHECK
check_PROGRAMS = $(tests) test_valgrind
TESTS = $(tests) test_valgrind
MEMTESTS = $(tests)
endif

test_sources = \
	test_common.h
test_libs = \
	../src/shared/libmiracle-shared.la \
	$(DEPS_LIBS) \
	$(CHECK_LIBS)
test_cflags = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(CHECK_CFLAGS)

test_rtsp_SOURCES = test_rtsp.c $(test_sources)
test_rtsp_CPPFLAGS = $(test_cflags)
test_rtsp_LDADD = $(test_libs)

test_valgrind_SOURCES = test_valgrind.c $(test_sources)
test_valgrind_CPPFLAGS = $(test_cflags)
test_valgrind_LDADD = $(test_libs)

test_wpas_SOURCES = test_wpas.c $(test_sources)
test_wpas_CPPFLAGS = $(test_cflags)
test_wpas_LDADD = $(test_libs)

## custom recipes

VALGRIND = CK_FORK=no valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --leak-resolution=high --error-exitcode=1 --suppressions=$(top_builddir)/test.supp

# verify that test_valgrind actually leaks data
memcheck-verify: check
	$(AM_V_GEN)$(VALGRIND) --log-file=/dev/null ./test_valgrind >/dev/null ; test 1 = $$?

# run memcheck tests via valgrind
memcheck: memcheck-verify
	$(AM_V_GEN)for i in $(MEMTESTS) ; do \
		$(VALGRIND) --log-file=$(top_builddir)/$$i.memlog \
			$(top_builddir)/$$i >/dev/null || (echo "memcheck failed on: $$i" ; exit 1) ; \
	done

distcheck-hook: memcheck
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests

#
# Phony targets
#

.PHONY: memcheck-verify
