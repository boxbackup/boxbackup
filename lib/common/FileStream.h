// --------------------------------------------------------------------------
//
// File
//		Name:    FileStream.h
//		Purpose: FileStream interface to files
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef FILESTREAM__H
#define FILESTREAM__H

#include "IOStream.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef WIN32
#define INVALID_FILE NULL
#else
#define INVALID_FILE -1
#endif

class FileStream : public IOStream
{
public:
#ifdef WIN32
	FileStream(const char *Filename, int flags = (O_RDONLY | O_BINARY), int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH));
	FileStream(HANDLE FileDescriptor);
#else
	FileStream(const char *Filename, int flags = O_RDONLY, int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH));
	FileStream(int FileDescriptor);
	FileStream(const FileStream &rToCopy);
#endif
	
	virtual ~FileStream();
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(IOStream::pos_type Offset, int SeekType);
	virtual void Close();
	
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
#ifdef WIN32
	HANDLE mOSFileHandle;
	//for debugging..
	std::string fileName;
#else
	int mOSFileHandle;
#endif
	bool mIsEOF;
};


#endif // FILESTREAM__H


