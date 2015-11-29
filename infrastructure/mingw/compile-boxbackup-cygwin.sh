#!/bin/sh

set -e
set -x

basedir=`cd $(dirname $0)/../../.. && pwd`
cd $basedir

wget -c https://cygwin.com/setup-x86_64.exe \
|| powershell wget https://cygwin.com/setup-x86_64.exe -UseBasicParsing -outfile setup-x86_64.exe

chmod a+x setup-x86_64.exe
./setup-x86_64.exe --quiet-mode --packages \
	"autoconf,automake,gdb,make,mingw64-x86_64-gcc,mingw64-x86_64-gcc-g++,
	,mingw64-x86_64-zlib,libxml2,libxslt,perl,subversion,unzip,vim,wget"

install_prefix=/usr/x86_64-w64-mingw32
compiler_prefix=x86_64-w64-mingw32

openssl_source=https://www.openssl.org/source/
latest_openssl=`wget -O- -q $openssl_source \
| grep '<td><a href="openssl-1.0.*.tar.gz">' \
| sed -e 's/.tar.gz">.*//' -e 's/.*"//' | sort | tail -1`

wget -c $openssl_source/$latest_openssl.tar.gz
tar xzf $latest_openssl.tar.gz --exclude $latest_openssl/Makefile
(
	cd $latest_openssl
	[ -r Makefile ] || \
		./Configure --prefix=$install_prefix mingw64 \
		--cross-compile-prefix=$compiler_prefix-
	make
	make install_sw
)

pcre_url=`wget -O- -q ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/ \
| grep 'pcre-8.*.tar.bz2"' | sed -e 's/">pcre-.*//' | sed -e 's/.*<a href="//' \
| tail -1`
pcre_version=`basename $pcre_url .tar.bz2`
wget -c $pcre_url
tar xjf `basename $pcre_url`
(
	cd $pcre_version
	./configure --prefix=$install_prefix --disable-shared \
		--host=$compiler_prefix
	make
	make install
)

