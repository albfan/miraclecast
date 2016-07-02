set(CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")

add_subdirectory(shared)
add_subdirectory(wifi)
add_subdirectory(dhcp)
add_subdirectory(ctl)
add_subdirectory(uibc)

set(miracled_SRCS miracled.h miracled.c)
add_executable(miracled ${miracled_SRCS})
target_link_libraries(miracled miracle-shared)
install(TARGETS miracled DESTINATION bin)

INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)


########### install files ###############




#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#SUBDIRS = shared wifi dhcp ctl
#
#bin_PROGRAMS = miracled
#
#miracled_SOURCES = \
#	miracled.h \
#	miracled.c
#miracled_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS)
#miracled_LDADD = \
#	shared/libmiracle-shared.la \
#	$(DEPS_LIBS)
#
