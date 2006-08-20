// --------------------------------------------------------------------------
//
// File
//		Name:    DiscardStream.h
//		Purpose: Discards data written to it
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------

#ifndef DISCARDSTREAM__H
#define DISCARDSTREAM__H

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    DiscardStream
//		Purpose: Discards data written to it
//		Created: 2006/03/06
//
// --------------------------------------------------------------------------
class DiscardStream : public IOStream
{
public:
	DiscardStream();
	~DiscardStream();
private:
	// No copying
	DiscardStream(const DiscardStream &);
	DiscardStream(const IOStream &);
public:

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();
};

#endif // DISCARDSTREAM__H

