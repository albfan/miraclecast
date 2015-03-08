
add_subdirectory(shared)
add_subdirectory(wifi)
add_subdirectory(dhcp)
add_subdirectory(ctl)


########### next target ###############

set(miracled_SRCS miracled.h miracled.c)
include_directories(shared)
link_directories(shared)
add_executable(miracled ${miracled_SRCS})
target_link_libraries(miracled miracle-shared)
find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED glib-2.0)
pkg_check_modules ( REQUIRED glib-2.0)
if (GLIB2_FOUND)
   message("let's go $GLIB2_INCLUDE_DIRS $GLIB2_LIBRARIES")
   #include_directories(${BZIP_INCLUDE_DIRS})
   #target_link_libraries (miracled ${BZIP2_LIBRARIES})
endif (GLIB2_FOUND) 
#target_link_libraries(miracled glib)
install(TARGETS miracled DESTINATION bin)


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
