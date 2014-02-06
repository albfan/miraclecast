#!/bin/sh
set -e

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

origdir=`pwd`
cd $srcdir

mkdir -p m4
autoreconf -is --force

cd $origdir

if test -z "$NOCONFIGURE" ; then
    exec $srcdir/configure "$@"
fi
