// --------------------------------------------------------------------------
//
// File
//		Name:    BufferedWriteStream.h
//		Purpose: Buffering write-only wrapper around IOStreams
//		Created: 2010/09/13
//
// --------------------------------------------------------------------------

#ifndef BUFFEREDWRITESTREAM__H
#define BUFFEREDWRITESTREAM__H

#include "IOStream.h"

class BufferedWriteStream : public IOStream
{
private:
	IOStream& mrSink;
	char mBuffer[4096];
	int  mBufferPosition;

public:
	BufferedWriteStream(IOStream& rSource);
	virtual ~BufferedWriteStream() { Close(); }
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	using IOStream::Write;

	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, seek_type SeekType);
	virtual void Flush(int Timeout = IOStream::TimeOutInfinite);
	virtual void Close();
	
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	BufferedWriteStream(const BufferedWriteStream &rToCopy) 
	: mrSink(rToCopy.mrSink) { /* do not call */ }
};

#endif // BUFFEREDWRITESTREAM__H


