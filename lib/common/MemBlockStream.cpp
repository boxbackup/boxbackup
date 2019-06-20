// --------------------------------------------------------------------------
//
// File
//		Name:    MemBlockStream.cpp
//		Purpose: Stream out data from any memory block
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include <string>

#include "MemBlockStream.h"
#include "CommonException.h"
#include "StreamableMemBlock.h"
#include "CollectInBufferStream.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::MemBlockStream()
//		Purpose: Constructor with no contents
//		Created: 2012/11/07
//
// --------------------------------------------------------------------------
MemBlockStream::MemBlockStream()
: mpBuffer(NULL),
  mBytesInBuffer(0),
  mReadPosition(0)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::MemBlockStream()
//		Purpose: Constructor (doesn't copy block, careful with lifetimes)
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
MemBlockStream::MemBlockStream(const void *pBuffer, int Size)
	: mpBuffer((char*)pBuffer),
	  mBytesInBuffer(Size),
	  mReadPosition(0)
{
	ASSERT(pBuffer != 0);
	ASSERT(Size >= 0);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::MemBlockStream(const std::string& rMessage)
//		Purpose: Convenience constructor for sending a simple string.
//			 Copies the string, so you can pass a temporary in.
//		Created: 2014/01/20
//
// --------------------------------------------------------------------------
MemBlockStream::MemBlockStream(const std::string& rMessage)
: mReadPosition(0)
{
	mTempBuffer.Write(rMessage.c_str(), rMessage.size());
	mTempBuffer.SetForReading();
	mpBuffer = (const char *)(mTempBuffer.GetBuffer());
	mBytesInBuffer = rMessage.size();
	ASSERT(mpBuffer != 0);
	ASSERT(mBytesInBuffer >= 0);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::MemBlockStream(const StreamableMemBlock &)
//		Purpose: Constructor (doesn't copy block, careful with lifetimes)
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
MemBlockStream::MemBlockStream(const StreamableMemBlock &rBlock)
	: mpBuffer((char*)rBlock.GetBuffer()),
	  mBytesInBuffer(rBlock.GetSize()),
	  mReadPosition(0)
{
	ASSERT(mpBuffer != 0);
	ASSERT(mBytesInBuffer >= 0);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::MemBlockStream(const StreamableMemBlock &)
//		Purpose: Constructor (doesn't copy block, careful with lifetimes)
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
MemBlockStream::MemBlockStream(const CollectInBufferStream &rBuffer)
	: mpBuffer((char*)rBuffer.GetBuffer()),
	  mBytesInBuffer(rBuffer.GetSize()),
	  mReadPosition(0)
{
	ASSERT(mpBuffer != 0);
	ASSERT(mBytesInBuffer >= 0);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::MemBlockStream(const MemBlockStream &)
//		Purpose: Copy constructor
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
MemBlockStream::MemBlockStream(const MemBlockStream &rToCopy)
	: mpBuffer(rToCopy.mpBuffer),
	  mBytesInBuffer(rToCopy.mBytesInBuffer),
	  mReadPosition(0)
{
	ASSERT(mpBuffer != 0);
	ASSERT(mBytesInBuffer >= 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::~MemBlockStream()
//		Purpose: Destructor
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
MemBlockStream::~MemBlockStream()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::Read(void *, int, int)
//		Purpose: As interface. But only works in read phase
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
int MemBlockStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	// Adjust to number of bytes left
	if(NBytes > (mBytesInBuffer - mReadPosition))
	{
		NBytes = (mBytesInBuffer - mReadPosition);
	}
	ASSERT(NBytes >= 0);
	if(NBytes <= 0) return 0;	// careful now
	
	// Copy in the requested number of bytes and adjust the read pointer
	::memcpy(pBuffer, mpBuffer + mReadPosition, NBytes);
	mReadPosition += NBytes;
	
	return NBytes;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::BytesLeftToRead()
//		Purpose: As interface. But only works in read phase
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
IOStream::pos_type MemBlockStream::BytesLeftToRead()
{
	return (mBytesInBuffer - mReadPosition);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::Write(void *, int)
//		Purpose: As interface. But only works in write phase
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void MemBlockStream::Write(const void *pBuffer, int NBytes, int Timeout)
{
	THROW_EXCEPTION(CommonException, MemBlockStreamNotSupported)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::GetPosition()
//		Purpose: In write phase, returns the number of bytes written, in read
//				 phase, the number of bytes to go
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
IOStream::pos_type MemBlockStream::GetPosition() const
{
	return mReadPosition;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::Seek(pos_type, int)
//		Purpose: As interface.
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void MemBlockStream::Seek(pos_type Offset, seek_type SeekType)
{
	int newPos = 0;
	switch(SeekType)
	{
	case IOStream::SeekType_Absolute:
		newPos = Offset;
		break;
	case IOStream::SeekType_Relative:
		newPos = mReadPosition + Offset;
		break;
	case IOStream::SeekType_End:
		newPos = mBytesInBuffer + Offset;
		break;
	}
	
	// Make sure it doesn't go over
	if(newPos > mBytesInBuffer)
	{
		newPos = mBytesInBuffer;
	}
	// or under
	else if(newPos < 0)
	{
		newPos = 0;
	}
	
	// Set the new read position
	mReadPosition = newPos;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::StreamDataLeft()
//		Purpose: As interface
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
bool MemBlockStream::StreamDataLeft()
{
	return mReadPosition < mBytesInBuffer;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    MemBlockStream::StreamClosed()
//		Purpose: As interface
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
bool MemBlockStream::StreamClosed()
{
	return true;
}

