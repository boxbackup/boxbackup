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
#ifdef HAVE_FLOCK
	#include <sys/file.h>
#endif

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
#if HAVE_DECL_O_EXLOCK
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
#else
	int fd = ::open(Filename, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if(fd == -1)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}

#ifdef HAVE_FLOCK
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
#else
	struct flock desc;
	desc.l_type = F_WRLCK;
	desc.l_whence = SEEK_SET;
	desc.l_start = 0;
	desc.l_len = 0;
	if(::fcntl(fd, F_SETLK, &desc) != 0)
	{
		::close(fd);
		if(errno == EAGAIN)
		{
			return false;
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	}
#endif

	// Success
	mFileDescriptor = fd;

	return true;
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




