include $(top_srcdir)/common.am
SUBDIRS = shared wifi dhcp ctl uibc

bin_PROGRAMS = miracled

miracled_SOURCES = \
	miracled.h \
	miracled.c
miracled_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS)
miracled_LDADD = \
	shared/libmiracle-shared.la \
	$(DEPS_LIBS)

