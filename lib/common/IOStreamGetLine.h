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

#include "IOStream.h"

#ifdef NDEBUG
	#define IOSTREAMGETLINE_BUFFER_SIZE		1024
#else
	#define IOSTREAMGETLINE_BUFFER_SIZE		4
#endif

// Just a very large upper bound for line size to avoid
// people sending lots of data over sockets and causing memory problems.
#define IOSTREAMGETLINE_MAX_LINE_SIZE			(1024*256)

// --------------------------------------------------------------------------
//
// Class
//		Name:    IOStreamGetLine
//		Purpose: Line based stream reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
class IOStreamGetLine
{
public:
	IOStreamGetLine(IOStream &Stream);
	~IOStreamGetLine();
private:
	IOStreamGetLine(const IOStreamGetLine &rToCopy);

public:
	bool GetLine(std::string &rOutput, bool Preprocess = false, int Timeout = IOStream::TimeOutInfinite);
	bool IsEOF() {return mEOF;}
	int GetLineNumber() {return mLineNumber;}
	
	// Call to detach, setting file pointer correctly to last bit read.
	// Only works for lseek-able file descriptors.
	void DetachFile();
	
	// For doing interesting stuff with the remaining data...
	// Be careful with this!
	const void *GetBufferedData() const {return mBuffer + mBufferBegin;}
	int GetSizeOfBufferedData() const {return mBytesInBuffer - mBufferBegin;}
	
private:
	char mBuffer[IOSTREAMGETLINE_BUFFER_SIZE];
	IOStream &mrStream;
	int mLineNumber;
	int mBufferBegin;
	int mBytesInBuffer;
	bool mPendingEOF;
	bool mEOF;
	std::string mPendingString;
};

#endif // IOSTREAMGETLINE__H

