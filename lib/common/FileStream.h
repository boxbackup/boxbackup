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

#if HAVE_DECL_O_EXLOCK
#	define BOX_LOCK_TYPE_O_EXLOCK
#elif defined BOX_OPEN_LOCK
#	define BOX_LOCK_TYPE_WIN32
#elif defined HAVE_FLOCK
// This is preferable to F_OFD_SETLK because no byte ranges are involved
#	define BOX_LOCK_TYPE_FLOCK
#elif HAVE_DECL_F_OFD_SETLK
// This is preferable to F_SETLK because it's non-reentrant
#	define BOX_LOCK_TYPE_F_OFD_SETLK
#elif HAVE_DECL_F_SETLK
// This is not ideal because it's reentrant, but better than a dumb lock
// (reentrancy only matters in tests; in real use it's as good as F_OFD_SETLK).
#	define BOX_LOCK_TYPE_F_SETLK
#else
// We have no other way to get a lock, so all we can do is fail if the
// file already exists, and take the risk of stale locks.
#	define BOX_LOCK_TYPE_DUMB
#endif

class FileStream : public IOStream
{
public:
	enum lock_mode_t {
		SHARED,
		EXCLUSIVE,
	};

	FileStream(const std::string& rFilename,
		int flags = (O_RDONLY | O_BINARY),
		int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
		lock_mode_t lock_mode = SHARED);

	// Ensure that const char * name doesn't end up as a handle
	// on Windows!

	FileStream(const char *pFilename, 
		int flags = (O_RDONLY | O_BINARY),
		int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),
		lock_mode_t lock_mode = SHARED);

	FileStream(tOSFileHandle FileDescriptor);
	
	virtual ~FileStream();
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	using IOStream::Write;
	virtual pos_type GetPosition() const;
	virtual void Seek(IOStream::pos_type Offset, int SeekType);
	virtual void Close();
	
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	bool CompareWith(IOStream& rOther, int Timeout = IOStream::TimeOutInfinite);
	std::string ToString() const
	{
		return std::string("local file ") + mFileName;
	}
	const std::string GetFileName() const { return mFileName; }

private:
	tOSFileHandle mOSFileHandle;
	bool mIsEOF;
	FileStream(const FileStream &rToCopy) { /* do not call */ }
	void OpenFile(int flags, int mode, lock_mode_t lock_mode);

	// for debugging..
	std::string mFileName;
};


#endif // FILESTREAM__H


