// --------------------------------------------------------------------------
//
// File
//		Name:    InvisibleTempFileStream.cpp
//		Purpose: IOStream interface to temporary files that
//			delete themselves
//		Created: 2006/10/13
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "InvisibleTempFileStream.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    InvisibleTempFileStream::InvisibleTempFileStream
//			(const char *, int, int)
//		Purpose: Constructor, opens invisible file
//		Created: 2006/10/13
//
// --------------------------------------------------------------------------
InvisibleTempFileStream::InvisibleTempFileStream(const std::string& Filename,
	int flags, int mode)
#ifdef WIN32
	: FileStream(Filename, flags | O_TEMPORARY, mode)
#else
	: FileStream(Filename, flags, mode)
#endif
{
	#ifndef WIN32
	if(EMU_UNLINK(Filename.c_str()) != 0)
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
	#endif
}
