
########### next target ###############

set(miracle-wifid_SRCS ${CMAKE_CURRENT_BINARY_DIR}/wifid.h 
                       ${CMAKE_CURRENT_BINARY_DIR}/wifid.c 
                       ${CMAKE_CURRENT_BINARY_DIR}/wifid-dbus.c 
                       ${CMAKE_CURRENT_BINARY_DIR}/wifid-link.c 
                       ${CMAKE_CURRENT_BINARY_DIR}/wifid-peer.c 
                       ${CMAKE_CURRENT_BINARY_DIR}/wifid-supplicant.c)

add_executable(miracle-wifid ${miracle-wifid_SRCS})

target_link_libraries(miracle-wifid ${KDE4_KDECORE_LIBS})

install(TARGETS miracle-wifid DESTINATION bin)


########### install files ###############




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
