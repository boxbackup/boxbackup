cmake_minimum_required(VERSION 2.6)

project(BoxBackup_Windows)

set(boxbackup_dir ${CMAKE_SOURCE_DIR}/../../..)
set_property(DIRECTORY PROPERTY EP_PREFIX .)
set(install_dir ${CMAKE_BINARY_DIR}/install)

# Automate the process of downloading, building and "installing" dependencies on Windows,
# as used by AppVeyor.
set(ZLIB_VERSION    1.2.10 CACHE STRING "Version of zlib to download, build, and compile Box Backup against")
set(ZLIB_HASH SHA256=a23d3d1da69d800d96d749c0a7c1a5b423dcc7381d1d5095d8f14a28dc453733
	CACHE STRING "Hash of zlib-${ZLIB_VERSION}.zip, to be verified after download")
set(OPENSSL_VERSION 1.0.2h CACHE STRING "Version of OpenSSL to download, build, and compile Box Backup against")
set(OPENSSL_HASH SHA256=1d4007e53aad94a5b2002fe045ee7bb0b3d98f1a47f8b2bc851dcd1c74332919
	CACHE STRING "Hash of the OpenSSL download file, to be verified after download")
set(PCRE_VERSION    8.38   CACHE STRING "Version of PCRE to download, build, and compile Box Backup against")
set(PCRE_HASH SHA256=dbef7cf80258c29396d435804cd5dba34006a77548850bca8bad6db6a6eac110
	CACHE STRING "Hash of the PCRE download file, to be verified after download")
set(BOOST_VERSION    1.62.0   CACHE STRING "Version of Boost to download, unpack, and compile Box Backup against")
set(BOOST_HASH SHA1=5fd97433c3f859d8cbab1eaed4156d3068ae3648
	CACHE STRING "Hash of the Boost download file, to be verified after download")

include(ExternalProject)

string(REPLACE "." "" zlib_version_nodots ${ZLIB_VERSION})
ExternalProject_Add(zlib
	URL "http://zlib.net/zlib${zlib_version_nodots}.zip"
	URL_HASH ${ZLIB_HASH}
	DOWNLOAD_NO_PROGRESS 1
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${install_dir}
	# We need to build both versions, debug and release, because cmake requires both to be
	# present to generate its multi-configuration project files for Visual Studio/MSBuild.
	INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Debug
	COMMAND         ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Release
	STEP_TARGETS configure install
)

if(WIN32)
	ExternalProject_Add(openssl
		DEPENDS zlib
		URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
		URL_HASH ${OPENSSL_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CONFIGURE_COMMAND perl Configure debug-VC-WIN32 no-asm --prefix=${install_dir}
		COMMAND cmd /c ms\\do_ms.bat
		# You would expect us to use nt.mak to compile a static library here, but mk1mf.pl uses the /MT[d]
		# CRT in that case, which is incompatible with our dynamic runtime, /MD[d]. It seems that the libs
		# built by ntdll.mak, which are compiled with /MD[d], are full libraries and not import libs,
		# so we can link statically against them and still get a dynamic runtime.
		BUILD_IN_SOURCE 1
		BUILD_COMMAND   nmake /s /f ms\\nt.mak
		INSTALL_COMMAND nmake /s /f ms\\nt.mak install
	)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	ExternalProject_Add(openssl
		DEPENDS zlib
		URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
		URL_HASH ${OPENSSL_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CONFIGURE_COMMAND perl Configure darwin64-x86_64-cc --prefix=${install_dir}
		BUILD_IN_SOURCE 1
	)
else()
	ExternalProject_Add(openssl
		DEPENDS zlib
		URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
		URL_HASH ${OPENSSL_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CONFIGURE_COMMAND ./config --prefix=${install_dir}
		BUILD_IN_SOURCE 1
	)

endif()

ExternalProject_Add(pcre
	URL "http://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-${PCRE_VERSION}.zip"
	URL_HASH ${PCRE_HASH}
	DOWNLOAD_NO_PROGRESS 1
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${install_dir}
	# We need to build both versions, debug and release, because cmake requires both to be
	# present to generate its multi-configuration project files for Visual Studio/MSBuild.
	INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Debug
	COMMAND         ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Release
)

string(REPLACE "." "_" BOOST_VERSION_UNDERSCORES ${BOOST_VERSION})
ExternalProject_Add(boost
	URL "http://downloads.sourceforge.net/project/boost/boost/${BOOST_VERSION}/boost_${BOOST_VERSION_UNDERSCORES}.tar.bz2"
	URL_HASH ${BOOST_HASH}
	# DOWNLOAD_NO_PROGRESS 1
	CONFIGURE_COMMAND "" # none needed
	BUILD_COMMAND ${CMAKE_COMMAND} -E echo "No build step needed" # none needed
	INSTALL_COMMAND "" # none needed
)

ExternalProject_Add(boxbackup
	DEPENDS zlib openssl pcre
	SOURCE_DIR ${boxbackup_dir}/infrastructure/cmake
	CMAKE_ARGS
		-DZLIB_ROOT=${install_dir}
		-DOPENSSL_ROOT_DIR=${install_dir}
		-DPCRE_ROOT=${install_dir}
		-DBOOST_ROOT=${CMAKE_BINARY_DIR}/src/boost
		-DAPPVEYOR_MODE=1
		-DDEBUG=${DEBUG}
	STEP_TARGETS configure build install
)
