// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <new>

#include "CommonException.h"

#include "MemLeakFindOn.h"

template <int flags = O_RDONLY, int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)>
class FileHandleGuard
{
public:
	FileHandleGuard(const char *filename)
		: mOSFileHandle(::open(filename, flags, mode))
	{
		if(mOSFileHandle < 0)
		{
			THROW_EXCEPTION(CommonException, OSFileOpenError)
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
		: mpBlock(::malloc(BlockSize))
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
	
	void Resize(int NewSize)
	{
		void *ptrn = ::realloc(mpBlock, NewSize);
		if(ptrn == 0)
		{
			throw std::bad_alloc();
		}
		mpBlock = ptrn;
	}
	
private:
	void *mpBlock;
};

#include "MemLeakFindOff.h"

#endif // GUARDS__H

