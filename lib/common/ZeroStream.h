// --------------------------------------------------------------------------
//
// File
//		Name:    ZeroStream.h
//		Purpose: An IOStream which returns all zeroes up to a certain
//			 size. It can optionally discard all data written to
//			 it as well.
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
	bool mDiscardWrites;

public:
	ZeroStream(IOStream::pos_type Size, bool DiscardWrites = false)
	: mSize(Size), mPosition(0), mDiscardWrites(DiscardWrites)
	{ }

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	using IOStream::Write;

	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, seek_type SeekType);
	virtual void Close();

	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	ZeroStream(const ZeroStream &rToCopy);
};

#endif // ZEROSTREAM__H


