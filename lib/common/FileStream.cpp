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

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::FileStream(const char *, int, int)
//		Purpose: Constructor, opens file
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
FileStream::FileStream(const char *Filename, int flags, int mode)
#ifdef WIN32
	: mOSFileHandle(::openfile(Filename, flags, mode)),
#else
	: mOSFileHandle(::open(Filename, flags, mode)),
#endif
	  mIsEOF(false)
{
#ifdef WIN32
	if(mOSFileHandle == INVALID_HANDLE_VALUE)
#else
	if(mOSFileHandle < 0)
#endif
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
#ifdef WIN32
	this->fileName = Filename;
#endif
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
	  mIsEOF(false)
{
#ifdef WIN32
	if(mOSFileHandle == INVALID_HANDLE_VALUE)
#else
	if(mOSFileHandle < 0)
#endif
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
#ifdef WIN32
	this->fileName = "HANDLE";
#endif
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
	if(mOSFileHandle < 0)
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);
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

	if ( valid )
	{
		r = numBytesRead;
	}
	else
	{
		r = -1;
	}
#else
	int r = ::read(mOSFileHandle, pBuffer, NBytes);
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
	struct stat st;
	if(::fstat(mOSFileHandle, &st) != 0)
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

	if ( (res == 0) || (numBytesWritten != NBytes))
	{
		// DWORD err = GetLastError();
		THROW_EXCEPTION(CommonException, OSFileWriteError)
	}
#else
	if(::write(mOSFileHandle, pBuffer, NBytes) != NBytes)
	{
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

	if ( retVal == INVALID_SET_FILE_POINTER && (GetLastError() != NO_ERROR) )
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
	if(mOSFileHandle < 0)
	{
		THROW_EXCEPTION(CommonException, FileAlreadyClosed)
	}
#ifdef WIN32
	if(::CloseHandle(mOSFileHandle) == 0)
	{
		THROW_EXCEPTION(CommonException, OSFileCloseError)
	}
	mOSFileHandle = NULL;
	mIsEOF = true;
#else
	if(::close(mOSFileHandle) != 0)
	{
		THROW_EXCEPTION(CommonException, OSFileCloseError)
	}
	mOSFileHandle = -1;
	mIsEOF = true;
#endif
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

