// --------------------------------------------------------------------------
//
// File
//		Name:    BufferedStream.h
//		Purpose: Buffering wrapper around IOStreams
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------

#ifndef BUFFEREDSTREAM__H
#define BUFFEREDSTREAM__H

#include "IOStream.h"

class BufferedStream : public IOStream
{
private:
	IOStream& mrSource;
	char mBuffer[4096];
	size_t  mBufferSize;
	size_t  mBufferPosition;

public:
	BufferedStream(IOStream& rSource);
	
	virtual size_t Read(void *pBuffer, size_t NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, size_t NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(IOStream::pos_type Offset, int SeekType);
	virtual void Close();
	
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	BufferedStream(const BufferedStream &rToCopy) 
	: mrSource(rToCopy.mrSource) { /* do not call */ }
};

#endif // BUFFEREDSTREAM__H


