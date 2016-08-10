
########### next target ###############

find_package(PkgConfig)
pkg_check_modules (CHECK check)
    
if(CHECK_FOUND)
    set(test_rtsp_SOURCES test_common.h test_rtsp.c)
    add_executable(test_rtsp ${test_rtsp_SOURCES})
    target_link_libraries(test_rtsp miracle-shared)
    target_link_libraries(test_rtsp ${UDEV_LIBRARIES})
    target_link_libraries(test_rtsp ${GLIB2_LIBRARIES})
    target_link_libraries(test_rtsp ${CHECK_LIBRARIES})
    target_link_libraries(test_rtsp ${CHECK_CFLAGS})
    
    set(test_wpas_SOURCES test_common.h test_wpas.c)
    add_executable(test_wpas ${test_wpas_SOURCES})
    target_link_libraries(test_wpas miracle-shared)
    target_link_libraries(test_wpas ${UDEV_LIBRARIES})
    target_link_libraries(test_wpas ${GLIB2_LIBRARIES})
    target_link_libraries(test_wpas ${CHECK_LIBRARIES})
    target_link_libraries(test_wpas ${CHECK_CFLAGS})

    set(test_valgrind_SOURCES test_common.h test_valgrind.c)
    add_executable(test_valgrind ${test_valgrind_SOURCES})
    target_link_libraries(test_valgrind miracle-shared)
    target_link_libraries(test_valgrind ${UDEV_LIBRARIES})
    target_link_libraries(test_valgrind ${GLIB2_LIBRARIES})
    target_link_libraries(test_valgrind ${CHECK_LIBRARIES})
    target_link_libraries(test_valgrind ${CHECK_CFLAGS})

    INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/src/shared)

    set(VALGRIND CK_FORK=no valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --leak-resolution=high --error-exitcode=1 --suppressions=${CMAKE_SOURCE_DIR}/test.supp)

    add_custom_target(memcheck-verify
                    DEPENDS test_rtsp test_wpas test_valgrind
                    COMMAND ${VALGRIND} --log-file=/dev/null ./test_valgrind >/dev/null |
                            test 1 = $$?
                    COMMENT "verify memcheck")

    add_custom_target(memcheck
                    DEPENDS memcheck-verify
                    COMMAND for i in $(MEMTESTS) |
                            do |
                            ${VALGRIND} --log-file=${CMAKE_SOURCE_DIR}/$$i.memlog |
                            	${CMAKE_SOURCE_DIR}/$$i >/dev/null || (echo "memcheck failed on: $$i" ; exit 1) ; |
                            done
                    SOURCES test_rtsp test_valgrind test_wpas
                    COMMENT "verify memcheck")

endif(CHECK_FOUND)

########### install files ###############




#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#tests = \
#	test_rtsp \
#	test_wpas
#
#if BUILD_HAVE_CHECK
#check_PROGRAMS = $(tests) test_valgrind
#TESTS = $(tests) test_valgrind
#MEMTESTS = $(tests)
#endif
#
#test_sources = \
#	test_common.h
#test_libs = \
#	../src/shared/libmiracle-shared.la \
#	$(DEPS_LIBS) \
#	$(CHECK_LIBS)
#test_cflags = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS) \
#	$(CHECK_CFLAGS)
#
#test_rtsp_SOURCES = test_rtsp.c $(test_sources)
#test_rtsp_CPPFLAGS = $(test_cflags)
#test_rtsp_LDADD = $(test_libs)
#
#test_valgrind_SOURCES = test_valgrind.c $(test_sources)
#test_valgrind_CPPFLAGS = $(test_cflags)
#test_valgrind_LDADD = $(test_libs)
#
#test_wpas_SOURCES = test_wpas.c $(test_sources)
#test_wpas_CPPFLAGS = $(test_cflags)
#test_wpas_LDADD = $(test_libs)
#
### custom recipes
#
#VALGRIND = CK_FORK=no valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --leak-resolution=high --error-exitcode=1 --suppressions=$(top_builddir)/test.supp
#
## verify that test_valgrind actually leaks data
#memcheck-verify: check
#	$(AM_V_GEN)$(VALGRIND) --log-file=/dev/null ./test_valgrind >/dev/null ; test 1 = $$?
#
## run memcheck tests via valgrind
#memcheck: memcheck-verify
#	$(AM_V_GEN)for i in $(MEMTESTS) ; do \
#		$(VALGRIND) --log-file=$(top_builddir)/$$i.memlog \
#			$(top_builddir)/$$i >/dev/null || (echo "memcheck failed on: $$i" ; exit 1) ; \
#	done
#
#distcheck-hook: memcheck
#AM_MAKEFLAGS = --no-print-directory
#AUTOMAKE_OPTIONS = color-tests
#
##
## Phony targets
##
#
#.PHONY: memcheck-verify
