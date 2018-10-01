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

// How to identify a 64-bit build: https://stackoverflow.com/a/687902/648162
#if defined _WIN64
#	define BOX_BUILD_BITS 64
#elif defined _WIN32
#	define BOX_BUILD_BITS 32
#elif __SIZEOF_POINTER__ == 8
#	define BOX_BUILD_BITS 64
#elif __SIZEOF_POINTER__ == 4
#	define BOX_BUILD_BITS 32
#else
#	error Cannot determine 32/64-bitness
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
#	define BOX_COMPILER "Unknown " STRINGIFY2(__VERSION__)
#else
#	define BOX_COMPILER "Unknown"
#endif

#define BOX_BUILD_SIGNATURE STRINGIFY2(BOX_COMPILER " " STRINGIFY2(BOX_BUILD_BITS) "bit " BOX_BUILD_TYPE)

#define BANNER_TEXT(UtilityName) \
	"Box Backup " UtilityName " v" BOX_VERSION "\n" \
	"(c) Ben Summers and contributors 2003-2018"

#endif // BANNERTEXT__H

