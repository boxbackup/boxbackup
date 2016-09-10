cmake_minimum_required(VERSION 2.6)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
	# Support Unix Makefiles and Ninja
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

project(BoxBackup)
enable_testing()

set(base_dir ${CMAKE_SOURCE_DIR}/../..)

set(files_to_configure
	bin/bbackupd/bbackupd-config
	bin/bbstored/bbstored-certs
	bin/bbstored/bbstored-config
	contrib/mac_osx/org.boxbackup.bbackupd.plist
	contrib/mac_osx/org.boxbackup.bbstored.plist
	contrib/solaris/bbackupd-manifest.xml
	contrib/solaris/bbstored-manifest.xml
	contrib/debian/bbackupd
	contrib/debian/bbstored
	contrib/redhat/bbackupd
	contrib/redhat/bbstored
	contrib/suse/bbackupd
	contrib/suse/bbstored
	contrib/solaris/bbackupd-smf-method
	contrib/solaris/bbstored-smf-method
	contrib/windows/installer/boxbackup.mpi
	infrastructure/BoxPlatform.pm
	infrastructure/makebuildenv.pl
	infrastructure/makedistribution.pl
	lib/bbackupquery/makedocumentation.pl
	lib/common/BoxPortsAndFiles.h
	lib/common/makeexception.pl
	lib/raidfile/raidfile-config
	lib/server/makeprotocol.pl
	runtest.pl
	test/backupstorefix/testfiles/testbackupstorefix.pl
	test/bbackupd/testfiles/bbackupd.conf
	test/bbackupd/testfiles/bbackupd-exclude.conf
	test/bbackupd/testfiles/bbackupd-snapshot.conf
	test/bbackupd/testfiles/bbackupd-symlink.conf
	test/bbackupd/testfiles/bbackupd-temploc.conf
	test/bbackupd/testfiles/extcheck1.pl
	test/bbackupd/testfiles/extcheck2.pl
	test/bbackupd/testfiles/notifyscript.pl
	test/bbackupd/testfiles/syncallowscript.pl
)

# We need to substitute TARGET_PERL in test/bbackupd/testfiles/bbackupd.conf, so define it
# as a variable before running configure_file().
include(FindPerl)
set(TARGET_PERL ${PERL_EXECUTABLE})

function(replace_file_if_different dest_file source_file)
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E
		copy_if_different "${source_file}" "${dest_file}")
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E
		remove "${source_file}")
endfunction()

function(move_file_if_exists source_file dest_file)
	if(EXISTS "${source_file}")
		execute_process(
			COMMAND "${CMAKE_COMMAND}" -E
			rename "${source_file}" "${dest_file}")
	endif()
endfunction()

foreach(file_to_configure ${files_to_configure})
	configure_file("${base_dir}/${file_to_configure}.in" "${base_dir}/${file_to_configure}.out" @ONLY)
	replace_file_if_different(
		"${base_dir}/${file_to_configure}"
		"${base_dir}/${file_to_configure}.out")
endforeach()

# Work out the current Box version (requires Perl) and update lib/common/BoxVersion.h,
# but only if it has changed, to avoid unnecessary complete rebuilds due to timestamps.
execute_process(
	COMMAND ${PERL_EXECUTABLE} ${base_dir}/infrastructure/cmake/getversion.pl
	WORKING_DIRECTORY ${base_dir}/infrastructure
	RESULT_VARIABLE status
	OUTPUT_VARIABLE new_version_define
	ERROR_VARIABLE command_output)
if(NOT status EQUAL 0)
	message(FATAL_ERROR "Failed to execute: "
		"${PERL_EXECUTABLE} ${base_dir}/infrastructure/cmake/getversion.pl: "
		"status ${status}: ${command_output}")
endif()
file(WRITE "${base_dir}/lib/common/BoxVersion.h.new" "${new_version_define}")
replace_file_if_different(
	"${base_dir}/lib/common/BoxVersion.h"
	"${base_dir}/lib/common/BoxVersion.h.new")

add_definitions(-DBOX_CMAKE -DNEED_BOX_VERSION_H)
if(WIN32)
	add_definitions(-DWIN32)
endif()

file(READ "${base_dir}/infrastructure/buildenv-testmain-template.cpp" test_template)

# Parsing Makefile.extra files in CMake script is a pain, so the relevant rules for
# code-generating Perl scripts are hard-coded here.

set(exception_files
	lib/backupclient/ClientException.txt
	lib/backupstore/BackupStoreException.txt
	lib/common/CommonException.txt
	lib/common/ConversionException.txt
	lib/compress/CompressException.txt
	lib/crypto/CipherException.txt
	lib/httpserver/HTTPException.txt
	lib/raidfile/RaidFileException.txt
	lib/server/ServerException.txt
	lib/server/ConnectionException.txt
)

foreach(exception_file ${exception_files})
	string(REGEX MATCH "(.*)/(.*).txt" valid_exception_file ${exception_file})
	if(NOT valid_exception_file)
		message(FATAL_ERROR "invalid exception file: '${exception_file}'")
	endif()

	set(output_file "${base_dir}/${CMAKE_MATCH_1}/autogen_${CMAKE_MATCH_2}.cpp")
	add_custom_command(OUTPUT "${output_file}"
		MAIN_DEPENDENCY "${base_dir}/${exception_file}"
		COMMAND ${PERL_EXECUTABLE} "${base_dir}/lib/common/makeexception.pl" "${CMAKE_MATCH_2}.txt"
		WORKING_DIRECTORY "${base_dir}/${CMAKE_MATCH_1}")

	string(REPLACE "/" "_" module_name ${CMAKE_MATCH_1})
	set(${module_name}_extra_files ${${module_name}_extra_files} ${output_file})
endforeach()

set(protocol_files
	lib/backupstore/BackupProtocol.txt
	test/basicserver/TestProtocol.txt
)

foreach(protocol_file ${protocol_files})
	string(REGEX MATCH "(.*)/(.*).txt" valid_protocol_file ${protocol_file})
	if(NOT valid_protocol_file)
		message(FATAL_ERROR "invalid protocol file: '${protocol_file}'")
	endif()

	set(output_file "${base_dir}/${CMAKE_MATCH_1}/autogen_${CMAKE_MATCH_2}.cpp")
	add_custom_command(OUTPUT "${output_file}"
		MAIN_DEPENDENCY "${base_dir}/${protocol_file}"
		COMMAND ${PERL_EXECUTABLE} "${base_dir}/lib/server/makeprotocol.pl" "${CMAKE_MATCH_2}.txt"
		WORKING_DIRECTORY "${base_dir}/${CMAKE_MATCH_1}")

	string(REPLACE "/" "_" module_name ${CMAKE_MATCH_1})
	set(${module_name}_extra_files ${${module_name}_extra_files} ${output_file})
endforeach()

set(documentation_files
	lib/bbackupquery/Documentation.txt
)

foreach(documentation_file ${documentation_files})
	string(REGEX MATCH "(.*)/(.*).txt" valid_documentation_file ${documentation_file})
	if(NOT valid_documentation_file)
		message(FATAL_ERROR "invalid documentation file: '${documentation_file}'")
	endif()

	set(output_file "${base_dir}/${CMAKE_MATCH_1}/autogen_${CMAKE_MATCH_2}.cpp")
	add_custom_command(OUTPUT "${output_file}"
		MAIN_DEPENDENCY "${base_dir}/${documentation_file}"
		COMMAND ${PERL_EXECUTABLE} "${base_dir}/lib/bbackupquery/makedocumentation.pl"
		WORKING_DIRECTORY "${base_dir}/${CMAKE_MATCH_1}")

	string(REPLACE "/" "_" module_name ${CMAKE_MATCH_1})
	set(${module_name}_extra_files ${${module_name}_extra_files} ${output_file})
endforeach()

file(STRINGS ${base_dir}/modules.txt module_deps REGEX "^[^#]")
foreach(module_dep ${module_deps})

	string(REGEX MATCH "([^	 ]+)[	 ]*(.*)" valid_module_line ${module_dep})
	if(valid_module_line)
		if(DEBUG)
			message(STATUS "found module: ${CMAKE_MATCH_1} -> ${CMAKE_MATCH_2}")
		endif()

		set(module_dir ${CMAKE_MATCH_1})
		set(module_path ${base_dir}/${module_dir})
		string(REPLACE "/" "_" module_name ${CMAKE_MATCH_1})
		string(REPLACE "/" "_" dependencies "${CMAKE_MATCH_2}")

		# We are replacing QDBM's normal build system, and we only want to include
		# the modules that we actually need, to avoid warnings about duplicate
		# definitions, and potential conflicts with Box Backup code in future, so
		# we specify the C files to compile in explicitly.
		if(module_name STREQUAL "qdbm")
			file(GLOB module_files ${module_path}/depot.c ${module_path}/myconf.c)
		else()
			file(GLOB module_files ${module_path}/*.cpp ${module_path}/*.h)
		endif()

		set(module_files ${module_files} ${${module_name}_extra_files})

		string(REGEX REPLACE "^ " "" dependencies "${dependencies}")
		string(REGEX REPLACE " $" "" dependencies "${dependencies}")

		if(module_name MATCHES "^bin_")
			string(REGEX MATCH "^bin_(.*)" valid_exe ${module_name})
			set(bin_name ${CMAKE_MATCH_1})
			if(DEBUG)
				message(STATUS "add executable '${module_name}': '${module_files}'")
			endif()
			add_executable(${module_name} ${module_files})

			# Unfortunately we have to use install(PROGRAMS) instead of
			# install(TARGETS) because TARGETS doesn't allow us to change
			# the executable name.
			install(PROGRAMS "$<TARGET_FILE:${module_name}>"
				CONFIGURATIONS Debug
				DESTINATION "${base_dir}/debug/${module_dir}"
				RENAME "${bin_name}${CMAKE_EXECUTABLE_SUFFIX}")
			install(PROGRAMS "$<TARGET_FILE:${module_name}>"
				CONFIGURATIONS Release
				DESTINATION "${base_dir}/release/${module_dir}"
				RENAME "${bin_name}${CMAKE_EXECUTABLE_SUFFIX}")
		elseif(module_name MATCHES "^test_")
			string(REGEX MATCH "^test_(.*)" valid_test ${module_name})
			set(test_name ${CMAKE_MATCH_1})
			set(bin_name ${module_name})

			if(DEBUG)
				message(STATUS "add test '${module_name}': '${module_files}'")
			endif()

			string(REPLACE "TEST_NAME" ${test_name} test_main "${test_template}")
			file(WRITE "${module_path}/_main.cpp.new" "${test_main}")
			replace_file_if_different(
				"${module_path}/_main.cpp"
				"${module_path}/_main.cpp.new")
			add_executable(${module_name} ${module_files}
				"${module_path}/_main.cpp")

			if(WIN32)
				install(PROGRAMS "$<TARGET_FILE:${module_name}>"
					CONFIGURATIONS Debug
					DESTINATION "${base_dir}/debug/${module_dir}")
				install(PROGRAMS "$<TARGET_FILE:${module_name}>"
					CONFIGURATIONS Release
					DESTINATION "${base_dir}/release/${module_dir}")
				set(test_executable "$<TARGET_FILE_NAME:${module_name}>")
			else()
				# Unfortunately we have to use install(PROGRAMS) instead of
				# install(TARGETS) because TARGETS doesn't allow us to change
				# the executable name.
				install(PROGRAMS "$<TARGET_FILE:${module_name}>"
					CONFIGURATIONS Debug
					DESTINATION "${base_dir}/debug/${module_dir}"
					RENAME "_test")
				install(PROGRAMS "$<TARGET_FILE:${module_name}>"
					CONFIGURATIONS Release
					DESTINATION "${base_dir}/release/${module_dir}"
					RENAME "_test")
				set(test_executable "./_test")
			endif()

			if(${APPVEYOR_MODE})
				set(appveyor_runtest_pl_switch -a)
			else()
				set(appveyor_runtest_pl_switch)
			endif()

			target_compile_definitions(${module_name} PRIVATE
				-DTEST_EXECUTABLE="${test_executable}")
			add_test(NAME ${test_name}
				COMMAND ${PERL_EXECUTABLE} ${base_dir}/runtest.pl
				${appveyor_runtest_pl_switch} -c ${test_name}
				$<CONFIG> "$<TARGET_FILE:${module_name}>" "${test_executable}"
				WORKING_DIRECTORY ${base_dir})

			if(${APPVEYOR_MODE})
				execute_process(COMMAND appveyor AddTest -Name ${test_name}
					-Framework Custom -FileName "")
			endif()

			# It helps with debugging if the test depends on another step which
			# prepares the target directory, and is always out of date.
			add_custom_target(${module_name}-prepare
				COMMAND ${PERL_EXECUTABLE} ${base_dir}/runtest.pl
				-n -c ${test_name}
				$<CONFIG> "$<TARGET_FILE:${module_name}>" "${test_executable}"
				WORKING_DIRECTORY ${base_dir})
		elseif(module_name MATCHES "^(lib_.*|qdbm)$")
			if(DEBUG)
				message(STATUS "add library '${module_name}': '${module_files}'")
			endif()
			add_library(${module_name} STATIC ${module_files})
		else()
			message(FATAL_ERROR "Unsupported module type: " ${module_name})
		endif()

		target_compile_definitions(${module_name} PRIVATE -DBOX_MODULE="${module_name}")

		if(dependencies)
			string(REGEX REPLACE "[ 	]+" ";" dependency_list "${dependencies}")

			foreach(dependency ${dependency_list})
				if(DEBUG)
					message(STATUS "add dependency to '${module_name}': '${dependency}'")
				endif()
				add_dependencies(${module_name} ${dependency})
				if(dependency MATCHES "^(lib_.*|qdbm)$")
					# message(STATUS "add link library to '${module_name}': '${dependency}'")
					target_link_libraries(${module_name} PUBLIC ${dependency})
				endif()

				# We can't make a binary depend on another binary, so we need to
				# add the dependency's directory directly to our include path.
				if(dependency MATCHES "^bin_")
					get_property(dep_include_dirs
						TARGET ${dependency}
						PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
					target_include_directories(${module_name}
						PUBLIC ${dep_include_dirs})
				endif()
			endforeach()
		endif()

		target_include_directories(${module_name} PUBLIC ${module_path})
	endif()
endforeach()

# We can't do anything conditional on CMAKE_BUILD_TYPE because that's not valid for multi-configuration
# generators such as MSVC. We need to use a generator expression instead.
target_compile_definitions(lib_common PUBLIC $<$<CONFIG:Release>:BOX_RELEASE_BUILD>)

# Detect platform features and write BoxConfig.h.in. Reuse code from
# infrastructure/m4/boxbackup_tests.m4 where possible

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckCXXSourceCompiles)

set(boxconfig_h_file "${CMAKE_BINARY_DIR}/BoxConfig.h.in")
file(REMOVE "${boxconfig_h_file}")
file(WRITE "${boxconfig_h_file}" "// Auto-generated by CMake. Do not edit.\n")

if(WIN32)
	target_link_libraries(lib_common PUBLIC ws2_32 gdi32)
endif()

# Link to ZLib
# http://stackoverflow.com/a/6174604/648162
include_directories(${base_dir}/../zlib-win32/include)
# On Windows we want to statically link zlib to make debugging and distribution easier,
# but FindZLIB.cmake doesn't offer that as an option, so we have to go through some
# contortions to "find" the correct library. ZLIB_ROOT is required in this case.
if(WIN32)
	if(NOT DEFINED ZLIB_ROOT)
		message(FATAL_ERROR "You must set ZLIB_ROOT to point to include/zlib.h and lib/zlibstatic[d].lib on Windows")
	endif()

	message(STATUS "Searching for Zlib in: ${ZLIB_ROOT}")
	find_path(ZLIB_INCLUDE_DIR zlib.h PATHS ${ZLIB_ROOT}/include NO_DEFAULT_PATH)
	include_directories(${ZLIB_INCLUDE_DIR})
	message(STATUS "Found Zlib headers: ${ZLIB_INCLUDE_DIR}")

	# We must link against zlibstaticD if this is a debug build, otherwise
	# we have a C runtime mismatch (/MD versus /MDd) and the application
	# crashes at runtime.
	find_library(ZLIB_LIBRARY_STATIC_DEBUG NAMES zlibstaticd
		PATHS ${ZLIB_ROOT}/lib NO_DEFAULT_PATH)
	find_library(ZLIB_LIBRARY_STATIC_RELEASE NAMES zlibstatic
		PATHS ${ZLIB_ROOT}/lib NO_DEFAULT_PATH)

	target_link_libraries(lib_compress PUBLIC
		debug ${ZLIB_LIBRARY_STATIC_DEBUG}
		optimized ${ZLIB_LIBRARY_STATIC_RELEASE})
else()
	find_package(ZLIB REQUIRED)
	include_directories(${ZLIB_INCLUDE_DIRS})
	target_link_libraries(lib_compress PUBLIC ${ZLIB_LIBRARIES})
endif()

# Link to OpenSSL
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
target_link_libraries(lib_crypto PUBLIC ${OPENSSL_LIBRARIES})

# Link to PCRE
if (WIN32)
	if(NOT DEFINED PCRE_ROOT)
		message(FATAL_ERROR "You must set PCRE_ROOT to point to include/pcreposix.h and lib/pcreposix[d].lib on Windows")
	endif()

	target_compile_definitions(lib_common PUBLIC -DPCRE_STATIC)
	find_library(pcreposix_lib_path  pcreposix  ${PCRE_ROOT}/lib)
	find_library(pcreposixd_lib_path pcreposixd ${PCRE_ROOT}/lib)
	find_library(pcre_lib_path  pcre  ${PCRE_ROOT}/lib)
	find_library(pcred_lib_path pcred ${PCRE_ROOT}/lib)
	target_link_libraries(lib_common PUBLIC debug "${pcreposixd_lib_path}" optimized "${pcreposix_lib_path}")
	target_link_libraries(lib_common PUBLIC debug "${pcred_lib_path}"      optimized "${pcre_lib_path}")
	include_directories(${PCRE_ROOT}/include)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(PCRE REQUIRED libpcreposix)
	include_directories(${PCRE_INCLUDE_DIRS})
	target_link_libraries(lib_common PUBLIC ${PCRE_LIBRARIES})

	if(DEBUG)
		message(STATUS "Linking PCRE libraries from ${PCRE_LIBRARY_DIRS}: ${PCRE_LIBRARIES}")
	endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")
find_package(Readline)
if(READLINE_FOUND)
	include_directories(${Readline_INCLUDE_DIR})
	target_link_libraries(lib_common PUBLIC ${Readline_LIBRARY})
endif()

set(boxconfig_cmake_h_dir "${base_dir}/lib/common")
# Get the values of all directories added to the INCLUDE_DIRECTORIES property
# by include_directory() statements, and save it in CMAKE_REQUIRED_INCLUDES
# which check_include_files() uses to set the include file search path:
get_property(CMAKE_REQUIRED_INCLUDES DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
list(APPEND CMAKE_REQUIRED_INCLUDES "${boxconfig_cmake_h_dir}")
# message(STATUS "CMAKE_REQUIRED_INCLUDES=${CMAKE_REQUIRED_INCLUDES}")

# Save the original BoxConfig.cmake.h so that we can move it back later,
# and not need to recompile everything.
move_file_if_exists(
	"${boxconfig_cmake_h_dir}/BoxConfig.cmake.h"
	"${boxconfig_cmake_h_dir}/BoxConfig.cmake.h.bak")

foreach(m4_filename boxbackup_tests.m4 ax_check_mount_point.m4 ax_func_syscall.m4)
	file(STRINGS "${base_dir}/infrastructure/m4/${m4_filename}" m4_functions REGEX "^ *AC[_A-Z]+\\(.*\\)$")
	foreach(m4_function ${m4_functions})
		if(DEBUG)
			message(STATUS "Processing m4_function: ${m4_function}")
		endif()

		string(REGEX MATCH .* ac_check_headers ${m4_function})
		if(m4_function MATCHES "^ *AC_CHECK_HEADERS?\\(\\[([a-z./ ]+)\\](.*)\\)$")
			if(DEBUG)
				message(STATUS "Processing ac_check_headers: ${CMAKE_MATCH_1}")
			endif()

			# http://stackoverflow.com/questions/5272781/what-is-common-way-to-split-string-into-list-with-cmake
			string(REPLACE " " ";" header_files ${CMAKE_MATCH_1})

			foreach(header_file ${header_files})
				list(APPEND detect_header_files ${header_file})
			endforeach()
		elseif(m4_function MATCHES "^ *AC_CHECK_FUNCS\\(\\[([a-z./_ ]+)\\](.*)\\)$")
			if(DEBUG)
				message(STATUS "Processing ac_check_funcs: ${CMAKE_MATCH_1}")
			endif()

			# http://stackoverflow.com/questions/5272781/what-is-common-way-to-split-string-into-list-with-cmake
			string(REPLACE " " ";" function_names ${CMAKE_MATCH_1})

			foreach(function_name ${function_names})
				list(APPEND detect_functions ${function_name})
			endforeach()
		elseif(m4_function MATCHES "^ *AC_CHECK_DECLS\\(\\[([A-Za-z._/ ]+)\\](,,, ..#include <([^>]+)>..)?\\)$")
			if(DEBUG)
				message(STATUS "Processing ac_check_decls: ${CMAKE_MATCH_1} in ${CMAKE_MATCH_3}")
			endif()

			# http://stackoverflow.com/questions/5272781/what-is-common-way-to-split-string-into-list-with-cmake
			string(REPLACE " " ";" decl_names "${CMAKE_MATCH_1}")
			string(REPLACE " " ";" header_files "${CMAKE_MATCH_3}")

			foreach(decl_name ${decl_names})
				string(TOUPPER ${decl_name} platform_var_name)
				string(REGEX REPLACE "[/.]" "_" platform_var_name ${platform_var_name})
				check_symbol_exists("${decl_name}" "${header_files}" HAVE_DECL_${platform_var_name})
				file(APPEND "${boxconfig_h_file}" "#cmakedefine01 HAVE_DECL_${platform_var_name}\n")
			endforeach()
		elseif(m4_function MATCHES "^ *AC_SEARCH_LIBS\\(\\[([A-Za-z._/ ]+)\\], \\[([A-Za-z._]+)\\]\\)$")
			if(DEBUG)
				message(STATUS "Processing ac_search_libs: ${CMAKE_MATCH_1} in ${CMAKE_MATCH_2}")
			endif()

			set(function_name ${CMAKE_MATCH_1})
			# http://stackoverflow.com/questions/5272781/what-is-common-way-to-split-string-into-list-with-cmake
			string(REPLACE " " ";" library_names "${CMAKE_MATCH_2}")

			foreach(library_name ${library_names})
				string(TOUPPER ${library_name} platform_var_name)
				check_library_exists(${library_name} ${function_name} "" HAVE_LIB_${platform_var_name})
				if(HAVE_LIB_${platform_var_name})
					target_link_libraries(lib_common PUBLIC ${library_name})
				endif()
			endforeach()
		elseif(m4_function MATCHES "^ *AC_CHECK_MEMBERS\\(\\[([A-Za-z._/ ]+)\\.([[A-Za-z_]+)\\](,,, ..(#include <([^>]+)>)..)?\\)$")
			if(DEBUG)
				message(STATUS "Processing ac_check_members: ${CMAKE_MATCH_1}.${CMAKE_MATCH_2} in ${CMAKE_MATCH_5}")
			endif()

			set(struct_name "${CMAKE_MATCH_1}")
			set(member_name "${CMAKE_MATCH_2}")
			set(include_file "${CMAKE_MATCH_5}")

			string(TOUPPER "${struct_name}_${member_name}" platform_var_name)
			string(REGEX REPLACE "[/. ]" "_" platform_var_name ${platform_var_name})

			CHECK_CXX_SOURCE_COMPILES([=[
				#include "BoxConfig.cmake.h"
				#include <${include_file}>
				int main()
				{
					${struct_name} foo;
					return sizeof(foo.${member_name}) > 0 ? 0 : 1;
				}
				]=] "HAVE_${platform_var_name}")
			file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_${platform_var_name}\n")
		endif()
	endforeach()

	# Build an intermediate version of BoxConfig.cmake.h for use in the following tests.
	configure_file("${boxconfig_h_file}" "${boxconfig_cmake_h_dir}/BoxConfig.cmake.h")
endforeach()

list(APPEND detect_header_files mntent.h sys/mnttab.h sys/mount.h sys/param.h)

foreach(header_file ${detect_header_files})
	list(APPEND detect_header_files ${header_file})
	string(TOUPPER ${header_file} platform_var_name)
	string(REGEX REPLACE "[/.]" "_" platform_var_name ${platform_var_name})
	check_include_files(${header_file} HAVE_${platform_var_name})
	file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_${platform_var_name}\n")
endforeach()

if(NOT HAVE_PCREPOSIX_H)
	message(FATAL_ERROR "pcreposix.h not found at PCRE_ROOT/include: ${PCRE_ROOT}/include")
endif()

# PCRE is required, so unconditionally define this:
set(HAVE_REGEX_SUPPORT 1)
file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_REGEX_SUPPORT\n")

foreach(function_name ${detect_functions})
	string(TOUPPER ${function_name} platform_var_name)
	string(REGEX REPLACE "[/.]" "_" platform_var_name ${platform_var_name})
	check_function_exists(${function_name} HAVE_${platform_var_name})
	file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_${platform_var_name}\n")
endforeach()

check_symbol_exists(dirfd "dirent.h" HAVE_DECL_DIRFD)
file(APPEND "${boxconfig_h_file}" "#cmakedefine01 HAVE_DECL_DIRFD\n")

# Emulate ax_check_mount_point.m4
# These checks are run by multi-line M4 commands which are harder to parse/fake using
# regexps above, so we hard-code them here:
CHECK_CXX_SOURCE_COMPILES([=[
	#include "BoxConfig.cmake.h"
	#ifdef HAVE_SYS_PARAM_H
	#	include <sys/param.h>
	#endif
	#include <sys/mount.h>
	int main()
	{
		struct statfs foo;
		return sizeof(foo.f_mntonname) > 0 ? 0 : 1;
	}
	]=] "HAVE_STRUCT_STATFS_F_MNTONNAME")
file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_STRUCT_STATFS_F_MNTONNAME\n")
CHECK_CXX_SOURCE_COMPILES([=[
	#include "BoxConfig.cmake.h"
	#ifdef HAVE_SYS_PARAM_H
	#	include <sys/param.h>
	#endif
	#include <sys/mount.h>
	int main()
	{
		struct statvfs foo;
		return sizeof(foo.f_mntonname) > 0 ? 0 : 1;
	}
	]=] "HAVE_STRUCT_STATVFS_F_MNTONNAME")
file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_STRUCT_STATVFS_F_MNTONNAME\n")
if(HAVE_STRUCT_STATFS_F_MNTONNAME OR
	HAVE_STRUCT_STATVFS_F_MNTONNAME OR
	HAVE_STRUCT_MNTENT_MNT_DIR OR
	HAVE_STRUCT_MNTTAB_MNT_MOUNTP)

	set(HAVE_MOUNTS 1)
	file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_MOUNTS\n")
endif()

# Emulate ax_random_device.m4
if(EXISTS /dev/urandom)
	set(RANDOM_DEVICE /dev/urandom)
elseif(EXISTS /dev/arandom)
	set(RANDOM_DEVICE /dev/arandom)
elseif(EXISTS /dev/random)
	set(RANDOM_DEVICE /dev/random)
endif()
if(RANDOM_DEVICE)
	set(HAVE_RANDOM_DEVICE TRUE)
endif()
file(APPEND "${boxconfig_h_file}" "#cmakedefine RANDOM_DEVICE \"${RANDOM_DEVICE}\"\n")
file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_RANDOM_DEVICE\n")

# Build an intermediate version of BoxConfig.cmake.h for use in the following tests:
configure_file("${boxconfig_h_file}" "${boxconfig_cmake_h_dir}/BoxConfig.cmake.h")

foreach(struct_member_name "struct ucred.uid" "struct ucred.cr_uid")
	string(REGEX MATCH "(.*)\\.(.*)" dummy_var ${struct_member_name})
	set(struct_name "${CMAKE_MATCH_1}")
	set(member_name "${CMAKE_MATCH_2}")
	
	string(TOUPPER "${struct_name}_${member_name}" platform_var_name)
	string(REGEX REPLACE "[/. ]" "_" platform_var_name ${platform_var_name})

	CHECK_CXX_SOURCE_COMPILES([=[
		#include "BoxConfig.cmake.h"

		#ifdef HAVE_UCRED_H
		#	include <ucred.h>
		#endif

		#ifdef HAVE_SYS_PARAM_H
		#	include <sys/param.h>
		#endif

		#ifdef HAVE_SYS_UCRED_H
		#	include <sys/ucred.h>
		#endif

		#ifdef HAVE_SYS_SOCKET_H
		#	include <sys/socket.h>
		#endif

		int main()
		{
			${struct_name} foo;
			return sizeof(foo.${member_name}) > 0 ? 0 : 1;
		}
		]=] "HAVE_${platform_var_name}")
	file(APPEND "${boxconfig_h_file}" "#cmakedefine HAVE_${platform_var_name}\n")
endforeach()
set(CMAKE_REQUIRED_INCLUDES "")

# Build the final version of BoxConfig.cmake.h, as a temporary file.
configure_file("${boxconfig_h_file}" "${boxconfig_cmake_h_dir}/BoxConfig.cmake.h.new")

# Move the original back into place, and then replace it with the
# temporary one if different (which will force a rebuild of everything).
move_file_if_exists(
	"${boxconfig_cmake_h_dir}/BoxConfig.cmake.h.bak"
	"${boxconfig_cmake_h_dir}/BoxConfig.cmake.h")
replace_file_if_different(
	"${boxconfig_cmake_h_dir}/BoxConfig.cmake.h"
	"${boxconfig_cmake_h_dir}/BoxConfig.cmake.h.new")

# Tell QDBM not to build itself as a DLL, because we want to link statically to it.
target_compile_definitions(qdbm PUBLIC -DQDBM_STATIC)

# Silence some less-useful warnings
if(MSVC)
	add_definitions(/wd4996 /wd4291)
	set_property(TARGET qdbm PROPERTY CMAKE_STATIC_LINKER_FLAGS /IGNORE:LNK4006)
endif(MSVC)

# Define the location of the Perl executable, needed by testbackupstorefix
file(TO_NATIVE_PATH "${PERL_EXECUTABLE}" perl_executable_native)
string(REPLACE "\\" "\\\\" perl_path_escaped ${perl_executable_native})
target_compile_definitions(test_backupstorefix PRIVATE -DPERL_EXECUTABLE="${perl_path_escaped}")

# Configure test timeouts:
# I've set the timeout to 4 times as long as it took to run on a particular run on Appveyor:
# https://ci.appveyor.com/project/qris/boxbackup/build/job/xm10itascygtu93j
set_tests_properties(common PROPERTIES TIMEOUT 20)
set_tests_properties(crypto PROPERTIES TIMEOUT 4)
set_tests_properties(compress PROPERTIES TIMEOUT 80)
set_tests_properties(raidfile PROPERTIES TIMEOUT 32)
set_tests_properties(basicserver PROPERTIES TIMEOUT 80)
set_tests_properties(backupstore PROPERTIES TIMEOUT 560)
set_tests_properties(backupstorefix PROPERTIES TIMEOUT 300)
set_tests_properties(backupstorepatch PROPERTIES TIMEOUT 320)
set_tests_properties(backupdiff PROPERTIES TIMEOUT 32)
set_tests_properties(bbackupd PROPERTIES TIMEOUT 1200)
set_tests_properties(s3store PROPERTIES TIMEOUT 20)
set_tests_properties(httpserver PROPERTIES TIMEOUT 40)
