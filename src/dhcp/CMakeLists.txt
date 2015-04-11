
########### next target ###############

set(miracle-dhcp_SRCS dhcp.c 
                      gdhcp.h 
                      unaligned.h 
                      common.h 
                      common.c 
                      ipv4ll.h 
                      ipv4ll.c 
                      client.c 
                      server.c)

add_executable(miracle-dhcp ${miracle-dhcp_SRCS})

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)
link_directories( ${UDEV_LIBRARY_DIRS})
include_directories( ${UDEV_INCLUDE_DIRS})
target_link_libraries(miracle-dhcp ${UDEV_LIBRARIES})
link_directories( ${GLIB2_LIBRARY_DIRS})
include_directories( ${GLIB2_INCLUDE_DIRS})
target_link_libraries(miracle-dhcp ${GLIB2_LIBRARIES})

target_link_libraries(miracle-dhcp miracle-shared)

install(TARGETS miracle-dhcp DESTINATION bin)
INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)


########### install files ###############




#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#bin_PROGRAMS = miracle-dhcp
#
#miracle_dhcp_SOURCES = \
#	dhcp.c \
#	gdhcp.h \
#	unaligned.h \
#	common.h \
#	common.c \
#	ipv4ll.h \
#	ipv4ll.c \
#	client.c \
#	server.c
#miracle_dhcp_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS) \
#	$(GDHCP_CFLAGS)
#miracle_dhcp_LDADD = \
#	../shared/libmiracle-shared.la \
#	$(DEPS_LIBS) \
#	$(GDHCP_LIBS)
#
#
#
