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

#include "config.h"

#ifdef HAVE_SYS_TYPES_H
	#include <sys/types.h>
#endif
#ifdef HAVE_INTTYPES_H
	#include <inttypes.h>
#else
	#ifdef HAVE_STDINT_H
		#include <stdint.h>
	#endif
#endif

// Find out if credentials on UNIX sockets can be obtained
#ifndef HAVE_GETPEEREID
	#if !HAVE_DECL_SO_PEERCRED
		#define PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
	#endif
#endif

// Cannot do the intercepts in test/raidfile if large file support is enabled
#ifdef HAVE_LARGE_FILE_SUPPORT
	#define PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
#endif

#ifdef HAVE_DEFINE_PRAGMA
	// set packing to one bytes (can't use push/pop on gcc)
	#define BEGIN_STRUCTURE_PACKING_FOR_WIRE	#pragma pack(1)

	// Use default packing
	#define END_STRUCTURE_PACKING_FOR_WIRE		#pragma pack()
#else
	#define STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#endif

// Define missing types
#ifndef HAVE_UINT8_T
	typedef u_int8_t uint8_t;
#endif

#ifndef HAVE_UINT16_T
	typedef u_int16_t uint16_t;
#endif

#ifndef HAVE_UINT32_T
	typedef u_int32_t uint32_t;
#endif

#ifndef HAVE_UINT64_T
	typedef u_int64_t uint64_t;
#endif

#ifndef HAVE_U_INT8_T
	typedef uint8_t u_int8_t;
#endif

#ifndef HAVE_U_INT16_T
	typedef uint16_t u_int16_t;
#endif

#ifndef HAVE_U_INT32_T
	typedef uint32_t u_int32_t;
#endif

#ifndef HAVE_U_INT64_T
	typedef uint64_t u_int64_t;
#endif

#if !HAVE_DECL_INFTIM
	#define INFTIM -1
#endif

#endif // BOXPLATFORM__H
