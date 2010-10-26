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

// utility whitespace function
inline bool iw(int c)
{
	return (c == ' ' || c == '\t' || c == '\v' || c == '\f'); // \r, \n are already excluded
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::FdGetLine(int)
//		Purpose: Constructor, taking file descriptor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
FdGetLine::FdGetLine(int fd)
	: mFileHandle(fd),
	  mLineNumber(0),
	  mBufferBegin(0),
	  mBytesInBuffer(0),
	  mPendingEOF(false),
	  mEOF(false)
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

	// EOF?
	if(mEOF) {THROW_EXCEPTION(CommonException, GetLineEOF)}
	
	std::string r;

	bool foundLineEnd = false;

	while(!foundLineEnd && !mEOF)
	{
		// Use any bytes left in the buffer
		while(mBufferBegin < mBytesInBuffer)
		{
			char c = mBuffer[mBufferBegin++];
			if(c == '\r')
			{
				// Ignore nasty Windows line ending extra chars
			}
			else if(c == '\n')
			{
				// Line end!
				foundLineEnd = true;
				break;
			}
			else
			{
				// Add to string
				r += c;
			}
			
			// Implicit line ending at EOF
			if(mBufferBegin >= mBytesInBuffer && mPendingEOF)
			{
				foundLineEnd = true;
			}
		}
		
		// Check size
		if(r.size() > FDGETLINE_MAX_LINE_SIZE)
		{
			THROW_EXCEPTION(CommonException, GetLineTooLarge)
		}
		
		// Read more in?
		if(!foundLineEnd && mBufferBegin >= mBytesInBuffer && !mPendingEOF)
		{
#ifdef WIN32
			int bytes;

			if (mFileHandle == _fileno(stdin))
			{
				bytes = console_read(mBuffer, sizeof(mBuffer));
			}
			else
			{
				bytes = ::read(mFileHandle, mBuffer, 
					sizeof(mBuffer));
			}
#else // !WIN32
			int bytes = ::read(mFileHandle, mBuffer, sizeof(mBuffer));
#endif // WIN32
			
			// Error?
			if(bytes == -1)
			{
				THROW_EXCEPTION(CommonException, OSFileError)
			}
			
			// Adjust buffer info
			mBytesInBuffer = bytes;
			mBufferBegin = 0;
			
			// EOF / closed?
			if(bytes == 0)
			{
				mPendingEOF = true;
			}
		}
		
		// EOF?
		if(mPendingEOF && mBufferBegin >= mBytesInBuffer)
		{
			// File is EOF, and now we've depleted the buffer completely, so tell caller as well.
			mEOF = true;
		}
	}

	if(!Preprocess)
	{
		return r;
	}
	// Skip Windows INI style headers
	// r might be empty so can't use r[0]
	else if(r.c_str()[0] == '[')
	{
		r.clear();
		return r;
	}
	else
	{
		// Remove whitespace
		size_t size = r.size();
		if(0 == size)
			return "";

		size_t begin = 0;
		while(begin < size && iw(r[begin]))
		{
			begin++;
		}
		
		// Check for comment char, but char before must be whitespace
		if(r[begin] == '#')
			return "";

		size_t end = begin;
		while(++end < size)
		{
			if(r[end] == '#' && iw(r[end-1]))
				break;
		}
		
		while(--end > begin && iw(r[end])) end--;
		
		// Return a sub string
		return r.substr(begin, end - begin + 1);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::DetachFile()
//		Purpose: Detaches the file handle, setting the file pointer correctly.
//				 Probably not good for sockets...
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


