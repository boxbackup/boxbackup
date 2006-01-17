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
//		Name:    CompressContext.h
//		Purpose: Interface to zlib compression
//		Created: 5/12/03
//
// --------------------------------------------------------------------------

#ifndef COMPRESSCONTEXT__H
#define COMPRESSCONTEXT__H

#include <zlib.h>

#include "CompressException.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    CompressContext
//		Purpose: Interface to zlib compression, only very slight wrapper.
//				 (Use CompressStream for a more friendly interface.)
//		Created: 5/12/03
//
// --------------------------------------------------------------------------
template<bool Compressing>
class Compress
{
public:
	Compress()
		: mFinished(false),
		  mFlush(Z_NO_FLUSH)
	{	
		// initialise stream
		mStream.zalloc = Z_NULL;
		mStream.zfree = Z_NULL;
		mStream.opaque = Z_NULL;
		mStream.data_type = Z_BINARY;

		if((Compressing)?(deflateInit(&mStream, Z_DEFAULT_COMPRESSION))
			:(inflateInit(&mStream)) != Z_OK)
		{
			THROW_EXCEPTION(CompressException, InitFailed)
		}
		
		mStream.avail_in = 0;
	}
	
	~Compress()
	{
		int r = 0;
		if((r = ((Compressing)?(deflateEnd(&mStream))
			:(inflateEnd(&mStream)))) != Z_OK)
		{
			TRACE1("zlib error code = %d\n", r);
			if(r == Z_DATA_ERROR)
			{
				TRACE0("WARNING: End of compress/decompress without all input being consumed -- possible corruption?\n");
			}
			else
			{
				THROW_EXCEPTION(CompressException, EndFailed)
			}
		}
	}
		
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Compress<Function>::InputRequired()
	//		Purpose: Input required yet?
	//		Created: 5/12/03
	//
	// --------------------------------------------------------------------------
	bool InputRequired()
	{
		return mStream.avail_in <= 0;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Compress<Function>::Input(const void *, int)
	//		Purpose: Set the input buffer ready for next output call.
	//		Created: 5/12/03
	//
	// --------------------------------------------------------------------------
	void Input(const void *pInBuffer, int InLength)
	{
		// Check usage
		if(mStream.avail_in != 0)
		{
			THROW_EXCEPTION(CompressException, BadUsageInputNotRequired)
		}
		
		// Store info
		mStream.next_in = (unsigned char *)pInBuffer;
		mStream.avail_in = InLength;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Compress<Function>::FinishInput()
	//		Purpose: When compressing, no more input will be given.
	//		Created: 5/12/03
	//
	// --------------------------------------------------------------------------
	void FinishInput()
	{
		mFlush = Z_FINISH;
	}
		
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Compress<Function>::Output(void *, int)
	//		Purpose: Get some output data
	//		Created: 5/12/03
	//
	// --------------------------------------------------------------------------
	int Output(void *pOutBuffer, int OutLength, bool SyncFlush = false)
	{
		// need more input?
		if(mStream.avail_in == 0 && mFlush != Z_FINISH && !SyncFlush)
		{
			return 0;
		}
	
		// Buffers
		mStream.next_out = (unsigned char *)pOutBuffer;
		mStream.avail_out = OutLength;
		
		// Call one of the functions
		int flush = mFlush;
		if(SyncFlush && mFlush != Z_FINISH)
		{
			flush = Z_SYNC_FLUSH;
		}
		int ret = (Compressing)?(deflate(&mStream, flush)):(inflate(&mStream, flush));
		
		if(SyncFlush && ret == Z_BUF_ERROR)
		{
			// No progress possible. Just return 0.
			return 0;
		}
		
		// Check errors
		if(ret < 0)
		{
			TRACE1("zlib error code = %d\n", ret);			
			THROW_EXCEPTION(CompressException, TransformFailed)
		}
		
		// Parse result
		if(ret == Z_STREAM_END)
		{
			mFinished = true;
		}
		
		// Return how much data was output
		return OutLength - mStream.avail_out;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Compress<Function>::OutputHasFinished()
	//		Purpose: No more output to be recieved 
	//		Created: 5/12/03
	//
	// --------------------------------------------------------------------------
	bool OutputHasFinished()
	{
		return mFinished;
	}
	
	
private:
	z_stream mStream;
	bool mFinished;
	int mFlush;
};

template<typename Integer>
Integer Compress_MaxSizeForCompressedData(Integer InLen)
{
	// Conservative rendition of the info found here: http://www.gzip.org/zlib/zlib_tech.html
	int blocks = (InLen + 32*1024 - 1) / (32*1024);
	return InLen + (blocks * 6) + 8;
}


#endif // COMPRESSCONTEXT__H

