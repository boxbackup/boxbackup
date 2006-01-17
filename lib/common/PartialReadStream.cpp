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
//		Name:    PartialReadStream::PartialReadStream(IOStream &, int)
//		Purpose: Constructor, taking another stream and the number of bytes
//				 to be read from it.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
PartialReadStream::PartialReadStream(IOStream &rSource, int BytesToRead)
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

