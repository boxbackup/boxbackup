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

#ifdef WIN32
	#include <windows.h>
#endif

// Prefix name with Box to avoid clashing with OS API names
std::string BoxGetTemporaryDirectoryName()
{
#ifdef WIN32
	// http://msdn.microsoft.com/library/default.asp?
	// url=/library/en-us/fileio/fs/creating_and_using_a_temporary_file.asp

	DWORD dwRetVal;
	char lpPathBuffer[1024];
	DWORD dwBufSize = sizeof(lpPathBuffer);
	
	// Get the temp path.
	dwRetVal = GetTempPath(dwBufSize,     // length of the buffer
						   lpPathBuffer); // buffer for path 
	if (dwRetVal > dwBufSize)
	{
		THROW_EXCEPTION(CommonException, TempDirPathTooLong)
	}
	
	return std::string(lpPathBuffer);
#elif defined TEMP_DIRECTORY_NAME
	return std::string(TEMP_DIRECTORY_NAME);
#else	
	#error non-static temporary directory names not supported yet
#endif
}

#endif // TEMPORARYDIRECTORY__H
