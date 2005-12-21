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
	MemBlockStream(const void *pBuffer, int Size);
	MemBlockStream(const StreamableMemBlock &rBlock);
	MemBlockStream(const CollectInBufferStream &rBuffer);
	MemBlockStream(const MemBlockStream &rToCopy);
	~MemBlockStream();
public:

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	const char *mpBuffer;
	int mBytesInBuffer;
	pos_type mReadPosition;
};

#endif // MEMBLOCKSTREAM__H

