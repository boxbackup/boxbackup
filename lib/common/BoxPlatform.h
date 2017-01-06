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

#ifdef WIN32
#define DIRECTORY_SEPARATOR			"\\"
#define DIRECTORY_SEPARATOR_ASCHAR		'\\'
#else
#define DIRECTORY_SEPARATOR			"/"
#define DIRECTORY_SEPARATOR_ASCHAR		'/'
#endif

#define PLATFORM_DEV_NULL			"/dev/null"

#if defined BOX_CMAKE
#	include "BoxConfig.cmake.h"
#elif defined _MSC_VER
#	include "BoxConfig-MSVC.h"
#	define NEED_BOX_VERSION_H
#else
#	include "BoxConfig.h"
#endif

#ifdef WIN32
	#ifdef __MSVCRT_VERSION__
		#if __MSVCRT_VERSION__ < 0x0601
			#error Must include Box.h before sys/types.h
		#endif
	#else
		// need msvcrt version 6.1 or higher for _gmtime64()
		// must define this before importing <sys/types.h>
		#define __MSVCRT_VERSION__ 0x0601
	#endif
#endif

#include "emu.h"

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

// Slight hack; disable interception in raidfile test on Darwin and Windows
#if defined WIN32
	// TODO: Replace with autoconf test
	#define PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
#endif

// Darwin also has a weird idea of permissions and dates on symlinks:
// perms are fixed at creation time by your umask, and dates can't be
// changed. This breaks unit tests if we try to compare these things.
// See: http://lists.apple.com/archives/darwin-kernel/2006/Dec/msg00057.html
#ifdef __APPLE__
	#define PLATFORM_DISABLE_SYMLINK_ATTRIB_COMPARE
#endif

// Find out if credentials on UNIX sockets can be obtained
#ifdef HAVE_GETPEEREID
	//
#elif HAVE_DECL_SO_PEERCRED
	//
#elif defined HAVE_UCRED_H && HAVE_GETPEERUCRED
	//
#else
	#define PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
#endif

#ifdef HAVE_DEFINE_PRAGMA
	// set packing to one bytes (can't use push/pop on gcc)
	#define BEGIN_STRUCTURE_PACKING_FOR_WIRE	#pragma pack(1)

	// Use default packing
	#define END_STRUCTURE_PACKING_FOR_WIRE		#pragma pack()
#else
	#define STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#endif

// Handle differing xattr APIs
#if !defined(HAVE_LLISTXATTR) && defined(HAVE_LISTXATTR) && HAVE_DECL_XATTR_NOFOLLOW
	#define llistxattr(a,b,c) listxattr(a,b,c,XATTR_NOFOLLOW)
	#define HAVE_LLISTXATTR
#endif

#if !defined(HAVE_LGETXATTR) && defined(HAVE_GETXATTR) && HAVE_DECL_XATTR_NOFOLLOW
	#define lgetxattr(a,b,c,d) getxattr(a,b,c,d,0,XATTR_NOFOLLOW)
	#define HAVE_LGETXATTR
#endif

#if !defined(HAVE_LSETXATTR) && defined(HAVE_SETXATTR) && HAVE_DECL_XATTR_NOFOLLOW
	#define lsetxattr(a,b,c,d,e) setxattr(a,b,c,d,0,(e)|XATTR_NOFOLLOW)
	#define HAVE_LSETXATTR
#endif

#if !HAVE_DECL_INFTIM
	#define INFTIM -1
#endif

// Define O_BINARY for Unix compatibility with Windows :-)
// MSVC 2010 and newer MinGW define this in fcntl.h, which is probably
// not included by this point, so include it now so that we can detect
// if we need O_BINARY

#ifdef HAVE_FCNTL_H
#	include <fcntl.h>
#endif

#ifndef O_BINARY
	#define O_BINARY 0
#endif

#ifdef WIN32
	typedef uint64_t InodeRefType;
#else
	typedef ino_t InodeRefType;
#endif

#ifdef WIN32
	#define WIN32_LEAN_AND_MEAN
#endif

#ifdef WIN32
	#define INVALID_FILE INVALID_HANDLE_VALUE
	typedef HANDLE tOSFileHandle;
#else
	#define INVALID_FILE -1
	typedef int tOSFileHandle;
#endif

// Solaris has no dirfd(x) macro or function, and we need one for
// intercept tests. We cannot define macros with arguments directly 
// using AC_DEFINE, so do it here instead of in configure.ac.

#if ! defined PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE && ! HAVE_DECL_DIRFD
	#ifdef HAVE_DIR_D_FD
		#define dirfd(x) (x)->d_fd
	#elif defined HAVE_DIR_DD_FD
		#define dirfd(x) (x)->dd_fd
	#else
		#error No way to get file descriptor from DIR structure
	#endif
#endif

#endif // BOXPLATFORM__H
