// --------------------------------------------------------------------------
//
// File
//		Name:    PathUtils.h
//		Purpose: Platform-independent path manipulation
//		Created: 2007/01/17
//
// --------------------------------------------------------------------------

#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <string>

// --------------------------------------------------------------------------
//
// Function
//		Name:    MakeFullPath(const std::string& rDir, const std::string& rFile)
//		Purpose: Combine directory and file name
//		Created: 2006/08/10
//
// --------------------------------------------------------------------------

std::string MakeFullPath(const std::string& rDir, const std::string& rEntry);

#endif // !PATHUTILS_H
