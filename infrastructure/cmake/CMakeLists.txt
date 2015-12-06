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
	bin/bbackupquery/makedocumentation.pl
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

file(STRINGS ${base_dir}/modules.txt module_deps REGEX "^[^#]")
# qdbm, lib/common and lib/win32 aren't listed in modules.txt, so hard-code them.
foreach(module_dep "qdbm" "lib/win32" "lib/common lib/win32" ${module_deps})
	string(REGEX MATCH "([^	 ]+)[	 ]*(.*)" valid_module_line ${module_dep})
	if(valid_module_line)
		if(DEBUG)
			message(STATUS "found module: ${CMAKE_MATCH_1} -> ${CMAKE_MATCH_2}")
		endif()

		set(module_dir ${base_dir}/${CMAKE_MATCH_1})
		string(REPLACE "/" "_" module_name ${CMAKE_MATCH_1})
		string(REPLACE "/" "_" dependencies "${CMAKE_MATCH_2}")
		file(GLOB module_files ${module_dir}/*.c ${module_dir}/*.cpp)

		# everything except lib/common and lib/win32 implicitly depend on
		# lib/common, so express that dependency here.
		if(module_name MATCHES "^lib_(common|win32)$")
		else()
			set(dependencies "${dependencies} lib_common")
		endif()
		string(REGEX REPLACE "^ " "" dependencies "${dependencies}")
		string(REGEX REPLACE " $" "" dependencies "${dependencies}")

		if(module_name MATCHES "^(bin|test)_")
			if(DEBUG)
				message(STATUS "add executable '${module_name}': '${module_files}'")
			endif()
			add_executable(${module_name} ${module_files})
		elseif(module_name MATCHES "^(lib_.*|qdbm)$")
			if(DEBUG)
				message(STATUS "add library '${module_name}': '${module_files}'")
			endif()
			add_library(${module_name} STATIC ${module_files})
		else()
			message(FATAL_ERROR "Unsupported module type: " ${module_dir})
		endif()

		if(dependencies MATCHES ".")
			if(DEBUG)
				message(STATUS "add dependencies to '${module_name}': '${dependencies}'")
			endif()
			string(REGEX REPLACE "[ 	]+" ";" dependency_list "${dependencies}")
			foreach(dependency ${dependency_list})
				add_dependencies(${module_name} ${dependency})
				if(dependency MATCHES "^lib_")
					target_link_libraries(${module_name} PUBLIC ${dependency})
				endif()
			endforeach()
		endif()

		target_include_directories(${module_name} PUBLIC ${module_dir})
	endif()
endforeach()

# Parsing Makefile.extra files in CMake script is a pain, so the relevant rules are
# hard-coded here.
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

foreach(exception_file ${exception_files})
	string(REGEX MATCH "(.*)/(.*)" valid_exception_file ${exception_file})
	if(NOT valid_exception_file)
		message(FATAL_ERROR "invalid exception file: '${exception_file}'")
	endif()

	execute_process(
		COMMAND ${PERL_EXECUTABLE} ${base_dir}/lib/common/makeexception.pl ${CMAKE_MATCH_2}
		WORKING_DIRECTORY "${base_dir}/${CMAKE_MATCH_1}"
		RESULT_VARIABLE status)
	if(NOT status EQUAL 0)
		message(FATAL_ERROR "Failed to execute: "
			"${PERL_EXECUTABLE} ${base_dir}/lib/common/makeexception.pl ${CMAKE_MATCH_2}"
			"(in ${CMAKE_MATCH_1}): ${status}")
	endif()
endforeach()

