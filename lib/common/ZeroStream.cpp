// --------------------------------------------------------------------------
//
// File
//		Name:    ZeroStream.cpp
//		Purpose: An IOStream which returns all zeroes up to a certain size
//		Created: 2007/04/28
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "ZeroStream.h"
#include "CommonException.h"

#include <string.h>

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::ZeroStream(IOStream::pos_type)
//		Purpose: Constructor
//		Created: 2007/04/28
//
// --------------------------------------------------------------------------
ZeroStream::ZeroStream(IOStream::pos_type size)
: mSize(size), mPosition(0)
{ }


// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::Read(void *, int)
//		Purpose: Reads bytes from the file
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
int ZeroStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	ASSERT(NBytes > 0);

	int bytesToRead = NBytes;
	
	if (bytesToRead > mSize - mPosition)
	{
		bytesToRead = mSize - mPosition;
	}

	memset(pBuffer, 0, bytesToRead);
	mPosition += bytesToRead;

	return bytesToRead;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::BytesLeftToRead()
//		Purpose: Returns number of bytes to read (may not be most efficient function ever)
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------
IOStream::pos_type ZeroStream::BytesLeftToRead()
{
	return mSize - mPosition;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::Write(void *, int)
//		Purpose: Writes bytes to the underlying stream (not supported)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void ZeroStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::GetPosition()
//		Purpose: Get position in stream
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
IOStream::pos_type ZeroStream::GetPosition() const
{
	return mPosition;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::Seek(pos_type, int)
//		Purpose: Seeks within file, as lseek, invalidate buffer
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void ZeroStream::Seek(IOStream::pos_type Offset, int SeekType)
{
	switch (SeekType)
	{
		case SeekType_Absolute:
		{
			mPosition = Offset;
		}
		break;

		case SeekType_Relative:
		{
			mPosition += Offset;
		}
		break;

		case SeekType_End:
		{
			mPosition = mSize - Offset;
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::Close()
//		Purpose: Closes the underlying stream (not needed)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void ZeroStream::Close()
{
	THROW_EXCEPTION(CommonException, NotSupported);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::StreamDataLeft()
//		Purpose: Any data left to write?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool ZeroStream::StreamDataLeft()
{
	return false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ZeroStream::StreamClosed()
//		Purpose: Is the stream closed?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool ZeroStream::StreamClosed()
{
	return false;
}

