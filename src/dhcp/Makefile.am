include $(top_srcdir)/common.am
bin_PROGRAMS = miracle-dhcp

miracle_dhcp_SOURCES = \
	dhcp.c \
	gdhcp.h \
	unaligned.h \
	common.h \
	common.c \
	ipv4ll.h \
	ipv4ll.c \
	client.c \
	server.c
miracle_dhcp_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS) \
	$(GLIB_CFLAGS)
miracle_dhcp_LDADD = \
	../shared/libmiracle-shared.la \
	$(DEPS_LIBS) \
	$(GLIB_LIBS)



