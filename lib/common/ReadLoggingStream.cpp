// --------------------------------------------------------------------------
//
// File
//		Name:    ReadLoggingStream.cpp
//		Purpose: Buffering wrapper around IOStreams
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "ReadLoggingStream.h"
#include "CommonException.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::ReadLoggingStream(const char *, int, int)
//		Purpose: Constructor, set up buffer
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
ReadLoggingStream::ReadLoggingStream(IOStream& rSource)
: mrSource(rSource),
  mOffset(0),
  mLength(mrSource.BytesLeftToRead()),
  mTotalRead(0),
  mStartTime(GetCurrentBoxTime())
{ }


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::Read(void *, int)
//		Purpose: Reads bytes from the file
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
int ReadLoggingStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	int numBytesRead = mrSource.Read(pBuffer, NBytes, Timeout);

	if (numBytesRead > 0)
	{
		mTotalRead += numBytesRead;
		mOffset += numBytesRead;
	}

	if (mLength >= 0 && mTotalRead > 0)
	{	
		box_time_t timeNow = GetCurrentBoxTime();
		box_time_t elapsed = timeNow - mStartTime;
		box_time_t finish  = (elapsed * mLength) / mTotalRead;
		box_time_t remain  = finish - elapsed;

		BOX_TRACE("Read " << numBytesRead << " bytes at " << mOffset << 
			", " << (mLength - mOffset) << " remain, eta " <<
			BoxTimeToSeconds(remain) << "s");
	}
	else if (mLength >= 0 && mTotalRead == 0)
	{
		BOX_TRACE("Read " << numBytesRead << " bytes at " << mOffset << 
			", " << (mLength - mOffset) << " remain");
	}
	else
	{	
		BOX_TRACE("Read " << numBytesRead << " bytes at " << mOffset << 
			", unknown bytes remaining");
	}
	
	return numBytesRead;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::BytesLeftToRead()
//		Purpose: Returns number of bytes to read (may not be most efficient function ever)
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
IOStream::pos_type ReadLoggingStream::BytesLeftToRead()
{
	return mLength - mOffset;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::Write(void *, int)
//		Purpose: Writes bytes to the underlying stream (not supported)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void ReadLoggingStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::GetPosition()
//		Purpose: Get position in stream
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
IOStream::pos_type ReadLoggingStream::GetPosition() const
{
	return mOffset;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::Seek(pos_type, int)
//		Purpose: Seeks within file, as lseek, invalidate buffer
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void ReadLoggingStream::Seek(IOStream::pos_type Offset, int SeekType)
{
	mrSource.Seek(Offset, SeekType);

	switch (SeekType)
	{
		case SeekType_Absolute:
		{
			// just go there
			mOffset = Offset;
		}
		break;

		case SeekType_Relative:
		{
			// Actual underlying file position is 
			// (mBufferSize - mBufferPosition) ahead of us.
			// Need to subtract that amount from the seek
			// to seek forward that much less, putting the 
			// real pointer in the right place.
			mOffset += Offset;
		}
		break;

		case SeekType_End:
		{
			// Actual underlying file position is 
			// (mBufferSize - mBufferPosition) ahead of us.
			// Need to add that amount to the seek
			// to seek backwards that much more, putting the 
			// real pointer in the right place.
			mOffset = mLength - Offset;
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::Close()
//		Purpose: Closes the underlying stream (not needed)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void ReadLoggingStream::Close()
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::StreamDataLeft()
//		Purpose: Any data left to write?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool ReadLoggingStream::StreamDataLeft()
{
	return mrSource.StreamDataLeft();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadLoggingStream::StreamClosed()
//		Purpose: Is the stream closed?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool ReadLoggingStream::StreamClosed()
{
	return mrSource.StreamClosed();
}

