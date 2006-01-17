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
//		Name:    StreamableMemBlock.cpp
//		Purpose: Memory blocks which can be loaded and saved from streams
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include "StreamableMemBlock.h"
#include "IOStream.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::StreamableMemBlock()
//		Purpose: Constructor, making empty block
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
StreamableMemBlock::StreamableMemBlock()
	: mpBuffer(0),
	  mSize(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::StreamableMemBlock(void *, int)
//		Purpose: Create block, copying data from another bit of memory
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
StreamableMemBlock::StreamableMemBlock(void *pBuffer, int Size)
	: mpBuffer(0),
	  mSize(0)
{
	AllocateBlock(Size);
	::memcpy(mpBuffer, pBuffer, Size);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::StreamableMemBlock(int)
//		Purpose: Create block, initialising it to all zeros
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
StreamableMemBlock::StreamableMemBlock(int Size)
	: mpBuffer(0),
	  mSize(0)
{
	AllocateBlock(Size);
	::memset(mpBuffer, 0, Size);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::StreamableMemBlock(const StreamableMemBlock &)
//		Purpose: Copy constructor
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
StreamableMemBlock::StreamableMemBlock(const StreamableMemBlock &rToCopy)
	: mpBuffer(0),
	  mSize(0)
{
	AllocateBlock(rToCopy.mSize);
	::memcpy(mpBuffer, rToCopy.mpBuffer, mSize);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::Set(void *, int)
//		Purpose: Set the contents of the block
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void StreamableMemBlock::Set(void *pBuffer, int Size)
{
	FreeBlock();
	AllocateBlock(Size);
	::memcpy(mpBuffer, pBuffer, Size);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::Set(IOStream &)
//		Purpose: Set from stream. Stream must support BytesLeftToRead()
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void StreamableMemBlock::Set(IOStream &rStream, int Timeout)
{
	// Get size
	IOStream::pos_type size = rStream.BytesLeftToRead();
	if(size == IOStream::SizeOfStreamUnknown)
	{
		THROW_EXCEPTION(CommonException, StreamDoesntHaveRequiredProperty)
	}
	
	// Allocate a new block (this way to be exception safe)
	char *pblock = (char*)malloc(size);
	if(pblock == 0)
	{
		throw std::bad_alloc();
	}
	
	try
	{
		// Read in
		if(!rStream.ReadFullBuffer(pblock, size, 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
		}
	
		// Free the block ready for replacement
		FreeBlock();
	}
	catch(...)
	{
		::free(pblock);
		throw;
	}
	
	// store...
	ASSERT(mpBuffer == 0);
	mpBuffer = pblock;
	mSize = size;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::Set(const StreamableMemBlock &)
//		Purpose: Set from other block.
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
void StreamableMemBlock::Set(const StreamableMemBlock &rBlock)
{
	Set(rBlock.mpBuffer, rBlock.mSize);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::~StreamableMemBlock()
//		Purpose: Destructor
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
StreamableMemBlock::~StreamableMemBlock()
{
	FreeBlock();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::FreeBlock()
//		Purpose: Protected. Frees block of memory
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void StreamableMemBlock::FreeBlock()
{
	if(mpBuffer != 0)
	{
		::free(mpBuffer);
	}
	mpBuffer = 0;
	mSize = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::AllocateBlock(int)
//		Purpose: Protected. Allocate the block of memory
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void StreamableMemBlock::AllocateBlock(int Size)
{
	ASSERT(mpBuffer == 0);
	if(Size > 0)
	{
		mpBuffer = ::malloc(Size);
		if(mpBuffer == 0)
		{
			throw std::bad_alloc();
		}
	}
	mSize = Size;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::ResizeBlock(int)
//		Purpose: Protected. Resizes the allocated block.
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void StreamableMemBlock::ResizeBlock(int Size)
{
	ASSERT(mpBuffer != 0);
	ASSERT(Size > 0);
	if(Size > 0)
	{
		void *pnewBuffer = ::realloc(mpBuffer, Size);
		if(pnewBuffer == 0)
		{
			throw std::bad_alloc();
		}
		mpBuffer = pnewBuffer;
	}
	mSize = Size;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::ReadFromStream(IOStream &, int)
//		Purpose: Read the block in from a stream
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void StreamableMemBlock::ReadFromStream(IOStream &rStream, int Timeout)
{
	// Get the size of the block
	int32_t size_s;
	if(!rStream.ReadFullBuffer(&size_s, sizeof(size_s), 0 /* not interested in bytes read if this fails */))
	{
		THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
	}
	
	int size = ntohl(size_s);
	
	
	// Allocate a new block (this way to be exception safe)
	char *pblock = (char*)malloc(size);
	if(pblock == 0)
	{
		throw std::bad_alloc();
	}
	
	try
	{
		// Read in
		if(!rStream.ReadFullBuffer(pblock, size, 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
		}
	
		// Free the block ready for replacement
		FreeBlock();
	}
	catch(...)
	{
		::free(pblock);
		throw;
	}
	
	// store...
	ASSERT(mpBuffer == 0);
	mpBuffer = pblock;
	mSize = size;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::WriteToStream(IOStream &)
//		Purpose: Write the block to a stream
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void StreamableMemBlock::WriteToStream(IOStream &rStream) const
{
	int32_t sizenbo = htonl(mSize);
	// Size
	rStream.Write(&sizenbo, sizeof(sizenbo));
	// Buffer
	if(mSize > 0)
	{
		rStream.Write(mpBuffer, mSize);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::WriteEmptyBlockToStream(IOStream &)
//		Purpose: Writes an empty block to a stream.
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void StreamableMemBlock::WriteEmptyBlockToStream(IOStream &rStream)
{
	int32_t sizenbo = htonl(0);
	rStream.Write(&sizenbo, sizeof(sizenbo));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::GetBuffer()
//		Purpose: Get pointer to buffer
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
void *StreamableMemBlock::GetBuffer() const
{
	if(mSize == 0)
	{
		// Return something which isn't a null pointer
		static const int validptr = 0;
		return (void*)&validptr;
	}
	
	// return the buffer
	ASSERT(mpBuffer != 0);
	return mpBuffer;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    StreamableMemBlock::operator==(const StreamableMemBlock &)
//		Purpose: Test for equality of memory blocks
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
bool StreamableMemBlock::operator==(const StreamableMemBlock &rCompare) const
{
	if(mSize != rCompare.mSize) return false;
	if(mSize == 0 && rCompare.mSize == 0) return true;	// without memory comparison!
	return ::memcmp(mpBuffer, rCompare.mpBuffer, mSize) == 0;
}


