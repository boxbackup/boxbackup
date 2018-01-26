// --------------------------------------------------------------------------
//
// File
//		Name:    FileStream.cpp
//		Purpose: IOStream interface to files
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>

#ifdef HAVE_FCNTL_G
	#include <fcntl.h>
#endif

#ifdef HAVE_SYS_FILE_H
	#include <sys/file.h>
#endif

#include "FileStream.h"
#include "CommonException.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::FileStream(const char *, int, int)
//		Purpose: Constructor, opens file
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
FileStream::FileStream(const std::string& mFileName, int flags, int mode,
	lock_mode_t lock_mode)
: mOSFileHandle(INVALID_FILE),
  mIsEOF(false),
  mFileName(mFileName)
{
	OpenFile(flags, mode, lock_mode);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::FileStream(const char *, int, int)
//		Purpose: Alternative constructor, takes a const char *,
//			 avoids const strings being interpreted as handles!
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
FileStream::FileStream(const char *pFilename, int flags, int mode,
	lock_mode_t lock_mode)
: mOSFileHandle(INVALID_FILE),
  mIsEOF(false),
  mFileName(pFilename)
{
	OpenFile(flags, mode, lock_mode);
}

void FileStream::OpenFile(int flags, int mode, lock_mode_t lock_mode)
{
	std::string lock_method_name, lock_message;

	if(lock_mode == EXCLUSIVE)
	{
#ifdef BOX_LOCK_TYPE_O_EXLOCK
		flags |= O_NONBLOCK | O_EXLOCK;
		lock_method_name = "O_EXLOCK";
#elif defined BOX_LOCK_TYPE_WIN32
		flags |= BOX_OPEN_LOCK;
		lock_method_name = "dwShareMode 0";
#elif defined BOX_LOCK_TYPE_F_OFD_SETLK
		lock_method_name = "F_OFD_SETLK, F_WRLCK";
#elif defined BOX_LOCK_TYPE_F_SETLK
		lock_method_name = "F_SETLK, F_WRLCK";
#elif defined BOX_LOCK_TYPE_FLOCK
		lock_method_name = "flock(LOCK_EX)";
#elif defined BOX_LOCK_TYPE_DUMB
		// We have no other way to get a lock, so this is equivalent to O_EXCL.
		flags |= O_EXCL;
		lock_method_name = "O_EXCL";
#else
#	error "Unknown locking type"
#endif
		lock_message = std::string("exclusively using ") + lock_method_name;
	}
	else
	{
#ifdef BOX_LOCK_TYPE_O_EXLOCK
		flags |= O_NONBLOCK | O_SHLOCK;
		lock_method_name = "O_SHLOCK";
#elif defined BOX_LOCK_TYPE_WIN32
		// no extra flags needed for FILE_SHARE_READ | FILE_SHARE_WRITE
		lock_method_name = "dwShareMode FILE_SHARE_READ | FILE_SHARE_WRITE";
#elif defined BOX_LOCK_TYPE_F_OFD_SETLK
		lock_method_name = "F_OFD_SETLK, F_RDLCK";
#elif defined BOX_LOCK_TYPE_F_SETLK
		lock_method_name = "F_SETLK, F_RDLCK";
#elif defined BOX_LOCK_TYPE_FLOCK
		lock_method_name = "flock(LOCK_SH)";
#elif defined BOX_LOCK_TYPE_DUMB
		lock_method_name = "no locking at all!";
#else
#	error "Unknown locking type"
#endif
		lock_message = std::string("shared using ") + lock_method_name;
	}

	BOX_TRACE("Trying to " << (mode & O_CREAT ? "create" : "open") << " " << mFileName << " " <<
		lock_message);

#ifdef WIN32
	mOSFileHandle = ::openfile(mFileName.c_str(), flags, mode);
#else
	mOSFileHandle = ::open(mFileName.c_str(), flags, mode);
#endif

	if(mOSFileHandle == INVALID_FILE)
	{
		// Failed to open the file. What's the reason? The errno which indicates a lock
		// conflict depends on the locking method.

#ifdef BOX_LOCK_TYPE_O_EXLOCK
		if(errno == EWOULDBLOCK)
#elif defined BOX_LOCK_TYPE_WIN32
		if(errno == EBUSY)
#elif defined BOX_LOCK_TYPE_DUMB
		if(errno == EEXIST)
#else // F_OFD_SETLK, F_SETLK or FLOCK
		if(false)
#endif
		{
			// We failed to lock the file, which means that it's locked by someone else.
			// Need to throw a specific exception that's expected by NamedLock, which
			// should just return false in this case (and only this case).
			THROW_EXCEPTION_MESSAGE(CommonException, FileLockingConflict,
				BOX_FILE_MESSAGE(mFileName, "File already locked by another process"));
		}
		else if(errno == EACCES)
		{
			THROW_EMU_ERROR(BOX_FILE_MESSAGE(mFileName, "Failed to open file"),
				CommonException, AccessDenied);
		}
		else
		{
			THROW_EMU_ERROR(BOX_FILE_MESSAGE(mFileName, "Failed to open file"),
				CommonException, OSFileOpenError);
		}
	}

	bool lock_failed = false;

#ifdef BOX_LOCK_TYPE_FLOCK
	BOX_TRACE("Trying to lock " << mFileName << " " << lock_message);
	if(::flock(mOSFileHandle, (lock_mode == SHARED ? LOCK_SH : LOCK_EX) | LOCK_NB) != 0)
	{
		Close();

		if(errno == EWOULDBLOCK)
		{
			lock_failed = true;
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to lock lockfile " << lock_method_name,
				mFileName, CommonException, OSFileError);
		}
	}
#elif defined BOX_LOCK_TYPE_F_SETLK || defined BOX_LOCK_TYPE_F_OFD_SETLK
	struct flock desc;
	desc.l_type = (lock_mode == SHARED ? F_RDLCK : F_WRLCK);
	desc.l_whence = SEEK_SET;
	desc.l_start = 0;
	desc.l_len = 0;
	desc.l_pid = 0;
	BOX_TRACE("Trying to lock " << mFileName << " " << lock_message);
#	if defined BOX_LOCK_TYPE_F_OFD_SETLK
	if(::fcntl(mOSFileHandle, F_OFD_SETLK, &desc) != 0)
#	else // BOX_LOCK_TYPE_F_SETLK
	if(::fcntl(mOSFileHandle, F_SETLK, &desc) != 0)
#	endif
	{
		Close();

		if(errno == EAGAIN)
		{
			lock_failed = true;
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to lock lockfile " << lock_method_name,
				mFileName, CommonException, OSFileError);
		}
	}
#endif

	if(lock_failed)
	{
		// We failed to lock the file, which means that it's locked by someone else.
		// Need to throw a specific exception that's expected by NamedLock, which
		// should just return false in this case (and only this case).
		THROW_EXCEPTION_MESSAGE(CommonException, FileLockingConflict,
			BOX_FILE_MESSAGE(mFileName, "File already locked by another process"));
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::FileStream(tOSFileHandle)
//		Purpose: Constructor, using existing file descriptor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
FileStream::FileStream(tOSFileHandle FileDescriptor)
	: mOSFileHandle(FileDescriptor),
	  mIsEOF(false),
	  mFileName("HANDLE")
{
	if(mOSFileHandle == INVALID_FILE)
	{
		BOX_ERROR("FileStream: called with invalid file handle");
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
}

#if 0
// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::FileStream(const FileStream &)
//		Purpose: Copy constructor, creates a duplicate of the file handle
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
FileStream::FileStream(const FileStream &rToCopy)
	: mOSFileHandle(::dup(rToCopy.mOSFileHandle)),
	  mIsEOF(rToCopy.mIsEOF)
{
#ifdef WIN32
	if(mOSFileHandle == INVALID_HANDLE_VALUE)
#else
	if(mOSFileHandle < 0)
#endif
	{
		BOX_ERROR("FileStream: copying unopened file");
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
}
#endif // 0

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::~FileStream()
//		Purpose: Destructor, closes file
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
FileStream::~FileStream()
{
	if(mOSFileHandle != INVALID_FILE)
	{
		Close();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::Read(void *, int)
//		Purpose: Reads bytes from the file
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
int FileStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	if(mOSFileHandle == INVALID_FILE)
	{
		THROW_EXCEPTION(CommonException, FileClosed)
	}

#ifdef WIN32
	int r;
	DWORD numBytesRead = 0;
	BOOL valid = ReadFile(
		this->mOSFileHandle,
		pBuffer,
		NBytes,
		&numBytesRead,
		NULL
		);

	if(valid)
	{
		r = numBytesRead;
	}
	else if(GetLastError() == ERROR_BROKEN_PIPE)
	{
		r = 0;
	}
	else
	{
		THROW_WIN_FILE_ERROR("Failed to read from file", mFileName,
			CommonException, OSFileReadError);
	}

	if(r == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileReadError)
	}
#else
	int r = ::read(mOSFileHandle, pBuffer, NBytes);
	if(r == -1)
	{
		THROW_SYS_FILE_ERROR("Failed to read from file", mFileName,
			CommonException, OSFileReadError);
	}
#endif

	if(r == 0)
	{
		mIsEOF = true;
	}
	
	return r;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::BytesLeftToRead()
//		Purpose: Returns number of bytes to read (may not be most efficient function ever)
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
IOStream::pos_type FileStream::BytesLeftToRead()
{
	EMU_STRUCT_STAT st;
	if(EMU_FSTAT(mOSFileHandle, &st) != 0)
	{
		BOX_LOG_SYS_ERROR(BOX_FILE_MESSAGE("Failed to stat file", mFileName));
	}
	
	return st.st_size - GetPosition();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::Write(void *, int)
//		Purpose: Writes bytes to the file
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void FileStream::Write(const void *pBuffer, int NBytes, int Timeout)
{
	if(mOSFileHandle == INVALID_FILE)
	{
		THROW_EXCEPTION(CommonException, FileClosed)
	}

#ifdef WIN32
	DWORD numBytesWritten = 0;
	BOOL res = WriteFile(
		this->mOSFileHandle,
		pBuffer,
		NBytes,
		&numBytesWritten,
		NULL
		);

	if ((res == 0) || (numBytesWritten != (DWORD)NBytes))
	{
		THROW_WIN_FILE_ERROR("Failed to write to file", mFileName,
			CommonException, OSFileWriteError);
	}
#else
	if(::write(mOSFileHandle, pBuffer, NBytes) != NBytes)
	{
		THROW_SYS_FILE_ERROR("Failed to write to file", mFileName,
			CommonException, OSFileWriteError);
	}
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::GetPosition()
//		Purpose: Get position in stream
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
IOStream::pos_type FileStream::GetPosition() const
{
	if(mOSFileHandle == INVALID_FILE)
	{
		THROW_EXCEPTION(CommonException, FileClosed)
	}

#ifdef WIN32
	LARGE_INTEGER conv;
	conv.HighPart = 0;
	conv.LowPart = SetFilePointer(this->mOSFileHandle, 0, &conv.HighPart, FILE_CURRENT);

	if(conv.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		THROW_WIN_FILE_ERROR("Failed to seek in file", mFileName,
			CommonException, OSFileError);
	}

	return (IOStream::pos_type)conv.QuadPart;
#else // ! WIN32
	off_t p = ::lseek(mOSFileHandle, 0, SEEK_CUR);
	if(p == -1)
	{
		THROW_SYS_FILE_ERROR("Failed to seek in file", mFileName,
			CommonException, OSFileError);
	}
	
	return (IOStream::pos_type)p;
#endif // WIN32
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::Seek(pos_type, int)
//		Purpose: Seeks within file, as lseek
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void FileStream::Seek(IOStream::pos_type Offset, int SeekType)
{
	if(mOSFileHandle == INVALID_FILE)
	{
		THROW_EXCEPTION(CommonException, FileClosed)
	}

#ifdef WIN32
	LARGE_INTEGER conv;
	conv.QuadPart = Offset;
	DWORD retVal = SetFilePointer(this->mOSFileHandle, conv.LowPart, &conv.HighPart, ConvertSeekTypeToOSWhence(SeekType));

	if(retVal == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		THROW_WIN_FILE_ERROR("Failed to seek in file", mFileName,
			CommonException, OSFileError);
	}
#else // ! WIN32
	if(::lseek(mOSFileHandle, Offset, ConvertSeekTypeToOSWhence(SeekType)) == -1)
	{
		THROW_SYS_FILE_ERROR("Failed to seek in file", mFileName,
			CommonException, OSFileError);
	}
#endif // WIN32

	// Not end of file any more!
	mIsEOF = false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::Close()
//		Purpose: Closes the underlying file
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void FileStream::Close()
{
	if(mOSFileHandle == INVALID_FILE)
	{
		THROW_EXCEPTION(CommonException, FileAlreadyClosed)
	}

#ifdef WIN32
	if(::CloseHandle(mOSFileHandle) == 0)
	{
		THROW_WIN_FILE_ERROR("Failed to close file", mFileName,
			CommonException, OSFileCloseError);
	}
#else // ! WIN32
	if(::close(mOSFileHandle) != 0)
	{
		THROW_SYS_FILE_ERROR("Failed to close file", mFileName,
			CommonException, OSFileCloseError);
	}
#endif // WIN32

	mOSFileHandle = INVALID_FILE;
	mIsEOF = true;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::StreamDataLeft()
//		Purpose: Any data left to write?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool FileStream::StreamDataLeft()
{
	return !mIsEOF;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::StreamClosed()
//		Purpose: Is the stream closed?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool FileStream::StreamClosed()
{
	return (mOSFileHandle == INVALID_FILE);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::CompareWith(IOStream&, int)
//		Purpose: Compare bytes in this file with other stream's data
//		Created: 2009/01/03
//
// --------------------------------------------------------------------------
bool FileStream::CompareWith(IOStream& rOther, int Timeout)
{
	// Size
	IOStream::pos_type mySize = BytesLeftToRead();
	IOStream::pos_type otherSize = 0;
	
	// Test the contents
	char buf1[2048];
	char buf2[2048];
	while(StreamDataLeft() && rOther.StreamDataLeft())
	{
		int readSize = rOther.Read(buf1, sizeof(buf1), Timeout);
		otherSize += readSize;
		
		if(Read(buf2, readSize) != readSize ||
			::memcmp(buf1, buf2, readSize) != 0)
		{
			return false;
		}
	}

	// Check read all the data from the server and file -- can't be
	// equal if local and remote aren't the same length. Can't use
	// StreamDataLeft() test on local file, because if it's the same
	// size, it won't know it's EOF yet.
	
	if(rOther.StreamDataLeft() || otherSize != mySize)
	{
		return false;
	}

	return true;
}
