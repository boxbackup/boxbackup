#!/bin/sh

if [ ! -r "/usr/i686-pc-mingw32/lib/libssl.a" ]; then
	echo "Error: install OpenSSL as instructed by" \
		"docs/backup/win32_build_on_cygwin_using_mingw.txt" >&2
	exit 2
fi

if [ ! -r "/usr/lib/mingw/libpcreposix.a" \
	-o ! -r "/usr/lib/mingw/libpcre.a" \
	-o ! -r "/usr/include/mingw/pcreposix.h" ]; then
	echo "Error: install PCRE as instructed by" \
		"docs/backup/win32_build_on_cygwin_using_mingw.txt" >&2
	exit 2
fi

export CXX="g++ -mno-cygwin"
export LD="g++ -mno-cygwin"
export CFLAGS="-mno-cygwin -mthreads"
export CXXFLAGS="-mno-cygwin -mthreads"
export LDFLAGS="-mno-cygwin -mthreads"
export LIBS="-lcrypto -lws2_32 -lgdi32"

if [ ! -x "configure" ]; then
	if ! ./bootstrap; then
		echo "Error: bootstrap failed, aborting." >&2
		exit 1
	fi
fi

if ! ./configure --target=i686-pc-mingw32; then
	echo "Error: configure failed, aborting." >&2
	exit 1
fi

exit 0
