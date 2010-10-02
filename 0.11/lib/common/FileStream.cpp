// --------------------------------------------------------------------------
//
// File
//		Name:    FileStream.cpp
//		Purpose: IOStream interface to files
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "FileStream.h"
#include "CommonException.h"
#include "Logging.h"

#include <errno.h>

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::FileStream(const char *, int, int)
//		Purpose: Constructor, opens file
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
FileStream::FileStream(const std::string& rFilename, int flags, int mode)
#ifdef WIN32
	: mOSFileHandle(::openfile(rFilename.c_str(), flags, mode)),
#else
	: mOSFileHandle(::open(rFilename.c_str(), flags, mode)),
#endif
	  mIsEOF(false),
	  mFileName(rFilename)
{
	AfterOpen();
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
FileStream::FileStream(const char *pFilename, int flags, int mode)
#ifdef WIN32
	: mOSFileHandle(::openfile(pFilename, flags, mode)),
#else
	: mOSFileHandle(::open(pFilename, flags, mode)),
#endif
	  mIsEOF(false),
	  mFileName(pFilename)
{
	AfterOpen();
}

void FileStream::AfterOpen()
{
#ifdef WIN32
	if(mOSFileHandle == INVALID_HANDLE_VALUE)
#else
	if(mOSFileHandle < 0)
#endif
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);

		#ifdef WIN32
		BOX_LOG_WIN_WARNING_NUMBER("Failed to open file: " <<
			mFileName, winerrno);
		#else
		BOX_LOG_SYS_WARNING("Failed to open file: " <<
			mFileName);
		#endif

		if(errno == EACCES)
		{
			THROW_EXCEPTION(CommonException, AccessDenied)
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileOpenError)
		}
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
#ifdef WIN32
	if(mOSFileHandle == INVALID_HANDLE_VALUE)
#else
	if(mOSFileHandle < 0)
#endif
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);
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
		MEMLEAKFINDER_NOT_A_LEAK(this);
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
		BOX_LOG_WIN_ERROR("Failed to read from file: " << mFileName);
		r = -1;
	}
#else
	int r = ::read(mOSFileHandle, pBuffer, NBytes);
	if(r == -1)
	{
		BOX_LOG_SYS_ERROR("Failed to read from file: " << mFileName);
	}
#endif

	if(r == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileReadError)
	}

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
		THROW_EXCEPTION(CommonException, OSFileError)
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
void FileStream::Write(const void *pBuffer, int NBytes)
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
		// DWORD err = GetLastError();
		THROW_EXCEPTION(CommonException, OSFileWriteError)
	}
#else
	if(::write(mOSFileHandle, pBuffer, NBytes) != NBytes)
	{
		BOX_LOG_SYS_ERROR("Failed to write to file: " << mFileName);
		THROW_EXCEPTION(CommonException, OSFileWriteError)
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
	conv.LowPart = 0;

	conv.LowPart = SetFilePointer(this->mOSFileHandle, 0, &conv.HighPart, FILE_CURRENT);

	return (IOStream::pos_type)conv.QuadPart;
#else // ! WIN32
	off_t p = ::lseek(mOSFileHandle, 0, SEEK_CUR);
	if(p == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
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
		THROW_EXCEPTION(CommonException, OSFileError)
	}
#else // ! WIN32
	if(::lseek(mOSFileHandle, Offset, ConvertSeekTypeToOSWhence(SeekType)) == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
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
#else
	if(::close(mOSFileHandle) != 0)
#endif
	{
		THROW_EXCEPTION(CommonException, OSFileCloseError)
	}

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
	return mIsEOF;
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
