// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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
	
	// types 'missing'
	typedef int socklen_t;
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

