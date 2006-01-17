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
//		Name:    NamedLock.cpp
//		Purpose: A global named lock, implemented as a lock file in file system
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#ifdef PLATFORM_LINUX
	#include <sys/file.h>
#endif // PLATFORM_LINUX
#ifdef PLATFORM_CYGWIN
	#include <sys/file.h>
#endif // PLATFORM_CYGWIN

#include "NamedLock.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::NamedLock()
//		Purpose: Constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
NamedLock::NamedLock()
	: mFileDescriptor(-1)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::~NamedLock()
//		Purpose: Destructor (automatically unlocks if locked)
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
NamedLock::~NamedLock()
{
	if(mFileDescriptor != -1)
	{
		ReleaseLock();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::TryAndGetLock(const char *, int)
//		Purpose: Trys to get a lock on the name in the file system.
//				 IMPORTANT NOTE: If a file exists with this name, it will be deleted.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
bool NamedLock::TryAndGetLock(const char *Filename, int mode)
{
	// Check
	if(mFileDescriptor != -1)
	{
		THROW_EXCEPTION(CommonException, NamedLockAlreadyLockingSomething)
	}

	// See if the lock can be got
#ifdef PLATFORM_open_NO_O_EXLOCK
	int fd = ::open(Filename, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if(fd == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	if(::flock(fd, LOCK_EX | LOCK_NB) != 0)
	{
		::close(fd);
		if(errno == EWOULDBLOCK)
		{
			return false;
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	}

	// Success
	mFileDescriptor = fd;

	return true;
#else
	int fd = ::open(Filename, O_WRONLY | O_NONBLOCK | O_CREAT | O_TRUNC | O_EXLOCK, mode);
	if(fd != -1)
	{
		// Got a lock, lovely
		mFileDescriptor = fd;
		return true;
	}
	
	// Failed. Why?
	if(errno != EWOULDBLOCK)
	{
		// Not the expected error
		THROW_EXCEPTION(CommonException, OSFileError)
	}

	return false;
#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    NamedLock::ReleaseLock()
//		Purpose: Release the lock. Exceptions if the lock is not held
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void NamedLock::ReleaseLock()
{
	// Got a lock?
	if(mFileDescriptor == -1)
	{
		THROW_EXCEPTION(CommonException, NamedLockNotHeld)
	}
	
	// Close the file
	if(::close(mFileDescriptor) != 0)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	// Mark as unlocked
	mFileDescriptor = -1;
}




