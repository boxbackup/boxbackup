// --------------------------------------------------------------------------
//
// File
//		Name:    RateLimitingStream.h
//		Purpose: Rate-limiting write-only wrapper around IOStreams
//		Created: 2011/01/11
//
// --------------------------------------------------------------------------

#ifndef RATELIMITINGSTREAM__H
#define RATELIMITINGSTREAM__H

#include "BoxTime.h"
#include "IOStream.h"

class RateLimitingStream : public IOStream
{
private:
	IOStream& mrSink;
	box_time_t mStartTime;
	uint64_t mTotalBytesRead;
	size_t mTargetBytesPerSecond;

public:
	RateLimitingStream(IOStream& rSink, size_t targetBytesPerSecond);
	virtual ~RateLimitingStream() { }

	// This is the only magic
	virtual int Read(void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);

	// Everything else is delegated to the sink
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		mrSink.Write(pBuffer, NBytes, Timeout);
	}
	using IOStream::Write;
	virtual pos_type BytesLeftToRead()
	{
		return mrSink.BytesLeftToRead();
	}
	virtual pos_type GetPosition() const
	{
		return mrSink.GetPosition();
	}
	virtual void Seek(pos_type Offset, seek_type SeekType)
	{
		mrSink.Seek(Offset, SeekType);
	}
	virtual void Flush(int Timeout = IOStream::TimeOutInfinite)
	{
		mrSink.Flush(Timeout);
	}
	virtual void Close()
	{
		mrSink.Close();
	}
	virtual bool StreamDataLeft()
	{
		return mrSink.StreamDataLeft();
	}
	virtual bool StreamClosed()
	{
		return mrSink.StreamClosed();
	}

private:
	RateLimitingStream(const RateLimitingStream &rToCopy) 
	: mrSink(rToCopy.mrSink) { /* do not call */ }
};

#endif // RATELIMITINGSTREAM__H
