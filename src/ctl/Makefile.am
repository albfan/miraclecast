include $(top_srcdir)/common.am
bin_PROGRAMS = miracle-wifictl miracle-sinkctl

miracle_wifictl_SOURCES = \
	ctl.h \
	ctl-cli.c \
	ctl-wifi.c \
	wifictl.c
miracle_wifictl_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(GLIB_CFLAGS)
miracle_wifictl_LDADD = \
	../shared/libmiracle-shared.la \
	-lreadline \
	$(DEPS_LIBS)

miracle_sinkctl_SOURCES = \
	ctl.h \
	ctl-cli.c \
	ctl-sink.h \
	ctl-sink.c \
	ctl-wifi.c \
	wfd.c \
	sinkctl.c
miracle_sinkctl_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(GLIB_CFLAGS)
miracle_sinkctl_LDADD = \
	../shared/libmiracle-shared.la \
	-lreadline \
	$(DEPS_LIBS) \
	$(GLIB_LIBS)


