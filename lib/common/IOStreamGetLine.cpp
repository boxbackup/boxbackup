// --------------------------------------------------------------------------
//
// File
//		Name:    IOStreamGetLine.cpp
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "Exception.h"
#include "IOStreamGetLine.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::IOStreamGetLine(int)
//		Purpose: Constructor, taking file descriptor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
IOStreamGetLine::IOStreamGetLine(IOStream &Stream)
: mrStream(Stream)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::~IOStreamGetLine()
//		Purpose: Destructor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
IOStreamGetLine::~IOStreamGetLine()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::ReadMore()
//		Purpose: Read more bytes from the handle, possible the
//			 console, into mBuffer and return the number of
//			 bytes read, 0 on EOF or -1 on error.
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
int IOStreamGetLine::ReadMore(int Timeout)
{
	int bytes = mrStream.Read(mBuffer, sizeof(mBuffer), Timeout);
	
	if(!mrStream.StreamDataLeft())
	{
		mPendingEOF = true;
	}

	return bytes;	
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::DetachFile()
//		Purpose: Detaches the file handle, setting the file pointer correctly.
//				 Probably not good for sockets...
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
void IOStreamGetLine::DetachFile()
{
	// Adjust file pointer
	int bytesOver = mBytesInBuffer - mBufferBegin;
	ASSERT(bytesOver >= 0);
	if(bytesOver > 0)
	{
		mrStream.Seek(0 - bytesOver, IOStream::SeekType_Relative);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStreamGetLine::IgnoreBufferedData(int)
//		Purpose: Ignore buffered bytes (effectively removing them
//		         from the beginning of the buffered data.) Cannot
//		         remove more bytes than are currently in the buffer.
//		         Be careful when this is used!
//		Created: 22/12/04
//
// --------------------------------------------------------------------------
void IOStreamGetLine::IgnoreBufferedData(int BytesToIgnore)
{
	int bytesInBuffer = mBytesInBuffer - mBufferBegin;
	if(BytesToIgnore < 0 || BytesToIgnore > bytesInBuffer)
	{
		THROW_EXCEPTION(CommonException, IOStreamGetLineNotEnoughDataToIgnore)
	}
	mBufferBegin += BytesToIgnore;
}



