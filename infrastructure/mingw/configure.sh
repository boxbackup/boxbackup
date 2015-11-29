#!/bin/sh

source `dirname $0`/environment.sh

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

if ! ./configure "$@" --host=$target \
	--with-ssl-headers="${DEP_PATH}/include" \
	--with-ssl-lib="${DEP_PATH}/lib" \
	CFLAGS="-mthreads" \
	CXXFLAGS="-mthreads" \
	LDFLAGS="-Wl,-Bstatic -mthreads -L${LIBZ_PATH} -lgdi32"
then
	echo "Error: configure failed, aborting." >&2
	exit 1
fi

exit 0
