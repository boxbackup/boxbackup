// --------------------------------------------------------------------------
//
// File
//		Name:    ByteCountingStream.h
//		Purpose: A stream wrapper that counts the number of bytes
//			 transferred through it.
//		Created: 2016/02/05
//
// --------------------------------------------------------------------------

#ifndef BYTECOUNTINGSTREAM__H
#define BYTECOUNTINGSTREAM__H

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    ByteCountingStream
//		Purpose: A stream wrapper that counts the number of bytes
//			 transferred through it.
//		Created: 2016/02/05
//
// --------------------------------------------------------------------------
class ByteCountingStream : public IOStream
{
public:
	ByteCountingStream(IOStream &underlying)
	: mrUnderlying(underlying),
	  mNumBytesRead(0),
	  mNumBytesWritten(0)
	{ }

	ByteCountingStream(const ByteCountingStream &rToCopy)
	: mrUnderlying(rToCopy.mrUnderlying),
	  mNumBytesRead(0),
	  mNumBytesWritten(0)
	{ }

private:
	// no copying from IOStream allowed
	ByteCountingStream(const IOStream& rToCopy);

public:
	virtual int Read(void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		int bytes_read = mrUnderlying.Read(pBuffer, NBytes, Timeout);
		mNumBytesRead += bytes_read;
		return bytes_read;
	}
	virtual pos_type BytesLeftToRead()
	{
		return mrUnderlying.BytesLeftToRead();
	}
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		mrUnderlying.Write(pBuffer, NBytes, Timeout);
		mNumBytesWritten += NBytes;
	}
	virtual bool StreamDataLeft()
	{
		return mrUnderlying.StreamDataLeft();
	}
	virtual bool StreamClosed()
	{
		return mrUnderlying.StreamClosed();
	}
	int64_t GetNumBytesRead() { return mNumBytesRead; }
	int64_t GetNumBytesWritten() { return mNumBytesWritten; }

private:
	IOStream &mrUnderlying;
	int64_t mNumBytesRead, mNumBytesWritten;
};

#endif // BYTECOUNTINGSTREAM__H

