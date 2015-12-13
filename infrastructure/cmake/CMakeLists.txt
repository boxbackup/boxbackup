cmake_minimum_required(VERSION 2.6)
project(BoxBackup)

set(base_dir ${CMAKE_SOURCE_DIR}/../..)

set(files_to_configure
	infrastructure/BoxPlatform.pm
	contrib/mac_osx/org.boxbackup.bbackupd.plist
	contrib/mac_osx/org.boxbackup.bbstored.plist
	contrib/solaris/bbackupd-manifest.xml
	contrib/solaris/bbstored-manifest.xml
	lib/common/BoxPortsAndFiles.h
	test/bbackupd/testfiles/bbackupd.conf
	test/bbackupd/testfiles/bbackupd-exclude.conf
	test/bbackupd/testfiles/bbackupd-snapshot.conf
	test/bbackupd/testfiles/bbackupd-symlink.conf
	test/bbackupd/testfiles/bbackupd-temploc.conf
	bin/bbackupd/bbackupd-config
	lib/bbackupquery/makedocumentation.pl
	bin/bbstored/bbstored-certs
	bin/bbstored/bbstored-config
	contrib/debian/bbackupd
	contrib/debian/bbstored
	contrib/redhat/bbackupd
	contrib/redhat/bbstored
	contrib/suse/bbackupd
	contrib/suse/bbstored
	contrib/solaris/bbackupd-smf-method
	contrib/solaris/bbstored-smf-method
	contrib/windows/installer/boxbackup.mpi
	infrastructure/makebuildenv.pl
	infrastructure/makeparcels.pl
	infrastructure/makedistribution.pl
	lib/common/makeexception.pl
	lib/raidfile/raidfile-config
	lib/server/makeprotocol.pl
	runtest.pl
	test/backupstorefix/testfiles/testbackupstorefix.pl
	test/bbackupd/testfiles/extcheck1.pl
	test/bbackupd/testfiles/extcheck2.pl
	test/bbackupd/testfiles/notifyscript.pl
	test/bbackupd/testfiles/syncallowscript.pl
)

foreach(file_to_configure ${files_to_configure})
	configure_file("${base_dir}/${file_to_configure}.in" "${base_dir}/${file_to_configure}" @ONLY)
endforeach()

file(READ "${base_dir}/infrastructure/buildenv-testmain-template.cpp" test_template)

include(FindPerl)

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
	lib/backupstore/backupprotocol.txt
	test/basicserver/testprotocol.txt
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
	lib/bbackupquery/documentation.txt
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
# qdbm, lib/common and lib/win32 aren't listed in modules.txt, so hard-code them.
foreach(module_dep
	"qdbm"
	"lib/win32"
	"lib/common qdbm lib/win32"
	${module_deps})

	string(REGEX MATCH "([^	 ]+)[	 ]*(.*)" valid_module_line ${module_dep})
	if(valid_module_line)
		if(DEBUG)
			message(STATUS "found module: ${CMAKE_MATCH_1} -> ${CMAKE_MATCH_2}")
		endif()

		set(module_dir ${CMAKE_MATCH_1})
		set(module_path ${base_dir}/${module_dir})
		string(REPLACE "/" "_" module_name ${CMAKE_MATCH_1})
		string(REPLACE "/" "_" dependencies "${CMAKE_MATCH_2}")
		file(GLOB module_files ${module_path}/*.c ${module_path}/*.cpp)
		set(module_files ${module_files} ${${module_name}_extra_files})

		# everything except qdbm, lib/common and lib/win32 implicitly depend on
		# lib/common, so express that dependency here.
		if(module_name MATCHES "^(qdbm|lib_(common|win32))$")
		else()
			set(dependencies "${dependencies} lib_common")
		endif()
		string(REGEX REPLACE "^ " "" dependencies "${dependencies}")
		string(REGEX REPLACE " $" "" dependencies "${dependencies}")

		if(module_name MATCHES "^bin_")
			if(DEBUG)
				message(STATUS "add executable '${module_name}': '${module_files}'")
			endif()
			add_executable(${module_name} ${module_files})
		elseif(module_name MATCHES "^test_")
			string(REGEX MATCH "^test_(.*)" valid_test ${module_name})
			set(test_name ${CMAKE_MATCH_1})

			if(DEBUG)
				message(STATUS "add test '${module_name}': '${module_files}'")
			endif()

			string(REPLACE "TEST_NAME" ${test_name} test_main "${test_template}")
			file(WRITE "${module_path}/_main.cpp" "${test_main}")
			add_executable(${module_name} ${module_files}
				"${module_path}/_main.cpp")
		elseif(module_name MATCHES "^(lib_.*|qdbm)$")
			if(DEBUG)
				message(STATUS "add library '${module_name}': '${module_files}'")
			endif()
			add_library(${module_name} STATIC ${module_files})
		else()
			message(FATAL_ERROR "Unsupported module type: " ${module_name})
		endif()

		target_compile_definitions(${module_name} PRIVATE -DBOX_MODULE="${module_dir}")

		if(dependencies)
			string(REGEX REPLACE "[ 	]+" ";" dependency_list "${dependencies}")

			foreach(dependency ${dependency_list})
				if(DEBUG)
					message(STATUS "add dependency to '${module_name}': '${dependency}'")
				endif()
				# add_dependencies(${module_name} ${dependency})
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

#include(ExternalProject)
#ExternalProject_Add(pcre
#	PREFIX "../pcre"
#	BUILD_COMMAND "${CMAKE_EXECUTABLE

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
include_directories(${base_dir}/../zlib-win32/include)
find_library(zlibstaticd_lib_path zlibstaticd ${base_dir}/../zlib-win32/lib)
target_link_libraries(lib_compress PUBLIC ${zlibstaticd_lib_path})

# Link to OpenSSL
include_directories(${base_dir}/../openssl-win32/include)
find_library(libeay32_lib_path libeay32 ${base_dir}/../openssl-win32/lib)
find_library(ssleay32_lib_path ssleay32 ${base_dir}/../openssl-win32/lib)
target_link_libraries(lib_crypto PUBLIC ${libeay32_lib_path} ${ssleay32_lib_path})

# Link to PCRE
include_directories(${base_dir}/../pcre-win32/include)
target_compile_definitions(lib_common PUBLIC -DPCRE_STATIC)
find_library(pcreposixd_lib_path pcreposixd ${base_dir}/../pcre-win32/lib)
find_library(pcred_lib_path pcred ${base_dir}/../pcre-win32/lib)
target_link_libraries(lib_common PUBLIC "${pcreposixd_lib_path}" "${pcred_lib_path}")

# Define the location of the Perl executable, needed by testbackupstorefix
target_compile_definitions(test_backupstorefix PRIVATE -DPERL_EXECUTABLE="${PERL_EXECUTABLE}")
