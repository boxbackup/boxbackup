// --------------------------------------------------------------------------
//
// File
//		Name:    ZeroStream.h
//		Purpose: An IOStream which returns all zeroes up to a certain size
//		Created: 2007/04/28
//
// --------------------------------------------------------------------------

#ifndef ZEROSTREAM__H
#define ZEROSTREAM__H

#include "IOStream.h"

class ZeroStream : public IOStream
{
private:
	IOStream::pos_type mSize, mPosition;

public:
	ZeroStream(IOStream::pos_type mSize);
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(IOStream::pos_type Offset, int SeekType);
	virtual void Close();
	
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	ZeroStream(const ZeroStream &rToCopy);
};

#endif // ZEROSTREAM__H


