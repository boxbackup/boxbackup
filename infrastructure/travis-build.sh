#!/bin/sh

set -e
set -x

ccache -s

if [ "$BUILD" = 'cmake' ]; then
	cd `dirname $0`
	mkdir -p cmake/build
	cd cmake/build
	cmake --version
	cmake -DCMAKE_BUILD_TYPE:STRING=$TEST_TARGET ..
	make install
	[ "$TEST" = "n" ] || ctest -C $TEST_TARGET -V
else
	cd `dirname $0`/..
	./bootstrap
	./configure CC="ccache $CC" CXX="ccache $CXX" "$@"
	grep CXX config.status
	make V=1
	./runtest.pl ALL $TEST_TARGET
	if [ "$TEST_TARGET" = "release" ]; then
		make
		make parcels
	fi
fi

ccache -s
