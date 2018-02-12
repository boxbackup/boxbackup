// --------------------------------------------------------------------------
//
// File
//		Name:    LineBuffer.h
//		Purpose: Common base class for line based file descriptor reading
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------

#ifndef LINEBUFFER__H
#define LINEBUFFER__H

#include <string>

#include "IOStream.h"

#ifdef BOX_RELEASE_BUILD
	#define GETLINE_BUFFER_SIZE		1024
#elif defined WIN32
	// need enough space for at least one unicode character 
	// in UTF-8 when calling console_read() from bbackupquery
	#define GETLINE_BUFFER_SIZE		5
#else
	#define GETLINE_BUFFER_SIZE		4
#endif

// Just a very large upper bound for line size to avoid
// people sending lots of data over sockets and causing memory problems.
#define GETLINE_MAX_LINE_SIZE			(1024*256)

// --------------------------------------------------------------------------
//
// Class
//		Name:    LineBuffer
//		Purpose: Common base class for line based file descriptor reading
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
class LineBuffer
{
protected:
	LineBuffer();

private:
	LineBuffer(const LineBuffer &forbidden);

public:
	virtual bool IsEOF() {return mEOF;}
	int GetLineNumber() {return mLineNumber;}
	virtual ~LineBuffer() { }
	std::string GetLine(bool Preprocess,
		int Timeout = IOStream::TimeOutInfinite);
	
protected:
	virtual int ReadMore(int Timeout = IOStream::TimeOutInfinite) = 0;
	virtual bool IsStreamDataLeft() = 0;

	char mBuffer[GETLINE_BUFFER_SIZE];
	int mLineNumber;
	int mBufferBegin;
	int mBytesInBuffer;
	bool mPendingEOF;
	std::string mPendingString;
	bool mEOF;
};

#endif // LINEBUFFER__H

