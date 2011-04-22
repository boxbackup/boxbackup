// --------------------------------------------------------------------------
//
// File
//		Name:    FdGetLine.cpp
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "FdGetLine.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::FdGetLine(int)
//		Purpose: Constructor, taking file descriptor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
FdGetLine::FdGetLine(int fd)
: mFileHandle(fd)
{
	if(mFileHandle < 0) {THROW_EXCEPTION(CommonException, BadArguments)}
	//printf("FdGetLine buffer size = %d\n", sizeof(mBuffer));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::~FdGetLine()
//		Purpose: Destructor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
FdGetLine::~FdGetLine()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::GetLine(bool)
//		Purpose: Returns a file from the file. If Preprocess is true, leading
//				 and trailing whitespace is removed, and comments (after #)
//				 are deleted.
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
std::string FdGetLine::GetLine(bool Preprocess)
{
	if(mFileHandle == -1) {THROW_EXCEPTION(CommonException, GetLineNoHandle)}
	
	std::string r;
	bool result = GetLineInternal(r, Preprocess);

	if(!result)
	{
		// should never fail for FdGetLine
		THROW_EXCEPTION(CommonException, Internal);
	}
	
	return r;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::ReadMore()
//		Purpose: Read more bytes from the handle, possible the
//			 console, into mBuffer and return the number of
//			 bytes read, 0 on EOF or -1 on error.
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
int FdGetLine::ReadMore(int Timeout)
{
	int bytes;
	
#ifdef WIN32
	if (mFileHandle == _fileno(stdin))
	{
		bytes = console_read(mBuffer, sizeof(mBuffer));
	}
	else
	{
		bytes = ::read(mFileHandle, mBuffer, sizeof(mBuffer));
	}
#else // !WIN32
	bytes = ::read(mFileHandle, mBuffer, sizeof(mBuffer));
#endif // WIN32

	if(bytes == 0)
	{
		mPendingEOF = true;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::DetachFile()
//		Purpose: Detaches the file handle, setting the file pointer correctly.
//			 Probably not good for sockets...
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
void FdGetLine::DetachFile()
{
	if(mFileHandle == -1) {THROW_EXCEPTION(CommonException, GetLineNoHandle)}

	// Adjust file pointer
	int bytesOver = mBufferBegin - mBufferBegin;
	ASSERT(bytesOver >= 0);
	if(bytesOver > 0)
	{
		if(::lseek(mFileHandle, 0 - bytesOver, SEEK_CUR) == -1)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	}

	// Unset file pointer
	mFileHandle = -1;
}

