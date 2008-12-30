// --------------------------------------------------------------------------
//
// File
//		Name:    CompressStream.h
//		Purpose: Compressing stream
//		Created: 27/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <memory>

#include "CompressStream.h"
#include "Compress.h"
#include "autogen_CompressException.h"

#include "MemLeakFindOn.h"

// How big a buffer to use
#ifndef BOX_RELEASE_BUILD
	// debug!
	#define BUFFER_SIZE	256
#else
	#define BUFFER_SIZE	(32*1024)
#endif

#define USE_READ_COMPRESSOR		\
	CheckRead();				\
	Compress<false> *pDecompress = (Compress<false> *)mpReadCompressor;

#define USE_WRITE_COMPRESSOR	\
	CheckWrite();				\
	Compress<true> *pCompress = (Compress<true> *)mpWriteCompressor;


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::CompressStream(IOStream *, bool, bool, bool, bool)
//		Purpose: Constructor
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
CompressStream::CompressStream(IOStream *pStream, bool TakeOwnership,
		bool DecompressRead, bool CompressWrite, bool PassThroughWhenNotCompressed)
	: mpStream(pStream),
	  mHaveOwnership(TakeOwnership),
	  mDecompressRead(DecompressRead),
	  mCompressWrite(CompressWrite),
	  mPassThroughWhenNotCompressed(PassThroughWhenNotCompressed),
	  mpReadCompressor(0),
	  mpWriteCompressor(0),
	  mpBuffer(0),
	  mIsClosed(false)
{
	if(mpStream == 0)
	{
		THROW_EXCEPTION(CompressException, NullPointerPassedToCompressStream)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::~CompressStream()
//		Purpose: Destructor
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
CompressStream::~CompressStream()
{
	// Clean up compressors
	if(mpReadCompressor)
	{
		delete ((Compress<false>*)mpReadCompressor);
		mpReadCompressor = 0;
	}
	if(mpWriteCompressor)
	{
		delete ((Compress<true>*)mpWriteCompressor);
		mpWriteCompressor = 0;
	}

	// Delete the stream, if we have ownership
	if(mHaveOwnership)
	{
		delete mpStream;
		mpStream = 0;
	}
	
	// Free any buffer
	if(mpBuffer != 0)
	{
		::free(mpBuffer);
		mpBuffer = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::CompressStream(const CompressStream &)
//		Purpose: Copy constructor, will exception
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
CompressStream::CompressStream(const CompressStream &)
{
	THROW_EXCEPTION(CompressException, CopyCompressStreamNotAllowed)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::operator=(const CompressStream &)
//		Purpose: Assignment operator, will exception
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
CompressStream &CompressStream::operator=(const CompressStream &)
{
	THROW_EXCEPTION(CompressException, CopyCompressStreamNotAllowed)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::Read(void *, int, int)
//		Purpose: As interface
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
int CompressStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	USE_READ_COMPRESSOR
	if(pDecompress == 0)
	{
		return mpStream->Read(pBuffer, NBytes, Timeout);
	}

	// Where is the buffer? (note if writing as well, read buffer is second in a block of two buffer sizes)
	void *pbuf = (mDecompressRead && mCompressWrite)?(((uint8_t*)mpBuffer) + BUFFER_SIZE):mpBuffer;

	// Any data left to go?
	if(!pDecompress->InputRequired())
	{
		// Output some data from the existing data read
		return pDecompress->Output(pBuffer, NBytes, true /* write as much as possible */);
	}

	// Read data into the buffer -- read as much as possible in one go
	int s = mpStream->Read(pbuf, BUFFER_SIZE, Timeout);
	if(s == 0)
	{
		return 0;
	}
	
	// Give input to the compressor
	pDecompress->Input(pbuf, s);

	// Output as much as possible
	return pDecompress->Output(pBuffer, NBytes, true /* write as much as possible */);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::Write(const void *, int)
//		Purpose: As interface
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
void CompressStream::Write(const void *pBuffer, int NBytes)
{
	USE_WRITE_COMPRESSOR
	if(pCompress == 0)
	{
		mpStream->Write(pBuffer, NBytes);
		return;
	}
	
	if(mIsClosed)
	{
		THROW_EXCEPTION(CompressException, CannotWriteToClosedCompressStream)
	}

	// Give the data to the compressor
	pCompress->Input(pBuffer, NBytes);
	
	// Write the data to the stream
	WriteCompressedData();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::WriteAllBuffered()
//		Purpose: As interface
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
void CompressStream::WriteAllBuffered()
{
	if(mIsClosed)
	{
		THROW_EXCEPTION(CompressException, CannotWriteToClosedCompressStream)
	}

	// Just ask compressed data to be written out, but with the sync flag set
	WriteCompressedData(true);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::Close()
//		Purpose: As interface
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
void CompressStream::Close()
{
	if(mCompressWrite)
	{
		USE_WRITE_COMPRESSOR
		if(pCompress != 0)
		{
			// Flush anything from the write buffer
			pCompress->FinishInput();
			WriteCompressedData();
			
			// Mark as definately closed
			mIsClosed = true;
		}
	}

	// Close
	mpStream->Close();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::WriteCompressedData(bool)
//		Purpose: Private. Writes the output of the compressor to the stream,
//				 optionally doing a sync flush.
//		Created: 28/5/04
//
// --------------------------------------------------------------------------
void CompressStream::WriteCompressedData(bool SyncFlush)
{
	USE_WRITE_COMPRESSOR
	if(pCompress == 0) {THROW_EXCEPTION(CompressException, Internal)}
	
	int s = 0;
	do
	{
		s = pCompress->Output(mpBuffer, BUFFER_SIZE, SyncFlush);
		if(s > 0)
		{
			mpStream->Write(mpBuffer, s);
		}
	} while(s > 0);
	// Check assumption -- all input has been consumed
	if(!pCompress->InputRequired()) {THROW_EXCEPTION(CompressException, Internal)}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::StreamDataLeft()
//		Purpose: As interface
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
bool CompressStream::StreamDataLeft()
{
	USE_READ_COMPRESSOR
	if(pDecompress == 0)
	{
		return mpStream->StreamDataLeft();
	}

	// Any bytes left in our buffer?
	if(!pDecompress->InputRequired())
	{
		// Still buffered data to decompress
		return true;
	}

	// Otherwise ask the stream
	return mpStream->StreamDataLeft();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::StreamClosed()
//		Purpose: As interface
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
bool CompressStream::StreamClosed()
{
	if(!mIsClosed && mpStream->StreamClosed())
	{
		mIsClosed = true;
	}
	return mIsClosed;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::CheckRead()
//		Purpose: Checks that everything is set up to read
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
void CompressStream::CheckRead()
{
	// Has the read compressor already been created?
	if(mpReadCompressor != 0)
	{
		return;
	}
	
	// Need to create one?
	if(mDecompressRead)
	{
		mpReadCompressor = new Compress<false>();
		// And make sure there's a buffer
		CheckBuffer();
	}
	else
	{
		// Not decompressing. Should be passing through data?
		if(!mPassThroughWhenNotCompressed)
		{
			THROW_EXCEPTION(CompressException, CompressStreamReadSupportNotRequested)
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::CheckWrite()
//		Purpose: Checks that everything is set up to write
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
void CompressStream::CheckWrite()
{
	// Has the read compressor already been created?
	if(mpWriteCompressor != 0)
	{
		return;
	}
	
	// Need to create one?
	if(mCompressWrite)
	{
		mpWriteCompressor = new Compress<true>();
		// And make sure there's a buffer
		CheckBuffer();
	}
	else
	{
		// Not decompressing. Should be passing through data?
		if(!mPassThroughWhenNotCompressed)
		{
			THROW_EXCEPTION(CompressException, CompressStreamWriteSupportNotRequested)
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CompressStream::CheckBuffer()
//		Purpose: Allocates the buffer for (de)compression operations
//		Created: 28/5/04
//
// --------------------------------------------------------------------------
void CompressStream::CheckBuffer()
{
	// Already done
	if(mpBuffer != 0)
	{
		return;
	}
	
	// Allocate the buffer -- which may actually be two buffers in one
	// The temporary use buffer is first (used by write only, so only present if writing)
	// and the read buffer follows.
	int size = BUFFER_SIZE;
	if(mDecompressRead && mCompressWrite)
	{
		size *= 2;
	}
	BOX_TRACE("Allocating CompressStream buffer, size " << size);
	mpBuffer = ::malloc(size);
	if(mpBuffer == 0)
	{
		throw std::bad_alloc();
	}
}


