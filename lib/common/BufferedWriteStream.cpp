// --------------------------------------------------------------------------
//
// File
//		Name:    BufferedWriteStream.cpp
//		Purpose: Buffering write-only wrapper around IOStreams
//		Created: 2010/09/13
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BufferedWriteStream.h"
#include "CommonException.h"

#include <string.h>

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::BufferedWriteStream(const char *, int, int)
//		Purpose: Constructor, set up buffer
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
BufferedWriteStream::BufferedWriteStream(IOStream& rSink)
: mrSink(rSink), mBufferPosition(0)
{ }


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::Read(void *, int)
//		Purpose: Reads bytes from the file - throws exception
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
int BufferedWriteStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::BytesLeftToRead()
//		Purpose: Returns number of bytes to read (may not be most efficient function ever)
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
IOStream::pos_type BufferedWriteStream::BytesLeftToRead()
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::Write(void *, int)
//		Purpose: Writes bytes to the underlying stream (not supported)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void BufferedWriteStream::Write(const void *pBuffer, int NBytes)
{
	int numBytesRemain = NBytes;

	do
	{
		int maxWritable = sizeof(mBuffer) - mBufferPosition;
		int numBytesToWrite = (NBytes < maxWritable) ? NBytes :
			maxWritable;

		if(numBytesToWrite > 0)
		{
			memcpy(mBuffer + mBufferPosition, pBuffer,
				numBytesToWrite);
			mBufferPosition += numBytesToWrite;
			pBuffer = ((const char *)pBuffer) + numBytesToWrite;
			numBytesRemain -= numBytesToWrite;
		}

		if(numBytesRemain > 0)
		{
			Flush();
		}
	}
	while(numBytesRemain > 0);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::GetPosition()
//		Purpose: Get position in stream
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
IOStream::pos_type BufferedWriteStream::GetPosition() const
{
	return mrSink.GetPosition() + mBufferPosition;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::Seek(pos_type, int)
//		Purpose: Seeks within file, as lseek, invalidate buffer
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void BufferedWriteStream::Seek(IOStream::pos_type Offset, int SeekType)
{
	// Always flush the buffer before seeking
	Flush();

	mrSink.Seek(Offset, SeekType);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::Flush();
//		Purpose: Write out current buffer contents and invalidate
//		Created: 2010/09/13
//
// --------------------------------------------------------------------------
void BufferedWriteStream::Flush(int Timeout)
{
	if(mBufferPosition > 0)
	{
		mrSink.Write(mBuffer, mBufferPosition);
	}

	mBufferPosition = 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::Close()
//		Purpose: Closes the underlying stream (not needed)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void BufferedWriteStream::Close()
{
	Flush();
	mrSink.Close();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::StreamDataLeft()
//		Purpose: Any data left to write?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool BufferedWriteStream::StreamDataLeft()
{
	THROW_EXCEPTION(CommonException, NotSupported);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BufferedWriteStream::StreamClosed()
//		Purpose: Is the stream closed?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool BufferedWriteStream::StreamClosed()
{
	return mrSink.StreamClosed();
}

