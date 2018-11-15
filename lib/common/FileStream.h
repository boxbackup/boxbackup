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

#include <string>

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
		NONE,
		UNKNOWN,
		SHARED,
		EXCLUSIVE,
	};

	static const int DEFAULT_MODE = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	FileStream(const std::string& rFilename,
		int flags = (O_RDONLY | O_BINARY),
		int mode = DEFAULT_MODE,
		lock_mode_t lock_mode = NONE,
		bool delete_asap = false,
		bool wait_for_lock = false);

	// Ensure that const char * name doesn't end up as a handle
	// on Windows!

	FileStream(const char *pFilename, 
		int flags = (O_RDONLY | O_BINARY),
		int mode = DEFAULT_MODE,
		lock_mode_t lock_mode = NONE,
		bool delete_asap = false,
		bool wait_for_lock = false);

	FileStream(tOSFileHandle FileDescriptor, const std::string& rFilename = "",
		bool delete_asap = false);

	static std::auto_ptr<FileStream> CreateTemporaryFile(const std::string& pattern,
		std::string temp_dir = "", int flags = O_BINARY, bool delete_asap = true);
	
	virtual ~FileStream();
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	using IOStream::Write;
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, seek_type SeekType);
	virtual void Truncate(IOStream::pos_type length);
	virtual void Close();
	
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	bool CompareWith(IOStream& rOther, int Timeout = IOStream::TimeOutInfinite);
	std::string ToString() const
	{
		return std::string("local file ") + mFileName;
	}
	const std::string GetFileName() const { return mFileName; }
	tOSFileHandle GetFileHandle() { return mOSFileHandle; }

private:
	tOSFileHandle mOSFileHandle;
	bool mIsEOF;
	FileStream(const FileStream &rToCopy) { /* do not call */ }
	int GetLockFlags(std::string* pMessage = NULL, bool wait_for_lock = false);
	void OpenFile(int flags, int mode, lock_mode_t lock_mode, bool delete_asap,
		bool wait_for_lock);
	void AfterOpen(bool delete_asap);

	// for debugging..
	std::string mFileName;
	lock_mode_t mLockMode;
};


#endif // FILESTREAM__H


