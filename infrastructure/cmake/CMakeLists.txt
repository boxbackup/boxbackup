cmake_minimum_required(VERSION 2.6)
project(BoxBackup)

set(base_dir ../..)

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
foreach(module_dep ${module_deps})
	string(REGEX MATCH "([^	 ]+)[	 ]*(.*)" valid_module_line ${module_dep})
	if(valid_module_line)
		if(DEBUG)
			message(STATUS "found module: ${CMAKE_MATCH_1} -> ${CMAKE_MATCH_2}")
		endif()

		set(module_dir ${base_dir}/${CMAKE_MATCH_1})
		string(REPLACE "/" "_" module_name ${CMAKE_MATCH_1})
		string(REPLACE "/" "_" dependencies "${CMAKE_MATCH_2}")
		string(REGEX REPLACE "^ " "" dependencies "${dependencies}")
		string(REGEX REPLACE " $" "" dependencies "${dependencies}")
		file(GLOB module_files ${module_dir}/*.c ${module_dir}/*.cpp)

		if(module_name MATCHES "^(bin|test)_")
			if(DEBUG)
				message(STATUS "add executable '${module_name}': '${module_files}'")
			endif()
			add_executable(${module_name} ${module_files})
		elseif(module_name MATCHES "^lib_")
			if(DEBUG)
				message(STATUS "add library '${module_name}': '${module_files}'")
			endif()
			add_library(${module_name} STATIC ${module_files})
		else()
			message(FATAL_ERROR "Unsupported module type: " ${module_dir})
		endif()

		if(DEBUG)
			message(STATUS "add dependencies to '${module_name}': '${dependencies}'")
		endif()
		target_link_libraries(${module_name} ${dependencies})
	endif()
endforeach()
