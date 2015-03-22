
########### next target ###############

set(miracle-wifid_SRCS wifid.h 
                       wifid.c 
                       wifid-dbus.c 
                       wifid-link.c 
                       wifid-peer.c 
                       wifid-supplicant.c)

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
