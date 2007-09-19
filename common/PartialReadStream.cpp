// --------------------------------------------------------------------------
//
// File
//		Name:    PartialReadStream.h
//		Purpose: Read part of another stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "PartialReadStream.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    PartialReadStream::PartialReadStream(IOStream &,
//			 pos_type)
//		Purpose: Constructor, taking another stream and the number of
//			 bytes to be read from it.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
PartialReadStream::PartialReadStream(IOStream &rSource,
	pos_type BytesToRead)
	: mrSource(rSource),
	  mBytesLeft(BytesToRead)
{
	ASSERT(BytesToRead > 0);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    PartialReadStream::~PartialReadStream()
//		Purpose: Destructor. Won't absorb any unread bytes.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
PartialReadStream::~PartialReadStream()
{
	// Warn in debug mode
	if(mBytesLeft != 0)
	{
		TRACE1("PartialReadStream::~PartialReadStream when mBytesLeft = %d\n", mBytesLeft);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    PartialReadStream::Read(void *, int, int)
//		Purpose: As interface.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
int PartialReadStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	// Finished?
	if(mBytesLeft <= 0)
	{
		return 0;
	}

	// Asking for more than is allowed?
	if(NBytes > mBytesLeft)
	{
		// Adjust downwards
		NBytes = mBytesLeft;
	}
	
	// Route the request to the source
	int read = mrSource.Read(pBuffer, NBytes, Timeout);
	ASSERT(read <= mBytesLeft);
	
	// Adjust the count
	mBytesLeft -= read;
	
	// Return the number read
	return read;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    PartialReadStream::BytesLeftToRead()
//		Purpose: As interface.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
IOStream::pos_type PartialReadStream::BytesLeftToRead()
{
	return mBytesLeft;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    PartialReadStream::Write(const void *, int)
//		Purpose: As interface. But will exception.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void PartialReadStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(CommonException, CantWriteToPartialReadStream)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    PartialReadStream::StreamDataLeft()
//		Purpose: As interface.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
bool PartialReadStream::StreamDataLeft()
{
	return mBytesLeft != 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    PartialReadStream::StreamClosed()
//		Purpose: As interface.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
bool PartialReadStream::StreamClosed()
{
	// always closed
	return true;
}

