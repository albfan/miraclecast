include $(top_srcdir)/common.am

bin_PROGRAMS = miracle-uibcctl

miracle_uibcctl_SOURCES = \
	miracle-uibcctl.h \
	miracle-uibcctl.c

miracle_uibctcl_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DEPS_CFLAGS)

miracle_uibcctl_LDADD = \
	../shared/libmiracle-shared.la \
	$(LIBM)

