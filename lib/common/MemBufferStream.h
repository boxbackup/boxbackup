// --------------------------------------------------------------------------
//
// File
//		Name:    MemBufferStream.h
//		Purpose: Stream to and from an encapsulated memory block
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------

#ifndef MEMBUFFERSTREAM__H
#define MEMBUFFERSTREAM__H

#include "IOStream.h"
#include "StreamableMemBlock.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    MemBufferStream
//		Purpose: Stream to and from an encapsulated memory block
//		Created: 2006/02/27
//
// --------------------------------------------------------------------------
class MemBufferStream : public IOStream
{
public:
	MemBufferStream();
	MemBufferStream(const StreamableMemBlock& rSource);

private:
	MemBufferStream(const MemBufferStream &rToCopy); // do not call

public:
	virtual ~MemBufferStream();
	
	// Timeout in milliseconds
	// Read may return 0 -- does not mean end of stream.
	typedef int64_t pos_type;
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();	// may return IOStream::SizeOfStreamUnknown (and will for most stream types)
	virtual void Write(const void *pBuffer, int NBytes);
	virtual void WriteAllBuffered();
	virtual pos_type GetPosition() const;
	virtual void Close();
	
	// Has all data that can be read been read?
	virtual bool StreamDataLeft();
	// Has the stream been closed (writing not possible)
	virtual bool StreamClosed();
	
	const StreamableMemBlock& GetBuffer() { return mBuffer; }
	
private:
	StreamableMemBlock mBuffer;
	pos_type mReadPosition;
	pos_type mWritePosition; // always equals buffer size
};


#endif // MEMBUFFERSTREAM__H


