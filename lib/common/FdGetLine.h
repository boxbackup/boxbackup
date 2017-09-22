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

#include "LineBuffer.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    FdGetLine
//		Purpose: Line based file descriptor reading
//		Created: 2003/07/24
//
// --------------------------------------------------------------------------
class FdGetLine : public LineBuffer
{
public:
	FdGetLine(int fd);
	virtual ~FdGetLine();
private:
	FdGetLine(const FdGetLine &forbidden);

public:
	// Call to detach, setting file pointer correctly to last bit read.
	// Only works for lseek-able file descriptors.
	void DetachFile();
	// if we read 0 bytes from an fd, it must be end of stream,
	// because we don't support timeouts
	virtual bool IsStreamDataLeft() { return false; }

protected:
	int ReadMore(int Timeout = IOStream::TimeOutInfinite);

private:
	int mFileHandle;
};

#endif // FDGETLINE__H

