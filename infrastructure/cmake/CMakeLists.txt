cmake_minimum_required(VERSION 2.6)
project(BoxBackup)
enable_testing()

set(base_dir ${CMAKE_SOURCE_DIR}/../..)

macro(cmake_to_native_path cmake_path native_path)
	if(WIN32)
		string(REPLACE "/" "\\" ${native_path} "${cmake_path}")
	else()
		set(${native_path} "${cmake_path}")
	endif()
endmacro()

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
	configure_file("${base_dir}/${file_to_configure}.in" "${base_dir}/${file_to_configure}" @ONLY)
endforeach()

file(READ "${base_dir}/infrastructure/buildenv-testmain-template.cpp" test_template)


execute_process(
	COMMAND ${PERL_EXECUTABLE} ${base_dir}/infrastructure/msvc/getversion.pl
	RESULT_VARIABLE status
	OUTPUT_VARIABLE command_output
	ERROR_VARIABLE command_output)
if(NOT status EQUAL 0)
	message(FATAL_ERROR "Failed to execute: "
		"${PERL_EXECUTABLE} ${base_dir}/infrastructure/msvc/getversion.pl: "
		"status ${status}: ${command_output}")
endif()

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
		file(GLOB module_files ${module_path}/*.c ${module_path}/*.cpp
			${module_path}/*.h)
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
target_compile_definitions(lib_common PUBLIC $<$<CONFIG:Release>:-DBOX_RELEASE_BUILD>)

# Tell QDBM not to build itself as a DLL, because we want to link statically to it.
target_compile_definitions(qdbm PUBLIC -DQDBM_STATIC)

# Silence some less-useful warnings
if(MSVC)
	add_definitions(/wd4996 /wd4291)
	# target_link_libraries(qdbm PRIVATE /IGNORE:LNK4006)
	set_property(TARGET qdbm PROPERTY CMAKE_STATIC_LINKER_FLAGS /IGNORE:LNK4006)
endif(MSVC)

target_link_libraries(lib_common PUBLIC ws2_32 gdi32)

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

# Define the location of the Perl executable, needed by testbackupstorefix
cmake_to_native_path("${PERL_EXECUTABLE}" perl_executable_native)
string(REPLACE "\\" "\\\\" perl_path_escaped ${perl_executable_native})
target_compile_definitions(test_backupstorefix PRIVATE -DPERL_EXECUTABLE="${perl_path_escaped}")
