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

ccache -s
