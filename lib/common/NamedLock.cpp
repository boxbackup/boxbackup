// --------------------------------------------------------------------------
//
// File
//		Name:    NamedLock.cpp
//		Purpose: A global named lock, implemented as a lock file in
//			 file system
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#ifdef HAVE_FLOCK
	#include <sys/file.h>
#endif

#include "CommonException.h"
#include "NamedLock.h"
#include "Utils.h"

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
//		Purpose: Tries to get a lock on the name in the file system.
//			 IMPORTANT NOTE: If a file exists with this name, it
//			 will be deleted.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
bool NamedLock::TryAndGetLock(const std::string& rFilename, int mode)
{
	// Check
	if(mFileDescriptor != -1)
	{
		THROW_EXCEPTION(CommonException, NamedLockAlreadyLockingSomething)
	}

	mFileName = rFilename;

	// See if the lock can be got
    int flags = O_WRONLY | O_CREAT | O_SYNC;

#if HAVE_DECL_O_EXLOCK
	flags |= O_NONBLOCK | O_EXLOCK;
	BOX_TRACE("Trying to create lockfile " << rFilename << " using O_EXLOCK");
#elif !HAVE_DECL_F_SETLK && !defined HAVE_FLOCK
	// We have no other way to get a lock, so all we can do is fail if
	// the file already exists, and take the risk of stale locks.
	flags |= O_EXCL;
	BOX_TRACE("Trying to create lockfile " << rFilename << " using O_EXCL");
#else
	BOX_TRACE("Trying to create lockfile " << rFilename << " without special flags");
#endif

	int fd = ::open(rFilename.c_str(), flags, mode);
	if(fd == -1)
#if HAVE_DECL_O_EXLOCK
	{ // if()
		if(errno == EWOULDBLOCK)
		{
			// Lockfile already exists, and we tried to open it
			// exclusively, which means we failed to lock it.
			BOX_NOTICE("Failed to lock lockfile with O_EXLOCK: " << rFilename
				<< ": already locked by another process?");
			return false;
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to open lockfile with O_EXLOCK",
				rFilename, CommonException, OSFileError);
		}
	}
#else // !HAVE_DECL_O_EXLOCK
	{ // if()
		if(errno == EEXIST && (flags & O_EXCL))
		{
			// Lockfile already exists, and we tried to open it
			// exclusively, which means we failed to lock it.
			BOX_NOTICE("Failed to lock lockfile with O_EXCL: " << rFilename
				<< ": already locked by another process?");
			return false;
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to open lockfile with O_EXCL",
				rFilename, CommonException, OSFileError);
		}
	}

	try
	{
# ifdef HAVE_FLOCK
		BOX_TRACE("Trying to lock lockfile " << rFilename << " using flock()");
		if(::flock(fd, LOCK_EX | LOCK_NB) != 0)
		{
			if(errno == EWOULDBLOCK)
			{
				::close(fd);
				BOX_NOTICE("Failed to lock lockfile with flock(): " << rFilename
					<< ": already locked by another process");
				return false;
			}
			else
			{
				THROW_SYS_FILE_ERROR("Failed to lock lockfile with flock()",
					rFilename, CommonException, OSFileError);
			}
		}
# elif HAVE_DECL_F_SETLK
		struct flock desc;
		desc.l_type = F_WRLCK;
		desc.l_whence = SEEK_SET;
		desc.l_start = 0;
		desc.l_len = 0;
		BOX_TRACE("Trying to lock lockfile " << rFilename << " using fcntl()");
		if(::fcntl(fd, F_SETLK, &desc) != 0)
		{
			if(errno == EAGAIN)
			{
				::close(fd);
				BOX_NOTICE("Failed to lock lockfile with fcntl(): " << rFilename
					<< ": already locked by another process");
				return false;
			}
			else
			{
				THROW_SYS_FILE_ERROR("Failed to lock lockfile with fcntl()",
					rFilename, CommonException, OSFileError);
			}
		}
# endif
	}
	catch(BoxException &e)
	{
		::close(fd);
		BOX_NOTICE("Failed to lock lockfile " << rFilename << ": " << e.what());
		throw;
	}
#endif // HAVE_DECL_O_EXLOCK

	if(!FileExists(rFilename))
	{
		BOX_ERROR("Locked lockfile " << rFilename << ", but lockfile no longer "
			"exists, bailing out");
		::close(fd);
		return false;
	}

	// Success
	mFileDescriptor = fd;
	BOX_TRACE("Successfully locked lockfile " << rFilename);

    // write pid in our lock
    std::string pidStr;          // string which will contain the result
    std::ostringstream convert;   // stream used for the conversion
    convert << getpid();
    pidStr=convert.str();
    ::ftruncate(fd, 0);
    ::write(fd, pidStr.c_str(), (size_t)pidStr.length());

	return true;
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

#ifndef WIN32
	// Delete the file. We need to do this before closing the filehandle, 
	// if we used flock() or fcntl() to lock it, otherwise someone could
	// acquire the lock, release and delete it between us closing (and
	// hence releasing) and deleting it, and we'd fail when it came to
	// deleting the file. This happens in tests much more often than
	// you'd expect!
	//
	// This doesn't apply on systems using plain lockfile locking, such as
	// Windows, and there we need to close the file before deleting it,
	// otherwise the system won't let us delete it.

	if(::unlink(mFileName.c_str()) != 0)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to delete lockfile"),
			CommonException, OSFileError);
	}
#endif // !WIN32

	// Close the file
	if(::close(mFileDescriptor) != 0)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to close lockfile"),
			CommonException, OSFileError);
	}

	// Mark as unlocked, so we don't try to close it again if the unlink() fails.
	mFileDescriptor = -1;

#ifdef WIN32
	// On Windows we need to close the file before deleting it, otherwise
	// the system won't let us delete it.

	if(::unlink(mFileName.c_str()) != 0)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to delete lockfile"),
			CommonException, OSFileError);
	}
#endif // WIN32

	BOX_TRACE("Released lock and deleted lockfile " << mFileName);
}

