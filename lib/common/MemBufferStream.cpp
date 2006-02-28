// --------------------------------------------------------------------------
//
// File
//		Name:    MemBufferStream.cpp
//		Purpose: Stream to and from an encapsulated memory block
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "MemBufferStream.h"
#include "CommonException.h"
#include "Guards.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::MemBufferStream()
//		Purpose: Constructor
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------
MemBufferStream::MemBufferStream()
: mReadPosition(0),
  mWritePosition(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::MemBufferStream(const MemBufferStream &)
//		Purpose: Copy constructor (exceptions)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
MemBufferStream::MemBufferStream(const MemBufferStream &rToCopy)
{
	THROW_EXCEPTION(CommonException, NotSupported)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::MemBufferStream(const StreamableMemBlock &)
//		Purpose: Copy an existing StreamableMemBlock
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------
MemBufferStream::MemBufferStream(const StreamableMemBlock &rSource)
: mBuffer(rSource),
  mReadPosition(0),
  mWritePosition(rSource.GetSize())
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::~MemBufferStream()
//		Purpose: Destructor
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
MemBufferStream::~MemBufferStream()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::Close()
//		Purpose: Close the stream
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void MemBufferStream::Close()
{
	// Do nothing by default -- let the destructor clear everything up.
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::GetPosition()
//		Purpose: Returns current position in stream 
//			(just after the last byte read)
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
MemBufferStream::pos_type MemBufferStream::GetPosition() const
{
	return mReadPosition;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::WriteAllBuffered()
//		Purpose: Ensures that any data which has been buffered is written to the stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void MemBufferStream::WriteAllBuffered()
{
	// These aren't the buffers you're looking for.
	// Nothing to see here, move along.
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::BytesLeftToRead()
//		Purpose: Numbers of bytes left to read in the stream, or
//				 MemBufferStream::SizeOfStreamUnknown if this isn't known.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
MemBufferStream::pos_type MemBufferStream::BytesLeftToRead()
{
	return mWritePosition - mReadPosition;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::Read(void *pBuffer, int NBytes, 
//			int Timeout)
//		Purpose: Read some bytes from the buffer, up to the maximum
//			number available to read.
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------
int MemBufferStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	if (NBytes > BytesLeftToRead())
	{
		NBytes = BytesLeftToRead();
	}

	uint8_t* source = (uint8_t *)( mBuffer.GetBuffer() );
	memcpy(pBuffer, source + mReadPosition, NBytes);
	mReadPosition += NBytes;

	return NBytes;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::Write(const void *pBuffer, 
//			int NBytes)
//		Purpose: Write some bytes to the buffer, resizing it if
//			necessary, increasing the number available to read.
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------
void MemBufferStream::Write(const void *pBuffer, int NBytes)
{
	mBuffer.ResizeBlock(mWritePosition + NBytes);
	uint8_t* dest = (uint8_t *)( mBuffer.GetBuffer() );
	memcpy(dest + mWritePosition, pBuffer, NBytes);
	mWritePosition += NBytes;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::StreamDataLeft()
//		Purpose: Tell whether any bytes are still available to read.
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------
bool MemBufferStream::StreamDataLeft()
{
	return BytesLeftToRead() > 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBufferStream::StreamClosed()
//		Purpose: Tell whether the stream is closed (no effect)
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------
bool MemBufferStream::StreamClosed()
{
	return false;
}

