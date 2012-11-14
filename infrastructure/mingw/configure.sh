#!/bin/sh

DEP_PATH=/usr/i686-pc-mingw32

if [ ! -r "$DEP_PATH/lib/libssl.a" ]; then
	echo "Error: install OpenSSL as instructed by" \
		"docs/backup/win32_build_on_cygwin_using_mingw.txt" >&2
	exit 2
fi

if [ ! -r "$DEP_PATH/lib/libpcreposix.a" \
	-o ! -r "$DEP_PATH/lib/libpcre.a" \
	-o ! -r "$DEP_PATH/include/pcreposix.h" ]; then
	echo "Error: install PCRE as instructed by" \
		"docs/backup/win32_build_on_cygwin_using_mingw.txt" >&2
	exit 2
fi

LIBZ_PATH="${DEP_PATH}/sys-root/mingw/lib"

if [ ! -r "$LIBZ_PATH/libz.dll.a" ]; then
	echo "Error: upgrade your Cygwin mingw-zlib-devel package" >&2
	exit 2
fi

if [ ! -x "configure" ]; then
	if ! ./bootstrap; then
		echo "Error: bootstrap failed, aborting." >&2
		exit 1
	fi
fi

if ! ./configure "$@" --target=i686-pc-mingw32 \
	CFLAGS="-mno-cygwin -mthreads" \
	CPPFLAGS="-mno-cygwin" \
	CXXFLAGS="-mno-cygwin -mthreads" \
	LDFLAGS="-Wl,-Bstatic -mno-cygwin -mthreads -L${DEP_PATH}/lib -L${LIBZ_PATH}"
then
	echo "Error: configure failed, aborting." >&2
	exit 1
fi

exit 0
