// --------------------------------------------------------------------------
//
// File
//		Name:    FdGetLine.h
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------

#ifndef FDGETLINE__H
#define FDGETLINE__H

#include <string>

#ifdef NDEBUG
	#define FDGETLINE_BUFFER_SIZE		1024
#else
	#define FDGETLINE_BUFFER_SIZE		4
#endif

// Just a very large upper bound for line size to avoid
// people sending lots of data over sockets and causing memory problems.
#define FDGETLINE_MAX_LINE_SIZE			(1024*256)

// --------------------------------------------------------------------------
//
// Class
//		Name:    FdGetLine
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
class FdGetLine
{
public:
	FdGetLine(int fd);
	~FdGetLine();
private:
	FdGetLine(const FdGetLine &rToCopy);

public:
	std::string GetLine(bool Preprocess = false);
	bool IsEOF() {return mEOF;}
	int GetLineNumber() {return mLineNumber;}
	
	// Call to detach, setting file pointer correctly to last bit read.
	// Only works for lseek-able file descriptors.
	void DetachFile();
	
private:
	char mBuffer[FDGETLINE_BUFFER_SIZE];
	int mFileHandle;
	int mLineNumber;
	int mBufferBegin;
	int mBytesInBuffer;
	bool mPendingEOF;
	bool mEOF;
};

#endif // FDGETLINE__H

