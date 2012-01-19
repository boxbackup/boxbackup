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
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	IOStream &mrSource;
	int mBytesLeftInCurrentBlock;
	bool mFinished;
};

#endif // PROTOCOLUNCERTAINSTREAM__H

