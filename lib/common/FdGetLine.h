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
//		Name:    FdGetLine.h
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------

#ifndef FDGETLINE__H
#define FDGETLINE__H

#include <string>

#ifdef NDEBUG
	#define FDGETLINE_BUFFER_SIZE		1024
#else
	#define FDGETLINE_BUFFER_SIZE		4
#endif

// Just a very large upper bound for line size to avoid
// people sending lots of data over sockets and causing memory problems.
#define FDGETLINE_MAX_LINE_SIZE			(1024*256)

// --------------------------------------------------------------------------
//
// Class
//		Name:    FdGetLine
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
class FdGetLine
{
public:
	FdGetLine(int fd);
	~FdGetLine();
private:
	FdGetLine(const FdGetLine &rToCopy);

public:
	std::string GetLine(bool Preprocess = false);
	bool IsEOF() {return mEOF;}
	int GetLineNumber() {return mLineNumber;}
	
	// Call to detach, setting file pointer correctly to last bit read.
	// Only works for lseek-able file descriptors.
	void DetachFile();
	
private:
	char mBuffer[FDGETLINE_BUFFER_SIZE];
	int mFileHandle;
	int mLineNumber;
	int mBufferBegin;
	int mBytesInBuffer;
	bool mPendingEOF;
	bool mEOF;
};

#endif // FDGETLINE__H

