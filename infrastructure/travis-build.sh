#!/bin/sh

set -e
set -x

ccache -s

cd `dirname $0`/..
./bootstrap
./configure CC="ccache $CC" CXX="ccache $CXX" "$@"
grep CXX config.status
make V=1
./runtest.pl ALL $TEST_TARGET
# When making a release build, also check that we can build the default
# target and "parcels" (which is the same thing):
if [ "$TEST_TARGET" = "release" ]; then
	make
	make parcels
fi

ccache -s
