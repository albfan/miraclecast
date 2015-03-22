
########### next target ###############

set(miracle-wifictl_SRCS ctl.h 
                         ctl-cli.c 
                         ctl-wifi.c 
                         wifictl.c)

add_executable(miracle-wifictl ${miracle-wifictl_SRCS})

target_link_libraries(miracle-wifictl ${KDE4_KDECORE_LIBS})

install(TARGETS miracle-wifictl DESTINATION bin)


########### next target ###############

set(miracle-sinkctl_SRCS ctl.h 
                         ctl-cli.c 
                         ctl-sink.c 
                         ctl-wifi.c 
                         sinkctl.c)

add_executable(miracle-sinkctl ${miracle-sinkctl_SRCS})

target_link_libraries(miracle-sinkctl ${KDE4_KDECORE_LIBS})

install(TARGETS miracle-sinkctl DESTINATION bin)


########### install files ###############




#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#bin_PROGRAMS = miracle-wifictl miracle-sinkctl
#
#miracle_wifictl_SOURCES = \
#	ctl.h \
#	ctl-cli.c \
#	ctl-wifi.c \
#	wifictl.c
#miracle_wifictl_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS)
#miracle_wifictl_LDADD = \
#	../shared/libmiracle-shared.la \
#	-lreadline \
#	$(DEPS_LIBS)
#
#miracle_sinkctl_SOURCES = \
#	ctl.h \
#	ctl-cli.c \
#	ctl-sink.c \
#	ctl-wifi.c \
#	sinkctl.c
#miracle_sinkctl_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS)
#miracle_sinkctl_LDADD = \
#	../shared/libmiracle-shared.la \
#	-lreadline \
#	$(DEPS_LIBS)
#
#
