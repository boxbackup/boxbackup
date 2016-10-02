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

	// Move constructor:
	CollectInBufferStream(CollectInBufferStream& rOther)
	: mBuffer(rOther.mBuffer.Release()),
	  mBufferSize(rOther.mBufferSize),
	  mBytesInBuffer(rOther.mBytesInBuffer),
	  mReadPosition(rOther.mReadPosition),
	  mInWritePhase(rOther.mInWritePhase)
	{
		rOther.Reset();
	}

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const std::string& rBuffer,
		int Timeout = IOStream::TimeOutInfinite)
	{
		IOStream::Write(rBuffer, Timeout);
	}
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	void SetForReading();

	void Reset();

	void *GetBuffer() const;
	int GetSize() const;
	bool IsSetForReading() const {return !mInWritePhase;}

private:
	MemoryBlockGuard<char*> mBuffer;
	int mBufferSize;
	int mBytesInBuffer;
	int mReadPosition;
	bool mInWritePhase;
};

#endif // COLLECTINBUFFERSTREAM__H

