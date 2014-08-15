// --------------------------------------------------------------------------
//
// File
//		Name:    IOStreamGetLine.h
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------

#ifndef IOSTREAMGETLINE__H
#define IOSTREAMGETLINE__H

#include <string>

#include "GetLine.h"
#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    IOStreamGetLine
//		Purpose: Line based stream reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
class IOStreamGetLine : public GetLine
{
public:
	IOStreamGetLine(IOStream &Stream);
	virtual ~IOStreamGetLine();
private:
	IOStreamGetLine(const IOStreamGetLine &rToCopy);

public:
	bool GetLine(std::string &rOutput, bool Preprocess = false, int Timeout = IOStream::TimeOutInfinite);
	
	// Call to detach, setting file pointer correctly to last bit read.
	// Only works for lseek-able file descriptors.
	void DetachFile();

	virtual bool IsStreamDataLeft()
	{
		return mrStream.StreamDataLeft();
	}

	// For doing interesting stuff with the remaining data...
	// Be careful with this!
	const void *GetBufferedData() const {return mBuffer + mBufferBegin;}
	int GetSizeOfBufferedData() const {return mBytesInBuffer - mBufferBegin;}
	void IgnoreBufferedData(int BytesToIgnore);
	IOStream &GetUnderlyingStream() {return mrStream;}

protected:
	int ReadMore(int Timeout = IOStream::TimeOutInfinite);

private:
	IOStream &mrStream;
};

#endif // IOSTREAMGETLINE__H

