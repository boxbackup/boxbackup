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
//		Name:    MemBlockStream.cpp
//		Purpose: Stream out data from any memory block
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "MemBlockStream.h"
#include "CommonException.h"
#include "StreamableMemBlock.h"
#include "CollectInBufferStream.h"

#include "MemLeakFindOn.h"

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
void MemBlockStream::Write(const void *pBuffer, int NBytes)
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
void MemBlockStream::Seek(pos_type Offset, int SeekType)
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
	default:
		THROW_EXCEPTION(CommonException, IOStreamBadSeekType)
		break;
	}
	
	// Make sure it doesn't go over
	if(newPos > mBytesInBuffer)
	{
		newPos = mBytesInBuffer;
	}
	// or under
	if(newPos < 0)
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

