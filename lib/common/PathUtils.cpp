// --------------------------------------------------------------------------
//
// File
//		Name:    PathUtils.cpp
//		Purpose: Platform-independent path manipulation
//		Created: 2007/01/17
//
// --------------------------------------------------------------------------

#include "Box.h"
#include <string>

// --------------------------------------------------------------------------
//
// Function
//		Name:    MakeFullPath(const std::string& rDir, const std::string& rFile)
//		Purpose: Combine directory and file name
//		Created: 2006/08/10
//
// --------------------------------------------------------------------------
std::string MakeFullPath(const std::string& rDir, const std::string& rEntry)
{
	std::string result(rDir);

	if (result.size() > 0 && 
		result[result.size()-1] != DIRECTORY_SEPARATOR_ASCHAR)
	{
		result += DIRECTORY_SEPARATOR;
	}

	result += rEntry;

	return result;
}
