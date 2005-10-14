// --------------------------------------------------------------------------
//
// File
//		Name:    BoxPlatform.h
//		Purpose: Specifies what each platform supports in more detail, and includes
//				 extra files to get basic support for types.
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------

#ifndef BOXPLATFORM__H
#define BOXPLATFORM__H

#define DIRECTORY_SEPARATOR			"/"
#define DIRECTORY_SEPARATOR_ASCHAR	'/'

#define PLATFORM_DEV_NULL			"/dev/null"


// Other flags which might be useful...
//
//	#define PLATFORM_BERKELEY_DB_NOT_SUPPORTED
//    -- dbopen etc not on this platform
//
//  #define PLATFORM_REGEX_NOT_SUPPORTED
//	  -- regex support not available on this platform


#ifdef PLATFORM_OPENBSD

	#include <sys/types.h>

	#define PLATFORM_HAVE_setproctitle

	#define PLATFORM_STATIC_TEMP_DIRECTORY_NAME	"/tmp"
	
	#define PLATFORM_HAVE_getpeereid

	#define PLATFORM_RANDOM_DEVICE	"/dev/arandom"

#endif // PLATFORM_OPENBSD

#ifdef PLATFORM_NETBSD

	#include <sys/types.h>

	#define PLATFORM_HAVE_setproctitle

	#define PLATFORM_NO_BUILT_IN_SWAP64

	#define PLATFORM_STATIC_TEMP_DIRECTORY_NAME	"/tmp"
	
	#define PLATFORM_KQUEUE_NOT_SUPPORTED

	#define PLATFORM_RANDOM_DEVICE	"/dev/urandom"

#endif

#ifdef PLATFORM_FREEBSD

	#include <sys/types.h>
	#include <netinet/in.h>

	#define PLATFORM_HAVE_setproctitle

	#define PLATFORM_NO_BUILT_IN_SWAP64

	#define PLATFORM_STATIC_TEMP_DIRECTORY_NAME	"/tmp"

	#define PLATFORM_HAVE_getpeereid

	#define PLATFORM_RANDOM_DEVICE  "/dev/urandom"

#endif // PLATFORM_FREEBSD

#ifdef PLATFORM_DARWIN

	#include <sys/types.h>
	#include <netdb.h>
	
	// types 'missing'
	#ifndef _SOCKLEN_T
		typedef int socklen_t;
	#endif
	typedef u_int8_t uint8_t;
	typedef signed char int8_t;
	typedef u_int64_t uint64_t;
	typedef u_int32_t uint32_t;
	typedef u_int16_t uint16_t;
	
	// poll() emulator on Darwin misses this declaration
	#define INFTIM		-1

	#define PLATFORM_STATIC_TEMP_DIRECTORY_NAME	"/tmp"
	
	#define PLATFORM_LCHOWN_NOT_SUPPORTED
	
	#define PLATFORM_READLINE_NOT_SUPPORTED

	#define PLATFORM_RANDOM_DEVICE	"/dev/random"
	
#endif // PLATFORM_DARWIN

#ifdef PLATFORM_LINUX
	
	#include <sys/types.h>

	// for ntohl etc...
	#include <netinet/in.h>

	// types 'missing'
	typedef u_int8_t uint8_t;
	typedef signed char int8_t;
	typedef u_int32_t uint32_t;
	typedef u_int16_t uint16_t;
	typedef u_int64_t uint64_t;

	// not defined in Linux, a BSD thing
	#define INFTIM -1

	#define LLONG_MAX    9223372036854775807LL
	#define LLONG_MIN    (-LLONG_MAX - 1LL)

	#define PLATFORM_STATIC_TEMP_DIRECTORY_NAME	"/tmp"

	#define PLATFORM_HAVE_getsockopt_SO_PEERCRED

	// load in installation specific linux configuration
	#include "../../local/_linux_platform.h"

	#define PLATFORM_KQUEUE_NOT_SUPPORTED
	#define PLATFORM_dirent_BROKEN_d_type
	#define PLATFORM_stat_SHORT_mtime
	#define PLATFORM_stat_NO_st_flags
	#define PLATFORM_USES_MTAB_FILE_FOR_MOUNTS
	#define PLATFORM_open_NO_O_EXLOCK
	#define PLATFORM_sockaddr_NO_len

	#define PLATFORM_RANDOM_DEVICE	"/dev/urandom"

	// If large file support is on, can't do the intercepts in the test/raidfile
	#if _FILE_OFFSET_BITS == 64
		#define PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	#endif

#endif // PLATFORM_LINUX

#ifdef PLATFORM_CYGWIN

	#define PLATFORM_BERKELEY_DB_NOT_SUPPORTED

	#define PLATFORM_KQUEUE_NOT_SUPPORTED
	#define PLATFORM_dirent_BROKEN_d_type
	#define PLATFORM_stat_SHORT_mtime
	#define PLATFORM_stat_NO_st_flags
	#define PLATFORM_USES_MTAB_FILE_FOR_MOUNTS
	#define PLATFORM_open_NO_O_EXLOCK
	#define PLATFORM_sockaddr_NO_len
	#define PLATFORM_NO_BUILT_IN_SWAP64

	#define PLATFORM_STATIC_TEMP_DIRECTORY_NAME	"/tmp"

	#define PLATFORM_READLINE_NOT_SUPPORTED

	#define LLONG_MAX    9223372036854775807LL
	#define LLONG_MIN    (-LLONG_MAX - 1LL)

	#define INFTIM -1

	// File listing canonical interesting mount points.  
	#define MNTTAB          _PATH_MNTTAB   

	// File listing currently active mount points.  
	#define MOUNTED         _PATH_MOUNTED   

	#define __need_FILE

	// Extra includes
	#include <stdint.h>
	#include <stdlib.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/stat.h>
	#include <sys/types.h>
	#include <dirent.h>
	#include <stdio.h>
	#include <paths.h>

	// No easy random entropy source
	#define PLATFORM_RANDOM_DEVICE_NONE

#endif // PLATFORM_CYGWIN


// Find out if credentials on UNIX sockets can be obtained
#ifndef PLATFORM_HAVE_getpeereid
	#ifndef PLATFORM_HAVE_getsockopt_SO_PEERCRED
		#define PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
	#endif
#endif


// Compiler issues
#ifdef __GNUC__

	#ifdef PLATFORM_GCC3

		// GCC v3 doesn't like pragmas in #defines
		#define STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
		
		// But fortunately, the STL allocations are much better behaved.
	
	#else
	
		// Force STL to use malloc() for memory allocation
		// -- slower, but doesn't gradually use more and more memory
		// HOWEVER -- this 'fix' is broken on some platforms. Lots of fun!
		#ifndef PLATFORM_STL_USE_MALLOC_BROKEN
			#define __USE_MALLOC
		#endif
		
		// set packing to one bytes (can't use push/pop on gcc)
		#define BEGIN_STRUCTURE_PACKING_FOR_WIRE	#pragma pack(1)
		
		// Use default packing
		#define END_STRUCTURE_PACKING_FOR_WIRE		#pragma pack()

	#endif

#else
	compiler not supported!
#endif


#endif // BOXPLATFORM__H

