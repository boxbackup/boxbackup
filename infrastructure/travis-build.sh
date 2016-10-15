#!/bin/sh

set -e
set -x

if [ "$TRAVIS_OS_NAME" == "osx" ]; then
	brew update
	brew install boost ccache openssl
fi

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
	if [ "$TRAVIS_OS_NAME" == "osx" ]; then
		EXTRA_ARGS="--with-ssl-lib=/usr/local/Cellar/openssl/*/lib --with-ssl-headers=/usr/local/Cellar/openssl/*/include -with-boost=/usr/local/Cellar/boost/*"
	fi

	cd `dirname $0`/..
	./bootstrap
	./configure CC="ccache $CC" CXX="ccache $CXX" $EXTRA_ARGS "$@"
	grep CXX config.status
	make V=1
	./runtest.pl ALL $TEST_TARGET
	if [ "$TEST_TARGET" = "release" ]; then
		make
		make parcels
	fi
fi

ccache -s
