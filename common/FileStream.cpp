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
	: mOSFileHandle(::open(Filename, flags, mode)),
	  mIsEOF(false)
{
	if(mOSFileHandle < 0)
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    FileStream::FileStream(int)
//		Purpose: Constructor, using existing file descriptor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
FileStream::FileStream(int FileDescriptor)
	: mOSFileHandle(FileDescriptor),
	  mIsEOF(false)
{
	if(mOSFileHandle < 0)
	{
		MEMLEAKFINDER_NOT_A_LEAK(this);
		THROW_EXCEPTION(CommonException, OSFileOpenError)
	}
}


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
	if(mOSFileHandle >= 0)
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
	if(mOSFileHandle == -1) {THROW_EXCEPTION(CommonException, FileClosed)}
	int r = ::read(mOSFileHandle, pBuffer, NBytes);
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
	if(mOSFileHandle == -1) {THROW_EXCEPTION(CommonException, FileClosed)}
	if(::write(mOSFileHandle, pBuffer, NBytes) != NBytes)
	{
		THROW_EXCEPTION(CommonException, OSFileWriteError)
	}
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
	if(mOSFileHandle == -1) {THROW_EXCEPTION(CommonException, FileClosed)}
	off_t p = ::lseek(mOSFileHandle, 0, SEEK_CUR);
	if(p == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	
	return (IOStream::pos_type)p;
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
	if(mOSFileHandle == -1) {THROW_EXCEPTION(CommonException, FileClosed)}
	if(::lseek(mOSFileHandle, Offset, ConvertSeekTypeToOSWhence(SeekType)) == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	
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
	if(::close(mOSFileHandle) != 0)
	{
		THROW_EXCEPTION(CommonException, OSFileCloseError)
	}
	mOSFileHandle = -1;
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

