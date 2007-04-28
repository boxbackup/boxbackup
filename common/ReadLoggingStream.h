// --------------------------------------------------------------------------
//
// File
//		Name:    ReadLoggingStream.h
//		Purpose: Wrapper around IOStreams that logs read progress
//		Created: 2007/01/16
//
// --------------------------------------------------------------------------

#ifndef READLOGGINGSTREAM__H
#define READLOGGINGSTREAM__H

#include "IOStream.h"
#include "BoxTime.h"

class ReadLoggingStream : public IOStream
{
private:
	IOStream& mrSource;
	IOStream::pos_type mOffset, mLength, mTotalRead;
	box_time_t mStartTime;

public:
	ReadLoggingStream(IOStream& rSource);
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(IOStream::pos_type Offset, int SeekType);
	virtual void Close();
	
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	ReadLoggingStream(const ReadLoggingStream &rToCopy) 
	: mrSource(rToCopy.mrSource) { /* do not call */ }
};

#endif // READLOGGINGSTREAM__H


