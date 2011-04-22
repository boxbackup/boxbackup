// --------------------------------------------------------------------------
//
// File
//		Name:    GetLine.h
//		Purpose: Common base class for line based file descriptor reading
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------

#ifndef GETLINE__H
#define GETLINE__H

#include <string>

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
//		Name:    GetLine
//		Purpose: Common base class for line based file descriptor reading
//		Created: 2011/04/22
//
// --------------------------------------------------------------------------
class GetLine
{
protected:
	GetLine();

private:
	GetLine(const GetLine &rToCopy);

public:
	virtual bool IsEOF() {return mEOF;}
	int GetLineNumber() {return mLineNumber;}
	
protected:
	bool GetLineInternal(std::string &rOutput,
		bool Preprocess = false,
		int Timeout = IOStream::TimeOutInfinite);
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

#endif // GETLINE__H

