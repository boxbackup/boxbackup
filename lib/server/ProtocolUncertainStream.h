// --------------------------------------------------------------------------
//
// File
//		Name:    ProtocolUncertainStream.h
//		Purpose: Read part of another stream
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------

#ifndef PROTOCOLUNCERTAINSTREAM__H
#define PROTOCOLUNCERTAINSTREAM__H

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    ProtocolUncertainStream
//		Purpose: Read part of another stream
//		Created: 2003/12/05
//
// --------------------------------------------------------------------------
class ProtocolUncertainStream : public IOStream
{
public:
	ProtocolUncertainStream(IOStream &rSource);
	~ProtocolUncertainStream();
private:
	// no copying allowed
	ProtocolUncertainStream(const IOStream &);
	ProtocolUncertainStream(const ProtocolUncertainStream &);
	
public:
	virtual size_t Read(void *pBuffer, size_t NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, size_t NBytes);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	IOStream &mrSource;
	size_t mBytesLeftInCurrentBlock;
	bool mFinished;
};

#endif // PROTOCOLUNCERTAINSTREAM__H

