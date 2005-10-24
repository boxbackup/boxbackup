// --------------------------------------------------------------------------
//
// File
//		Name:    TemporaryDirectory.h
//		Purpose: Location of temporary directory
//		Created: 2003/10/13
//
// --------------------------------------------------------------------------

#ifndef TEMPORARYDIRECTORY__H
#define TEMPORARYDIRECTORY__H

#include <string>

#ifdef PLATFORM_STATIC_TEMP_DIRECTORY_NAME
	// Prefix name with Box to avoid clashing with OS API names
	inline std::string BoxGetTemporaryDirectoryName()
	{
		return std::string(PLATFORM_STATIC_TEMP_DIRECTORY_NAME);
	}
#else
	non-static temporary directory names not supported yet
#endif

#endif // TEMPORARYDIRECTORY__H

