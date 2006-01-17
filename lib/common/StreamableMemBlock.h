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
//		Name:    StreamableMemBlock.h
//		Purpose: Memory blocks which can be loaded and saved from streams
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------

#ifndef STREAMABLEMEMBLOCK__H
#define STREAMABLEMEMBLOCK__H

class IOStream;

// --------------------------------------------------------------------------
//
// Class
//		Name:    StreamableMemBlock
//		Purpose: Memory blocks which can be loaded and saved from streams
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
class StreamableMemBlock
{
public:
	StreamableMemBlock();
	StreamableMemBlock(int Size);
	StreamableMemBlock(void *pBuffer, int Size);
	StreamableMemBlock(const StreamableMemBlock &rToCopy);
	~StreamableMemBlock();
	
	void Set(const StreamableMemBlock &rBlock);
	void Set(void *pBuffer, int Size);
	void Set(IOStream &rStream, int Timeout);
	StreamableMemBlock &operator=(const StreamableMemBlock &rBlock)
	{
		Set(rBlock);
		return *this;
	}

	void ReadFromStream(IOStream &rStream, int Timeout);
	void WriteToStream(IOStream &rStream) const;
	
	static void WriteEmptyBlockToStream(IOStream &rStream);
	
	void *GetBuffer() const;
	
	// Size of block
	int GetSize() const {return mSize;}

	// Buffer empty?
	bool IsEmpty() const {return mSize == 0;}

	// Clear the contents of the block
	void Clear() {FreeBlock();}
	
	bool operator==(const StreamableMemBlock &rCompare) const;

	void ResizeBlock(int Size);

protected:	// be careful with these!
	void AllocateBlock(int Size);
	void FreeBlock();

private:
	void *mpBuffer;
	int mSize;
};

#endif // STREAMABLEMEMBLOCK__H

