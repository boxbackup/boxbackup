// --------------------------------------------------------------------------
//
// File
//		Name:    ProtocolUncertainStream.h
//		Purpose: Read part of another stream
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "ProtocolUncertainStream.h"
#include "ServerException.h"
#include "Protocol.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolUncertainStream::ProtocolUncertainStream(IOStream &, int)
//		Purpose: Constructor, taking another stream.
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
ProtocolUncertainStream::ProtocolUncertainStream(IOStream &rSource)
	: mrSource(rSource),
	  mBytesLeftInCurrentBlock(0),
	  mFinished(false)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolUncertainStream::~ProtocolUncertainStream()
//		Purpose: Destructor. Won't absorb any unread bytes.
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
ProtocolUncertainStream::~ProtocolUncertainStream()
{
	if(!mFinished)
	{
		BOX_WARNING("ProtocolUncertainStream destroyed before "
			"stream finished");
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolUncertainStream::Read(void *, int, int)
//		Purpose: As interface.
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
int ProtocolUncertainStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	// Finished?
	if(mFinished)
	{
		return 0;
	}
	
	int read = 0;
	while(read < NBytes)
	{
		// Anything we can get from the current block?
		ASSERT(mBytesLeftInCurrentBlock >= 0);
		if(mBytesLeftInCurrentBlock > 0)
		{
			// Yes, let's use some of these up
			int toRead = (NBytes - read);
			if(toRead > mBytesLeftInCurrentBlock)
			{
				// Adjust downwards to only read stuff out of the current block
				toRead = mBytesLeftInCurrentBlock;
			}
			
			// Read it
			int r = mrSource.Read(((uint8_t*)pBuffer) + read, toRead, Timeout);
			// Give up now if it didn't return anything
			if(r == 0)
			{
				return read;
			}
			
			// Adjust counts of bytes by the bytes recieved
			read += r;
			mBytesLeftInCurrentBlock -= r;
			
			// stop now if the stream returned less than we asked for -- avoid blocking
			if(r != toRead)
			{
				return read;
			}
		}
		else
		{
			// Read the header byte to find out how much there is in the next block
			uint8_t header;
			if(mrSource.Read(&header, 1, Timeout) == 0)
			{
				// Didn't get the byte, return now
				return read;
			}
			
			// Interpret the byte...
			if(header == Protocol::ProtocolStreamHeader_EndOfStream)
			{
				// All done.
				mFinished = true;
				return read;
			}
			else if(header <= Protocol::ProtocolStreamHeader_MaxEncodedSizeValue)
			{
				// get size of the block from the Protocol's lovely list
				mBytesLeftInCurrentBlock = Protocol::sProtocolStreamHeaderLengths[header];
			}
			else if(header == Protocol::ProtocolStreamHeader_SizeIs64k)
			{
				// 64k
				mBytesLeftInCurrentBlock = (64*1024);
			}
			else
			{
				// Bad. It used the reserved values.
				THROW_EXCEPTION(ServerException, ProtocolUncertainStreamBadBlockHeader)	
			}
		}
	}

	// Return the number read
	return read;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolUncertainStream::BytesLeftToRead()
//		Purpose: As interface.
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
IOStream::pos_type ProtocolUncertainStream::BytesLeftToRead()
{
	// Only know how much is left if everything is finished
	return mFinished?(0):(IOStream::SizeOfStreamUnknown);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolUncertainStream::Write(const void *, int)
//		Purpose: As interface. But will exception.
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
void ProtocolUncertainStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(ServerException, CantWriteToProtocolUncertainStream)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolUncertainStream::StreamDataLeft()
//		Purpose: As interface.
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
bool ProtocolUncertainStream::StreamDataLeft()
{
	return !mFinished;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ProtocolUncertainStream::StreamClosed()
//		Purpose: As interface.
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
bool ProtocolUncertainStream::StreamClosed()
{
	// always closed
	return true;
}

