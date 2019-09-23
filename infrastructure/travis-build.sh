#!/bin/sh

set -e
set -x

if [ "$TRAVIS_OS_NAME" = "osx" ]; then
	# No need to "brew update" first: https://docs.travis-ci.com/user/reference/osx/#Homebrew
	# brew update

	# Travis appears to have Boost and OpenSSL installed already:
	# brew install boost ccache openssl
	ls /usr/local /usr/local/opt /usr/local/opt/openssl /usr/local/opt/openssl@1.1

	# Use OSX builds to test OpenSSL 1.1 compatibility as well:
	brew install ccache openssl@1.1
fi

ccache -s

if [ "$BUILD" = 'cmake' ]; then
	if [ -z "$TEST_TARGET" ]; then
		echo "TEST_TARGET must be set to 'release' or 'debug' for CMake builds"
		exit 2
	fi

	if [ "$TRAVIS_OS_NAME" = "osx" ]; then
		EXTRA_ARGS="
			-DOPENSSL_ROOT_DIR=/usr/local/opt/openssl@1.1
			-DBOOST_ROOT=/usr/local/opt/boost"
	fi

	cd `dirname $0`
	mkdir -p cmake/build
	cd cmake/build
	cmake --version
	cmake -DCMAKE_BUILD_TYPE:STRING=$TEST_TARGET $EXTRA_ARGS "$@" ..
	make

	[ "$TEST" = "n" ] || ctest -C $TEST_TARGET -V
else
	if [ "$TRAVIS_OS_NAME" = "osx" ]; then
		EXTRA_ARGS="
			--with-ssl-lib=/usr/local/opt/openssl@1.1/lib
			--with-ssl-headers=/usr/local/opt/openssl@1.1/include
			--with-boost=/usr/local/opt/boost"
	fi

	cd `dirname $0`/..
	./bootstrap
	./configure CC="ccache $CC" CXX="ccache $CXX" $EXTRA_ARGS "$@"
	grep CXX config.status
	make V=1 $EXTRA_MAKE_ARGS

	[ "$TEST" = "n" ] || ./runtest.pl ALL $TEST_TARGET

	if [ "$TEST_TARGET" = "release" ]; then
		make
		make parcels
	fi
fi

ccache -s
