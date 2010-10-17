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

#include "IOStream.h"

class StreamableMemBlock;
class CollectInBufferStream;

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
	MemBlockStream(const void *pBuffer, size_t Size);
	MemBlockStream(const StreamableMemBlock &rBlock);
	MemBlockStream(const CollectInBufferStream &rBuffer);
	MemBlockStream(const MemBlockStream &rToCopy);
	~MemBlockStream();
public:

	virtual size_t Read(void *pBuffer, size_t NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, size_t NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	const char *mpBuffer;
	size_t mBytesInBuffer;
	pos_type mReadPosition;
};

#endif // MEMBLOCKSTREAM__H

