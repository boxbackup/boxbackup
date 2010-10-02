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
	InvisibleTempFileStream(const char *Filename, 
#ifdef WIN32
		int flags = (O_RDONLY | O_BINARY),
#else
		int flags = O_RDONLY,
#endif
		int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH));

private:	
	InvisibleTempFileStream(const InvisibleTempFileStream &rToCopy) 
	: FileStream(INVALID_FILE)
	{ /* do not call */ }
};

#endif // INVISIBLETEMPFILESTREAM__H


