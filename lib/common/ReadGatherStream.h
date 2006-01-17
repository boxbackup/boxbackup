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
//		Name:    ReadGatherStream.h
//		Purpose: Build a stream (for reading only) out of a number of other streams.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------

#ifndef READGATHERSTREAM_H
#define READGATHERSTREAM_H

#include "IOStream.h"

#include <vector>

// --------------------------------------------------------------------------
//
// Class
//		Name:    ReadGatherStream
//		Purpose: Build a stream (for reading only) out of a number of other streams.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
class ReadGatherStream : public IOStream
{
public:
	ReadGatherStream(bool DeleteComponentStreamsOnDestruction);
	~ReadGatherStream();
private:
	ReadGatherStream(const ReadGatherStream &);
	ReadGatherStream &operator=(const ReadGatherStream &);
public:

	int AddComponent(IOStream *pStream);
	void AddBlock(int Component, pos_type Length, bool Seek = false, pos_type SeekTo = 0);

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();
	virtual pos_type GetPosition() const;

private:
	bool mDeleteComponentStreamsOnDestruction;
	std::vector<IOStream *> mComponents;
	
	typedef struct
	{
		pos_type mLength;
		pos_type mSeekTo;
		int mComponent;
		bool mSeek;
	} Block;
	
	std::vector<Block> mBlocks;
	
	pos_type mCurrentPosition;
	pos_type mTotalSize;
	unsigned int mCurrentBlock;
	pos_type mPositionInCurrentBlock;
	bool mSeekDoneForCurrent;
};


#endif // READGATHERSTREAM_H
