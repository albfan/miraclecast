include $(top_srcdir)/common.am
bin_PROGRAMS = miracle-wifid

miracle_wifid_SOURCES = \
	wifid.h \
	wifid.c \
	wifid-dbus.c \
	wifid-link.c \
	wifid-peer.c \
	wifid-supplicant.c
miracle_wifid_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(GLIB_CFLAGS)
miracle_wifid_LDADD = \
	../shared/libmiracle-shared.la \
	$(DEPS_LIBS) \
	$(GLIB_LIBS)

