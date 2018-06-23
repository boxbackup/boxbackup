// --------------------------------------------------------------------------
//
// File
//		Name:    TeeStream.h
//		Purpose: A stream that writes to two streams at once
//		Created: 2018-06-18
//
// --------------------------------------------------------------------------

#ifndef TEESTREAM_H
#define TEESTREAM_H

#include "Exception.h"
#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    TeeStream
//		Purpose: A stream that writes to two streams at once
//		Created: 2018-06-18
//
// --------------------------------------------------------------------------

class TeeStream : public IOStream
{
private:
	TeeStream(const MD5DigestStream &rToCopy); /* forbidden */
	TeeStream& operator=(const MD5DigestStream &rToCopy); /* forbidden */
	bool mClosed;
	IOStream& mrOutputStream1;
	IOStream& mrOutputStream2;

public:
	TeeStream(IOStream& output_stream_1, IOStream& output_stream_2)
	: mClosed(false),
	  mrOutputStream1(output_stream_1),
	  mrOutputStream2(output_stream_2)
	{ }

	virtual int Read(void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual pos_type BytesLeftToRead()
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		mrOutputStream1.Write(pBuffer, NBytes, Timeout);
		mrOutputStream2.Write(pBuffer, NBytes, Timeout);
	}
	virtual void Write(const std::string& rBuffer,
		int Timeout = IOStream::TimeOutInfinite)
	{
		mrOutputStream1.Write(rBuffer, Timeout);
		mrOutputStream2.Write(rBuffer, Timeout);
	}
	virtual void WriteAllBuffered(int Timeout = IOStream::TimeOutInfinite) { }
	virtual pos_type GetPosition() const
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void Seek(pos_type Offset, int SeekType)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void Close()
	{
		mrOutputStream1.Close();
		mrOutputStream2.Close();
		mClosed = true;
	}

	// Has all data that can be read been read?
	virtual bool StreamDataLeft()
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	// Has the stream been closed (writing not possible)
	virtual bool StreamClosed()
	{
		return mClosed;
	}

	// Utility functions
	bool ReadFullBuffer(void *pBuffer, int NBytes, int *pNBytesRead,
		int Timeout = IOStream::TimeOutInfinite)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	IOStream::pos_type CopyStreamTo(IOStream &rCopyTo,
		int Timeout = IOStream::TimeOutInfinite, int BufferSize = 1024)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	void Flush(int Timeout = IOStream::TimeOutInfinite)
	{ }
	static int ConvertSeekTypeToOSWhence(int SeekType)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual std::string ToString() const
	{
		return "TeeStream";
	}
};

#endif // TEESTREAM_H

