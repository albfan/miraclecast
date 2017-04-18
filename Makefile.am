SUBDIRS = src res test
EXTRA_DIST = README.md \
	COPYING \
	NEWS

ACLOCAL_AMFLAGS = -I m4

.PHONY: lcov genlcov lcov-clean

lcov:
	-$(MAKE) $(AM_MAKEFLAGS) -k check
	$(MAKE) $(AM_MAKEFLAGS) genlcov

# we have to massage the lcov.info file slightly to hide the effect of libtool
# placing the objects files in the .libs/ directory separate from the *.c
genlcov:
	$(LTP) --directory $(top_builddir) --capture --output-file miraclecast-lcov.info --test-name GLIB_PERF --no-checksum
	LANG=C $(LTP_GENHTML) --prefix $(top_builddir) --output-directory miraclecast-lcov --title "Miraclecast Code Coverage" --legend --show-details miraclecast-lcov.info

lcov-clean:
	-$(LTP) --directory $(top_builddir) -z
	-rm -rf miraclecast-lcov.info miraclecast-lcov
	-find -name '*.gcda' -print | xargs -Ix rm x

