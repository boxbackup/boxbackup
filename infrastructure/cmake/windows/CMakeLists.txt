cmake_minimum_required(VERSION 2.6)

project(BoxBackup_Windows)

set(boxbackup_dir ${CMAKE_SOURCE_DIR}/../../..)
set_property(DIRECTORY PROPERTY EP_BASE .)
set(install_dir ${CMAKE_BINARY_DIR}/install)
set(cache_dir ${CMAKE_BINARY_DIR}/cache)
file(MAKE_DIRECTORY ${cache_dir})

# Automate the process of downloading, building and "installing" dependencies on Windows,
# as used by AppVeyor.

# Version of zlib to download, build, and compile Box Backup against:
set(ZLIB_VERSION    1.2.11)
# Hash of zlib-${ZLIB_VERSION}.tar.gz, to be verified after download:
set(ZLIB_HASH SHA256=c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1)

# Version of OpenSSL to download, build, and compile Box Backup against:
set(OPENSSL_VERSION 1.1.0g)
# Hash of openssl-${OPENSSL_VERSION}.tar.gz, to be verified after download:
set(OPENSSL_HASH SHA256=de4d501267da39310905cb6dc8c6121f7a2cad45a7707f76df828fe1b85073af)

# Version of PCRE to download, build, and compile Box Backup against:
set(PCRE_VERSION    8.41)
# Hash of pcre-${PCRE_VERSION}.tar.bz2, to be verified after download:
set(PCRE_HASH SHA256=e62c7eac5ae7c0e7286db61ff82912e1c0b7a0c13706616e94a7dd729321b530)

# Version of Boost to download, unpack, and compile Box Backup against:
set(BOOST_VERSION    1.62.0)
# Hash of the Boost download file, to be verified after download:
set(BOOST_HASH SHA1=5fd97433c3f859d8cbab1eaed4156d3068ae3648)

include(ExternalProject)

file(MD5 ${CMAKE_CURRENT_LIST_FILE} cmake_lists_hash)

set(zlib_install_dir "${install_dir}/zlib")
set(zlib_package_file "${cache_dir}/zlib_${cmake_lists_hash}.tgz")

function(ExternalProject_Cached project_name package_file install_path)
	ExternalProject_Add(${project_name}
		DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E echo
			"No download step needed (using cached package)"
		CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo
			"No configure step needed (using cached package)"
		BUILD_COMMAND ${CMAKE_COMMAND} -E echo
			"No build step needed (using cached package)"
		INSTALL_COMMAND ${CMAKE_COMMAND} -E tar xzvf ${package_file}
			${install_path}
	)
endfunction()

if(EXISTS ${zlib_package_file})
	ExternalProject_Cached(zlib ${zlib_package_file} ${zlib_install_dir})
else()
	ExternalProject_Add(zlib
		URL "http://zlib.net/zlib-${ZLIB_VERSION}.tar.gz"
		URL_HASH ${ZLIB_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${zlib_install_dir} ${SUB_CMAKE_EXTRA_ARGS}
		# We need to build both versions, debug and release, because cmake requires both to be
		# present to generate its multi-configuration project files for Visual Studio/MSBuild.
		INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Debug
		COMMAND         ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Release
		COMMAND         ${CMAKE_COMMAND} -E tar czvf ${zlib_package_file}
			${zlib_install_dir}
		STEP_TARGETS configure install
	)
endif()

set(openssl_install_dir "${install_dir}/openssl")
set(openssl_package_file "${cache_dir}/openssl_${cmake_lists_hash}.tgz")

if(EXISTS ${openssl_package_file})
	ExternalProject_Cached(openssl ${openssl_package_file} ${openssl_install_dir})
elseif(WIN32)
	if(PLATFORM STREQUAL "x64")
		set(openssl_config debug-VC-WIN64A)
	else()
		set(openssl_config debug-VC-WIN32)
	endif()
	ExternalProject_Add(openssl
		DEPENDS zlib
		URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
		URL_HASH ${OPENSSL_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CONFIGURE_COMMAND
			perl Configure ${openssl_config} no-asm no-shared
			--prefix=${openssl_install_dir}
			--openssldir=etc
		# Run tests before install, but don't make the main target depend on them, so that
		# we don't have to run them whenever we build manually on Windows.
		TEST_BEFORE_INSTALL 1
		TEST_EXCLUDE_FROM_MAIN 1
		BUILD_IN_SOURCE 1
		# Recursive build loses the /s option, so override the MAKE macro to reinstate it
		# <https://github.com/openssl/openssl/issues/5277>:
		BUILD_COMMAND   nmake /s "MAKE=nmake /s"
		TEST_COMMAND    nmake /s test
		# The only way to silence all the "Copying..." messages appears to be redirecting
		# to /dev/null:
		INSTALL_COMMAND cmd /c "nmake /s install >nul"
		STEP_TARGETS build test
	)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	ExternalProject_Add(openssl
		DEPENDS zlib
		URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
		URL_HASH ${OPENSSL_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CONFIGURE_COMMAND perl Configure darwin64-x86_64-cc
			--prefix=${openssl_install_dir}
		BUILD_IN_SOURCE 1
	)
else()
	ExternalProject_Add(openssl
		DEPENDS zlib
		URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
		URL_HASH ${OPENSSL_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CONFIGURE_COMMAND ./config --prefix=${openssl_install_dir}
		BUILD_IN_SOURCE 1
	)
endif()

if(NOT EXISTS ${openssl_package_file})
	# Common to the three non-cached cases above: create cache package after build:
	ExternalProject_Add_Step(openssl package
		DEPENDEES install
		BYPRODUCTS ${openssl_package_file}
		ALWAYS 1
		COMMAND ${CMAKE_COMMAND} -E tar czvf ${openssl_package_file}
			${openssl_install_dir}
	)
	ExternalProject_Add_StepTargets(openssl package)
endif()

set(pcre_install_dir "${install_dir}/pcre")
set(pcre_package_file "${cache_dir}/pcre_${cmake_lists_hash}.tgz")

if(EXISTS ${pcre_package_file})
	ExternalProject_Cached(pcre ${pcre_package_file} ${pcre_install_dir})
else()
	ExternalProject_Add(pcre
		URL "http://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-${PCRE_VERSION}.tar.bz2"
		URL_HASH ${PCRE_HASH}
		DOWNLOAD_NO_PROGRESS 1
		CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${pcre_install_dir} ${SUB_CMAKE_EXTRA_ARGS}
		-DPCRE_SUPPORT_LIBREADLINE=OFF
		-DPCRE_SUPPORT_LIBBZ2=OFF
		-DPCRE_BUILD_PCRECPP=OFF
		-DPCRE_BUILD_PCREGREP=OFF
		-DPCRE_BUILD_TESTS=OFF
		# We need to build both versions, debug and release, because cmake requires both to be
		# present to generate its multi-configuration project files for Visual Studio/MSBuild.
		INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Debug
		COMMAND         ${CMAKE_COMMAND} --build <BINARY_DIR> --target install --config Release
		COMMAND         ${CMAKE_COMMAND} -E tar czvf ${pcre_package_file}
			${pcre_install_dir}
	)
endif()

string(REPLACE "." "_" BOOST_VERSION_UNDERSCORES ${BOOST_VERSION})
ExternalProject_Add(boost
	URL "http://downloads.sourceforge.net/project/boost/boost/${BOOST_VERSION}/boost_${BOOST_VERSION_UNDERSCORES}.tar.bz2"
	URL_HASH ${BOOST_HASH}
	# DOWNLOAD_NO_PROGRESS 1
	# Disable automatic updating (untarring) as it's slow and not necessary
	UPDATE_DISCONNECTED 1
	CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "No configure step needed"
	BUILD_COMMAND ${CMAKE_COMMAND} -E echo "No build step needed"
	INSTALL_COMMAND ${CMAKE_COMMAND} -E echo "No install step needed"
)

if(BOXBACKUP_VERSION)
	list(APPEND boxbackup_cmake_args "-DBOXBACKUP_VERSION=${BOXBACKUP_VERSION}")
endif()
string(REPLACE ";" " " boxbackup_cmake_args "${boxbackup_cmake_args}")

ExternalProject_Add(boxbackup
	DEPENDS zlib openssl pcre boost
	SOURCE_DIR ${boxbackup_dir}/infrastructure/cmake
	CMAKE_ARGS
		-DZLIB_ROOT=${zlib_install_dir}
		-DOPENSSL_ROOT_DIR=${openssl_install_dir}
		-DPCRE_ROOT=${pcre_install_dir}
		-DBOOST_ROOT=${CMAKE_BINARY_DIR}/Source/boost
		-DBOX_SUPPORT_READLINE=OFF
		-DCMAKE_INSTALL_PREFIX=${install_dir}
		-DAPPVEYOR_MODE=1
		-DDEBUG=${DEBUG}
		${boxbackup_cmake_args}
		${SUB_CMAKE_EXTRA_ARGS}
	INSTALL_COMMAND ${CMAKE_COMMAND} -E echo "No install step needed"
	STEP_TARGETS configure build
)
