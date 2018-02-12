// --------------------------------------------------------------------------
//
// File
//		Name:    MemBlockStream.h
//		Purpose: Stream out data from any memory block
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------

#ifndef MEMBLOCKSTREAM__H
#define MEMBLOCKSTREAM__H

#include <string>

#include "CollectInBufferStream.h"
#include "IOStream.h"

class StreamableMemBlock;

// --------------------------------------------------------------------------
//
// Class
//		Name:    MemBlockStream
//		Purpose: Stream out data from any memory block -- be careful the lifetime
//				 of the block is greater than the lifetime of this stream.
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
class MemBlockStream : public IOStream
{
public:
	MemBlockStream();
	MemBlockStream(const void *pBuffer, int Size);
	MemBlockStream(const std::string& rMessage);
	MemBlockStream(const StreamableMemBlock &rBlock);
	MemBlockStream(const CollectInBufferStream &rBuffer);
	MemBlockStream(const MemBlockStream &rToCopy);
	~MemBlockStream();
public:

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	using IOStream::Write;

	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();
	virtual const void* GetBuffer() const { return mpBuffer; }
	virtual int GetSize() const { return mBytesInBuffer; }

private:
	// Use mTempBuffer when we need to hold a copy of the memory block,
	// and free it ourselves when done.
	CollectInBufferStream mTempBuffer;
	const char *mpBuffer;
	int mBytesInBuffer;
	int mReadPosition;
};

#endif // MEMBLOCKSTREAM__H

