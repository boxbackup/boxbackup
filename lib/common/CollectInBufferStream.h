// --------------------------------------------------------------------------
//
// File
//		Name:    CollectInBufferStream.h
//		Purpose: Collect data in a buffer, and then read it out.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#ifndef COLLECTINBUFFERSTREAM__H
#define COLLECTINBUFFERSTREAM__H

#include "IOStream.h"
#include "Guards.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    CollectInBufferStream
//		Purpose: Collect data in a buffer, and then read it out.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
class CollectInBufferStream : public IOStream
{
public:
	CollectInBufferStream();
	~CollectInBufferStream();
private:
	// No copying
	CollectInBufferStream(const CollectInBufferStream &);
	CollectInBufferStream(const IOStream &);
public:

	virtual size_t Read(void *pBuffer, size_t NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, size_t NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	void SetForReading();
	
	void Reset();
	
	void *GetBuffer() const;
	size_t GetSize() const;
	bool IsSetForReading() const {return !mInWritePhase;}

private:
	MemoryBlockGuard<char*> mBuffer;
	size_t mBufferSize;
	size_t mBytesInBuffer;
	pos_type mReadPosition;
	bool mInWritePhase;
};

#endif // COLLECTINBUFFERSTREAM__H

