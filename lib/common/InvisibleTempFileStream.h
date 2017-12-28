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
		int mode = DEFAULT_MODE)
	: FileStream(filename, flags, mode, FileStream::SHARED, true) // delete_asap
	{ }

private:	
	InvisibleTempFileStream(const InvisibleTempFileStream &rToCopy) 
	: FileStream(INVALID_FILE)
	{ /* do not call */ }
};

#endif // INVISIBLETEMPFILESTREAM__H


