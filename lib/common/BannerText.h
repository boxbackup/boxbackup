// --------------------------------------------------------------------------
//
// File
//		Name:    BannerText.h
//		Purpose: Banner text for daemons and utilities
//		Created: 1/1/04
//
// --------------------------------------------------------------------------

#ifndef BANNERTEXT__H
#define BANNERTEXT__H

#ifdef NEED_BOX_VERSION_H
#	include "BoxVersion.h"
#endif

// Already included by BoxPlatform.h:
#include <stdint.h>

// Yes, you need two function macros to stringify an expanded macro value.
// https://stackoverflow.com/questions/5459868/concatenate-int-to-string-using-c-preprocessor
#define BOX_STRINGIFY_HELPER(x) #x
#define BOX_STRINGIFY(x) BOX_STRINGIFY_HELPER(x)

// How to identify a 64-bit build: https://stackoverflow.com/a/687902/648162
#if UINTPTR_MAX == (4294967295U)
#	define BOX_BUILD_BITS 32
#elif UINTPTR_MAX == (18446744073709551615UL)
#	define BOX_BUILD_BITS 64
#else
#	pragma message ("UINTPTR_MAX = " BOX_STRINGIFY(UINTPTR_MAX))
#	error Unknown architecture pointer size
#endif

#ifdef BOX_RELEASE_BUILD
#	define BOX_BUILD_TYPE Release
#else
#	define BOX_BUILD_TYPE Debug
#endif

#define STRINGIFY1(x) #x
#define STRINGIFY2(x) STRINGIFY1(x)
#ifdef _MSC_VER
#	define BOX_COMPILER "MSVC " STRINGIFY2(_MSC_VER)
#elif defined __GNUC__
#	define BOX_COMPILER "GCC " __VERSION__
#elif defined __VERSION__
// It might be an integer, not a string!
#	define BOX_COMPILER "Unknown " BOX_STRINGIFY(__VERSION__)
#else
#	define BOX_COMPILER "Unknown"
#endif

#ifdef ENABLE_VSS
#	define VSS_TEXT " (VSS)"
#else
#	define VSS_TEXT ""
#endif

#define BOX_BUILD_SIGNATURE BOX_COMPILER " " BOX_STRINGIFY(BOX_BUILD_BITS) "bit " BOX_STRINGIFY(BOX_BUILD_TYPE) VSS_TEXT

#define BANNER_TEXT(UtilityName) \
	"Box Backup " UtilityName " v" BOX_VERSION "\n" \
	"(c) Ben Summers and contributors 2003-2020. Build type: " BOX_BUILD_SIGNATURE

#endif // BANNERTEXT__H

