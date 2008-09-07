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

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#ifdef WIN32
	#define INVALID_FILE INVALID_HANDLE_VALUE
	typedef HANDLE tOSFileHandle;
#else
	#define INVALID_FILE -1
	typedef int tOSFileHandle;
#endif

class FileStream : public IOStream
{
public:
	FileStream(const std::string& rFilename, 
#ifdef WIN32
		int flags = (O_RDONLY | O_BINARY),
#else
		int flags = O_RDONLY,
#endif
		int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH));

	// Ensure that const char * name doesn't end up as a handle
	// on Windows!

	FileStream(const char *pFilename, 
#ifdef WIN32
		int flags = (O_RDONLY | O_BINARY),
#else
		int flags = O_RDONLY,
#endif
		int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH));

	FileStream(tOSFileHandle FileDescriptor);
	
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
	tOSFileHandle mOSFileHandle;
	bool mIsEOF;
	FileStream(const FileStream &rToCopy) { /* do not call */ }
	void AfterOpen();

	// for debugging..
	std::string mFileName;
};


#endif // FILESTREAM__H


