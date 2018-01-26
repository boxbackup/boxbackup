// --------------------------------------------------------------------------
//
// File
//		Name:    IOStream.cpp
//		Purpose: I/O Stream abstraction
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "IOStream.h"
#include "CommonException.h"
#include "Guards.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::IOStream()
//		Purpose: Constructor
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
IOStream::IOStream()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::~IOStream()
//		Purpose: Destructor
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
IOStream::~IOStream()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::Close()
//		Purpose: Close the stream
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void IOStream::Close()
{
	// Do nothing by default -- let the destructor clear everything up.
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::Seek(int, int)
//		Purpose: Seek in stream (if supported)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void IOStream::Seek(IOStream::pos_type Offset, int SeekType)
{
	THROW_EXCEPTION(CommonException, NotSupported)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::GetPosition()
//		Purpose: Returns current position in stream (if supported)
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
IOStream::pos_type IOStream::GetPosition() const
{
	THROW_EXCEPTION(CommonException, NotSupported)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::ConvertSeekTypeToOSWhence(int)
//		Purpose: Return an whence arg for lseek given a IOStream seek type
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
int IOStream::ConvertSeekTypeToOSWhence(int SeekType)
{
	// Should be nicely optimised out as values are choosen in header file to match OS values.
	int ostype = SEEK_SET;
	switch(SeekType)
	{
#ifdef WIN32
	case SeekType_Absolute:
		ostype = FILE_BEGIN;
		break;
	case SeekType_Relative:
		ostype = FILE_CURRENT;
		break;
	case SeekType_End:
		ostype = FILE_END;
		break;
#else // ! WIN32
	case SeekType_Absolute:
		ostype = SEEK_SET;
		break;
	case SeekType_Relative:
		ostype = SEEK_CUR;
		break;
	case SeekType_End:
		ostype = SEEK_END;
		break;
#endif // WIN32
	
	default:
		THROW_EXCEPTION(CommonException, IOStreamBadSeekType)
	}
	
	return ostype;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::ReadFullBuffer(void *, int, int)
//		Purpose: Reads bytes into buffer, returning whether or not it managed to
//			 get all the bytes required. Exception and abort use of stream
//			 if this returns false.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
bool IOStream::ReadFullBuffer(void *pBuffer, int NBytes, int *pNBytesRead, int Timeout)
{
	int bytesToGo = NBytes;
	char *buffer = (char*)pBuffer;
	if(pNBytesRead) (*pNBytesRead) = 0;
	
	while(bytesToGo > 0)
	{
		int bytesRead = Read(buffer, bytesToGo, Timeout);
		if(bytesRead == 0)
		{
			if(errno == EINTR)
			{
				THROW_EXCEPTION(CommonException, SignalReceived);
			}
			else
			{
				// Timeout or something
				return false;
			}
		}
		// Increment things
		bytesToGo -= bytesRead;
		buffer += bytesRead;
		if(pNBytesRead) (*pNBytesRead) += bytesRead;
	}
	
	// Got everything
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::WriteAllBuffered()
//		Purpose: Ensures that any data which has been buffered is written to the stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void IOStream::WriteAllBuffered(int Timeout)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::BytesLeftToRead()
//		Purpose: Numbers of bytes left to read in the stream, or
//				 IOStream::SizeOfStreamUnknown if this isn't known.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
IOStream::pos_type IOStream::BytesLeftToRead()
{
	return IOStream::SizeOfStreamUnknown;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::CopyStreamTo(IOStream &, int Timeout)
//		Purpose: Copies the entire stream to another stream (reading
//			 from this, writing to rCopyTo). Returns the number
//			 of bytes copied. Throws an exception if a network
//			 timeout occurs.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
IOStream::pos_type IOStream::CopyStreamTo(IOStream &rCopyTo, int Timeout, int BufferSize)
{
	// Make sure there's something to do before allocating that buffer
	if(!StreamDataLeft())
	{
		return 0;
	}

	// Buffer
	MemoryBlockGuard<char*> buffer(BufferSize);
	IOStream::pos_type bytes_copied = 0;

	// Get copying!
	while(StreamDataLeft())
	{
		// Read some data
		int bytes = Read(buffer, BufferSize, Timeout);
		if(bytes == 0 && StreamDataLeft())
		{
			THROW_EXCEPTION_MESSAGE(CommonException, IOStreamTimedOut,
				"Timed out copying stream");
		}
		
		// Write some data
		if(bytes != 0)
		{
			rCopyTo.Write(buffer, bytes, Timeout);
		}

		bytes_copied += bytes;
	}

	return bytes_copied;	// completed
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::Flush(int Timeout)
//		Purpose: Read and discard all remaining data in stream.
//			 Useful for protocol streams which must be flushed
//			 to avoid breaking the protocol.
//		Created: 2008/08/20
//
// --------------------------------------------------------------------------
void IOStream::Flush(int Timeout)
{
	char buffer[4096];

	while(StreamDataLeft())
	{
		Read(buffer, sizeof(buffer), Timeout);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::Write
//		Purpose: Convenience method for writing a C++ string to a
//			 protocol buffer.
//
// --------------------------------------------------------------------------
void IOStream::Write(const std::string& rBuffer, int Timeout)
{
	Write(rBuffer.c_str(), rBuffer.size(), Timeout);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::ToString()
//		Purpose: Returns a string which describes this stream. Useful
//			 when reporting exceptions about a stream of unknown
//			 origin, for example in BackupStoreDirectory().
//		Created: 2014/04/28
//
// --------------------------------------------------------------------------
std::string IOStream::ToString() const
{
	return "unknown IOStream";
}
