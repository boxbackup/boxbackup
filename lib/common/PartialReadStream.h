// --------------------------------------------------------------------------
//
// File
//		Name:    PartialReadStream.h
//		Purpose: Read part of another stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#ifndef PARTIALREADSTREAM__H
#define PARTIALREADSTREAM__H

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    PartialReadStream
//		Purpose: Read part of another stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
class PartialReadStream : public IOStream
{
public:
	PartialReadStream(IOStream &rSource, pos_type BytesToRead);
	~PartialReadStream();
private:
	// no copying allowed
	PartialReadStream(const IOStream &);
	PartialReadStream(const PartialReadStream &);
	
public:
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	IOStream &mrSource;
	pos_type mBytesLeft;
};

#endif // PARTIALREADSTREAM__H

