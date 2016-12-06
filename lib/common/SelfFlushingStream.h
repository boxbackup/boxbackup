// --------------------------------------------------------------------------
//
// File
//		Name:    SelfFlushingStream.h
//		Purpose: A stream wrapper that always flushes the underlying
//			 stream, to ensure protocol safety.
//		Created: 2008/08/20
//
// --------------------------------------------------------------------------

#ifndef SELFFLUSHINGSTREAM__H
#define SELFFLUSHINGSTREAM__H

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    SelfFlushingStream
//		Purpose: A stream wrapper that always flushes the underlying
//			 stream, to ensure protocol safety.
//		Created: 2008/08/20
//
// --------------------------------------------------------------------------
class SelfFlushingStream : public IOStream
{
public:
	SelfFlushingStream(IOStream &rSource)
	: mrSource(rSource) { }

	SelfFlushingStream(const SelfFlushingStream &rToCopy)
	: mrSource(rToCopy.mrSource) { }
	
	~SelfFlushingStream()
	{
		if(StreamDataLeft())
		{
			BOX_WARNING("Not all data was read from stream, "
				"discarding the rest");
		}

		Flush();
	}

private:
	// no copying from IOStream allowed
	SelfFlushingStream(const IOStream& rToCopy);
	
public:
	virtual int Read(void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		return mrSource.Read(pBuffer, NBytes, Timeout);
	}
	virtual pos_type BytesLeftToRead()
	{
		return mrSource.BytesLeftToRead();
	}
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		mrSource.Write(pBuffer, NBytes, Timeout);
	}
	using IOStream::Write;

	virtual bool StreamDataLeft()
	{
		return mrSource.StreamDataLeft();
	}
	virtual bool StreamClosed()
	{
		return mrSource.StreamClosed();
	}

private:
	IOStream &mrSource;
};

#endif // SELFFLUSHINGSTREAM__H

