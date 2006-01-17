// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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
//		Name:    IOStream::IOStream(const IOStream &)
//		Purpose: Copy constructor (exceptions)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
IOStream::IOStream(const IOStream &rToCopy)
{
	THROW_EXCEPTION(CommonException, NotSupported)
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
	case SeekType_Absolute:
		ostype = SEEK_SET;
		break;
	case SeekType_Relative:
		ostype = SEEK_CUR;
		break;
	case SeekType_End:
		ostype = SEEK_END;
		break;
	
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
//				 get all the bytes required. Exception and abort use of stream
//				 if this returns false.
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
			// Timeout or something
			return false;
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
void IOStream::WriteAllBuffered()
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
//		Purpose: Copies the entire stream to another stream (reading from this,
//				 writing to rCopyTo). Returns whether the copy completed (ie
//				 StreamDataLeft() returns false)
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
bool IOStream::CopyStreamTo(IOStream &rCopyTo, int Timeout, int BufferSize)
{
	// Make sure there's something to do before allocating that buffer
	if(!StreamDataLeft())
	{
		return true;	// complete, even though nothing happened
	}

	// Buffer
	MemoryBlockGuard<char*> buffer(BufferSize);
	
	// Get copying!
	while(StreamDataLeft())
	{
		// Read some data
		int bytes = Read(buffer, BufferSize, Timeout);
		if(bytes == 0 && StreamDataLeft())
		{
			return false;	// incomplete, timed out
		}
		
		// Write some data
		if(bytes != 0)
		{
			rCopyTo.Write(buffer, bytes);
		}
	}
	
	return true;	// completed
}


