// --------------------------------------------------------------------------
//
// File
//		Name:    Guards.h
//		Purpose: Classes which ensure things are closed/deleted properly when
//				 going out of scope. Easy exception proof code, etc
//		Created: 2003/07/12
//
// --------------------------------------------------------------------------

#ifndef GUARDS__H
#define GUARDS__H

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <new>

#include "CommonException.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

template <int flags = O_RDONLY | O_BINARY, int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)>
class FileHandleGuard
{
public:
	FileHandleGuard(const std::string& rFilename)
		: mOSFileHandle(::open(rFilename.c_str(), flags, mode))
	{
		if(mOSFileHandle < 0)
		{
			THROW_SYS_FILE_ERROR("Failed to open file", rFilename,
				CommonException, OSFileOpenError);
		}
	}
	
	~FileHandleGuard()
	{
		if(mOSFileHandle >= 0)
		{
			Close();
		}
	}
	
	void Close()
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
	}
	
	operator int() const
	{
		return mOSFileHandle;
	}

private:
	int mOSFileHandle;
};

template<typename type>
class MemoryBlockGuard
{
public:
	MemoryBlockGuard(int BlockSize)
	: mpBlock(::malloc(BlockSize)),
	  mBlockSize(BlockSize)
	{
		if(mpBlock == 0)
		{
			throw std::bad_alloc();
		}
	}

	MemoryBlockGuard(void *pBlock)
	: mpBlock(pBlock)
	{
		if(mpBlock == 0)
		{
			throw std::bad_alloc();
		}
	}
	
	~MemoryBlockGuard()
	{
		free(mpBlock);
	}
	
	operator type() const
	{
		return (type)mpBlock;
	}
	
	type GetPtr() const
	{
		return (type)mpBlock;
	}
	
	int GetSize() const
	{
		return mBlockSize;
	}

	void Resize(int NewSize)
	{
		void *ptrn = ::realloc(mpBlock, NewSize);
		if(ptrn == 0)
		{
			throw std::bad_alloc();
		}
		mpBlock = ptrn;
	}

	void* Release()
	{
		void* pBlock = mpBlock;
		mpBlock = ::malloc(mBlockSize);
		if(mpBlock == 0)
		{
			throw std::bad_alloc();
		}
		return pBlock;
	}
	
private:
	void *mpBlock;
	int mBlockSize;
};

#include "MemLeakFindOff.h"

#endif // GUARDS__H

