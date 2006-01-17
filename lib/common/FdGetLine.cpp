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
//		Name:    FdGetLine.cpp
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <unistd.h>

#include "FdGetLine.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// utility whitespace function
inline bool iw(int c)
{
	return (c == ' ' || c == '\t' || c == '\v' || c == '\f'); // \r, \n are already excluded
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::FdGetLine(int)
//		Purpose: Constructor, taking file descriptor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
FdGetLine::FdGetLine(int fd)
	: mFileHandle(fd),
	  mLineNumber(0),
	  mBufferBegin(0),
	  mBytesInBuffer(0),
	  mPendingEOF(false),
	  mEOF(false)
{
	if(mFileHandle < 0) {THROW_EXCEPTION(CommonException, BadArguments)}
	//printf("FdGetLine buffer size = %d\n", sizeof(mBuffer));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::~FdGetLine()
//		Purpose: Destructor
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
FdGetLine::~FdGetLine()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::GetLine(bool)
//		Purpose: Returns a file from the file. If Preprocess is true, leading
//				 and trailing whitespace is removed, and comments (after #)
//				 are deleted.
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
std::string FdGetLine::GetLine(bool Preprocess)
{
	if(mFileHandle == -1) {THROW_EXCEPTION(CommonException, GetLineNoHandle)}

	// EOF?
	if(mEOF) {THROW_EXCEPTION(CommonException, GetLineEOF)}
	
	std::string r;

	bool foundLineEnd = false;

	while(!foundLineEnd && !mEOF)
	{
		// Use any bytes left in the buffer
		while(mBufferBegin < mBytesInBuffer)
		{
			int c = mBuffer[mBufferBegin++];
			if(c == '\r')
			{
				// Ignore nasty Windows line ending extra chars
			}
			else if(c == '\n')
			{
				// Line end!
				foundLineEnd = true;
				break;
			}
			else
			{
				// Add to string
				r += c;
			}
			
			// Implicit line ending at EOF
			if(mBufferBegin >= mBytesInBuffer && mPendingEOF)
			{
				foundLineEnd = true;
			}
		}
		
		// Check size
		if(r.size() > FDGETLINE_MAX_LINE_SIZE)
		{
			THROW_EXCEPTION(CommonException, GetLineTooLarge)
		}
		
		// Read more in?
		if(!foundLineEnd && mBufferBegin >= mBytesInBuffer && !mPendingEOF)
		{
			int bytes = ::read(mFileHandle, mBuffer, sizeof(mBuffer));
			
			// Error?
			if(bytes == -1)
			{
				THROW_EXCEPTION(CommonException, OSFileError)
			}
			
			// Adjust buffer info
			mBytesInBuffer = bytes;
			mBufferBegin = 0;
			
			// EOF / closed?
			if(bytes == 0)
			{
				mPendingEOF = true;
			}
		}
		
		// EOF?
		if(mPendingEOF && mBufferBegin >= mBytesInBuffer)
		{
			// File is EOF, and now we've depleted the buffer completely, so tell caller as well.
			mEOF = true;
		}
	}

	if(!Preprocess)
	{
		return r;
	}
	else
	{
		// Check for comment char, but char before must be whitespace
		int end = 0;
		int size = r.size();
		while(end < size)
		{
			if(r[end] == '#' && (end == 0 || (iw(r[end-1]))))
			{
				break;
			}
			end++;
		}
		
		// Remove whitespace
		int begin = 0;
		while(begin < size && iw(r[begin]))
		{
			begin++;
		}
		if(!iw(r[end])) end--;
		while(end > begin && iw(r[end]))
		{
			end--;
		}
		
		// Return a sub string
		return r.substr(begin, end - begin + 1);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FdGetLine::DetachFile()
//		Purpose: Detaches the file handle, setting the file pointer correctly.
//				 Probably not good for sockets...
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
void FdGetLine::DetachFile()
{
	if(mFileHandle == -1) {THROW_EXCEPTION(CommonException, GetLineNoHandle)}

	// Adjust file pointer
	int bytesOver = mBufferBegin - mBufferBegin;
	ASSERT(bytesOver >= 0);
	if(bytesOver > 0)
	{
		if(::lseek(mFileHandle, 0 - bytesOver, SEEK_CUR) == -1)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	}

	// Unset file pointer
	mFileHandle = -1;
}


