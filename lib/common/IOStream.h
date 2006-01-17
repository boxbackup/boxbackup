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
//		Name:    IOStream.h
//		Purpose: I/O Stream abstraction
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef IOSTREAM__H
#define IOSTREAM__H

// --------------------------------------------------------------------------
//
// Class
//		Name:    IOStream
//		Purpose: Abstract interface to streams of data
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
class IOStream
{
public:
	IOStream();
	IOStream(const IOStream &rToCopy);
	virtual ~IOStream();
	
	enum
	{
		TimeOutInfinite = -1,
		SizeOfStreamUnknown = -1
	};
	
	enum
	{
		SeekType_Absolute = 0,
		SeekType_Relative = 1,
		SeekType_End = 2
	};
	
	// Timeout in milliseconds
	// Read may return 0 -- does not mean end of stream.
	typedef int64_t pos_type;
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite) = 0;
	virtual pos_type BytesLeftToRead();	// may return IOStream::SizeOfStreamUnknown (and will for most stream types)
	virtual void Write(const void *pBuffer, int NBytes) = 0;
	virtual void WriteAllBuffered();
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual void Close();
	
	// Has all data that can be read been read?
	virtual bool StreamDataLeft() = 0;
	// Has the stream been closed (writing not possible)
	virtual bool StreamClosed() = 0;
	
	// Utility functions
	bool ReadFullBuffer(void *pBuffer, int NBytes, int *pNBytesRead, int Timeout = IOStream::TimeOutInfinite);
	bool CopyStreamTo(IOStream &rCopyTo, int Timeout = IOStream::TimeOutInfinite, int BufferSize = 1024);
	
	static int ConvertSeekTypeToOSWhence(int SeekType);
};


#endif // IOSTREAM__H


