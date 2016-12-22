#!/bin/sh

set -e
set -x

if [ "$TRAVIS_OS_NAME" = "osx" ]; then
	brew update
	# Travis appears to have Boost and OpenSSL installed already:
	# brew install boost ccache openssl
	ls /usr/local /usr/local/opt /usr/local/opt/openssl
	brew install ccache
fi

ccache -s

if [ "$BUILD" = 'cmake' ]; then
	if [ -z "$TEST_TARGET" ]; then
		echo "TEST_TARGET must be set to 'release' or 'debug' for CMake builds"
		exit 2
	fi

	if [ "$TRAVIS_OS_NAME" = "osx" ]; then
		EXTRA_ARGS="-DOPENSSL_ROOT_DIR=/usr/local/opt/openssl -DBOOST_ROOT=/usr/local/opt/boost"
	fi

	cd `dirname $0`
	mkdir -p cmake/build
	cd cmake/build
	cmake --version
	cmake -DCMAKE_BUILD_TYPE:STRING=$TEST_TARGET $EXTRA_ARGS "$@" ..
	make install
	[ "$TEST" = "n" ] || ctest -C $TEST_TARGET -V
else
	if [ "$TRAVIS_OS_NAME" = "osx" ]; then
		EXTRA_ARGS="--with-ssl-lib=/usr/local/opt/openssl/lib --with-ssl-headers=/usr/local/opt/openssl/include --with-boost=/usr/local/opt/boost"
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
