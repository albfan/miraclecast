
########### next target ###############

set(miracle-wifid_SRCS wifid.h 
                       wifid.c 
                       wifid-dbus.c 
                       wifid-link.c 
                       wifid-peer.c 
                       wifid-supplicant.c)

add_executable(miracle-wifid ${miracle-wifid_SRCS})

target_link_libraries(miracle-wifid ${KDE4_KDECORE_LIBS})

cmake_policy(SET CMP0015 NEW)
include_directories(shared)
link_directories(shared)
target_link_libraries(miracle-wifid miracle-shared)

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)
#link_directories( ${UDEV_LIBRARY_DIRS})
#include_directories( ${UDEV_INCLUDE_DIRS})
target_link_libraries(miracle-wifid ${UDEV_LIBRARIES})
link_directories( ${GLIB2_LIBRARY_DIRS})
include_directories( ${GLIB2_INCLUDE_DIRS})
target_link_libraries(miracle-wifid ${GLIB2_LIBRARIES})

install(TARGETS miracle-wifid DESTINATION bin)
INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)

########### install files ###############

#set(CMAKE_MODULE_PATH  ${PROJECT_SOURCE_DIR}/cmake-extensions/ )





#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#bin_PROGRAMS = miracle-wifid
#
#miracle_wifid_SOURCES = \
#	wifid.h \
#	wifid.c \
#	wifid-dbus.c \
#	wifid-link.c \
#	wifid-peer.c \
#	wifid-supplicant.c
#miracle_wifid_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS)
#miracle_wifid_LDADD = \
#	../shared/libmiracle-shared.la \
#	$(DEPS_LIBS)
#
