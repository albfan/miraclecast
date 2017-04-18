#!/bin/sh

set -e

oldpwd=$(pwd)
topdir=$(dirname $0)
cd $topdir

#intltoolize --force --automake
autoreconf --force --install --symlink

libdir() {
  echo $(cd "$1/$(gcc -print-multi-os-directory)"; pwd)
}

args="\
--sysconfdir=/etc \
--localstatedir=/var \
--libdir=$(libdir /usr/lib) \
"

if [ -f "$topdir/.config.args" ]; then
  args="$args $(cat $topdir/.config.args)"
fi

cd $oldpwd

if [ "x$1" = "xc" ]; then
  shift
  args="$args $@"
  $topdir/configure CFLAGS='-g -O0 -ftrapv' $args
  make clean
elif [ "x$1" = "xg" ]; then
  shift
  args="$args $@"
  $topdir/configure CFLAGS='-g -O0 -ftrapv' $args
  make clean
elif [ "x$1" = "xa" ]; then
  shift
  args="$args $@"
  $topdir/configure CFLAGS='-g -O0 -Wsuggest-attribute=pure -Wsuggest-attribute=const -ftrapv' $args
  make clean
elif [ "x$1" = "xl" ]; then
  shift
  args="$args $@"
  $topdir/configure CC=clang CFLAGS='-g -O0 -ftrapv' $args
  make clean
elif [ "x$1" = "xs" ]; then
  shift
  args="$args $@"
  scan-build $topdir/configure CFLAGS='-std=gnu99 -g -O0 -ftrapv' $args
  scan-build make
else
  cat <<EOF

----------------------------------------------------------------
Initialized build system. For a common configuration please run:
----------------------------------------------------------------

$topdir/configure CFLAGS='-g -O0 -ftrapv' $args

or run $0 with param:

- c: compilation
- g: debugging
- a: pure/const warning
- l: clang build
- s: scan-build reporting

EOF

fi
