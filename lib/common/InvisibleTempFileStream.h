// --------------------------------------------------------------------------
//
// File
//		Name:    InvisibleTempFileStream.h
//		Purpose: FileStream interface to temporary files that 
//			delete themselves
//		Created: 2006/10/13
//
// --------------------------------------------------------------------------

#ifndef INVISIBLETEMPFILESTREAM__H
#define INVISIBLETEMPFILESTREAM__H

#include <string>

#include "FileStream.h"

class InvisibleTempFileStream : public FileStream
{
public:
	InvisibleTempFileStream(const std::string& filename,
#ifdef WIN32
		int flags = (O_RDONLY | O_BINARY),
#else
		int flags = O_RDONLY,
#endif
		int mode = DEFAULT_MODE,
		// Default to no lock, to allow multiple opens for diffing:
		lock_mode_t lock_mode = FileStream::NONE)
	: FileStream(filename,
#ifdef WIN32
		// In order for another user to delete the file on closing, we must open it with
		// FILE_SHARE_DELETE share mode:
		flags | BOX_FILE_SHARE_DELETE,
#else
		flags,
#endif
		mode, lock_mode, true) // delete_asap
	{ }

private:	
	InvisibleTempFileStream(const InvisibleTempFileStream &rToCopy) 
	: FileStream(INVALID_FILE)
	{ /* do not call */ }
};

#endif // INVISIBLETEMPFILESTREAM__H


