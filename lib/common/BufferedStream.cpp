// --------------------------------------------------------------------------
//
// File
//		Name:    BufferedStream.cpp
//		Purpose: Buffering wrapper around IOStreams
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BufferedStream.h"
#include "CommonException.h"

#include <string.h>

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::BufferedStream(const char *, int, int)
//		Purpose: Constructor, set up buffer
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
BufferedStream::BufferedStream(IOStream& rSource)
: mrSource(rSource), mBufferSize(0), mBufferPosition(0)
{ }


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::Read(void *, int)
//		Purpose: Reads bytes from the file
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
int BufferedStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	if (mBufferSize == mBufferPosition)
	{
		// buffer is empty, fill it.

		int numBytesRead = mrSource.Read(mBuffer, sizeof(mBuffer), 
			Timeout);

		if (numBytesRead < 0)
		{
			return numBytesRead;
		}

		mBufferSize = numBytesRead;
	}

	int sizeToReturn = mBufferSize - mBufferPosition;

	if (sizeToReturn > NBytes)
	{
		sizeToReturn = NBytes;
	}

	memcpy(pBuffer, mBuffer + mBufferPosition, sizeToReturn);
	mBufferPosition += sizeToReturn;

	if (mBufferPosition == mBufferSize)
	{
		// clear out the buffer
		mBufferSize = 0;
		mBufferPosition = 0;
	}

	return sizeToReturn;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::BytesLeftToRead()
//		Purpose: Returns number of bytes to read (may not be most efficient function ever)
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
IOStream::pos_type BufferedStream::BytesLeftToRead()
{
	return mrSource.BytesLeftToRead() + mBufferSize - mBufferPosition;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::Write(void *, int)
//		Purpose: Writes bytes to the underlying stream (not supported)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void BufferedStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::GetPosition()
//		Purpose: Get position in stream
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
IOStream::pos_type BufferedStream::GetPosition() const
{
	return mrSource.GetPosition() - mBufferSize + mBufferPosition;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::Seek(pos_type, int)
//		Purpose: Seeks within file, as lseek, invalidate buffer
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void BufferedStream::Seek(IOStream::pos_type Offset, int SeekType)
{
	switch (SeekType)
	{
		case SeekType_Absolute:
		{
			// just go there
			mrSource.Seek(Offset, SeekType);
		}
		break;

		case SeekType_Relative:
		{
			// Actual underlying file position is 
			// (mBufferSize - mBufferPosition) ahead of us.
			// Need to subtract that amount from the seek
			// to seek forward that much less, putting the 
			// real pointer in the right place.
			mrSource.Seek(Offset - mBufferSize + mBufferPosition, 
				SeekType);
		}
		break;

		case SeekType_End:
		{
			// Actual underlying file position is 
			// (mBufferSize - mBufferPosition) ahead of us.
			// Need to add that amount to the seek
			// to seek backwards that much more, putting the 
			// real pointer in the right place.
			mrSource.Seek(Offset + mBufferSize - mBufferPosition, 
				SeekType);
		}
	}

	// always clear the buffer for now (may be slightly wasteful)
	mBufferSize = 0;
	mBufferPosition = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::Close()
//		Purpose: Closes the underlying stream (not needed)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void BufferedStream::Close()
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::StreamDataLeft()
//		Purpose: Any data left to write?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool BufferedStream::StreamDataLeft()
{
	return mrSource.StreamDataLeft();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedStream::StreamClosed()
//		Purpose: Is the stream closed?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool BufferedStream::StreamClosed()
{
	return mrSource.StreamClosed();
}

