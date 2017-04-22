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
: mFileDescriptor(INVALID_FILE)
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
	if(mFileDescriptor != INVALID_FILE)
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
	if(mFileDescriptor != INVALID_FILE)
	{
		THROW_EXCEPTION(CommonException, NamedLockAlreadyLockingSomething)
	}

	mFileName = rFilename;

	// See if the lock can be got
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	std::string method;

#if HAVE_DECL_O_EXLOCK
	flags |= O_NONBLOCK | O_EXLOCK;
	method = "O_EXLOCK";
#elif defined BOX_OPEN_LOCK
	flags |= BOX_OPEN_LOCK;
	method = "BOX_OPEN_LOCK";
#elif HAVE_DECL_F_SETLK
	method = "no special flags (for F_SETLK)";
#elif defined HAVE_FLOCK
	method = "no special flags (for flock())";
#else
	// We have no other way to get a lock, so all we can do is fail if
	// the file already exists, and take the risk of stale locks.
	flags |= O_EXCL;
	method = "O_EXCL";
#endif

	BOX_TRACE("Trying to create lockfile " << rFilename << " using " << method);

#ifdef WIN32
	HANDLE fd = openfile(rFilename.c_str(), flags, mode);
	if(fd == INVALID_HANDLE_VALUE)
#else
	int fd = ::open(rFilename.c_str(), flags, mode);
	if(fd == -1)
#endif
#if HAVE_DECL_O_EXLOCK
	{ // if()
		if(errno == EWOULDBLOCK)
		{
			// Lockfile already exists, and we tried to open it
			// exclusively, which means we failed to lock it.
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to open lockfile with " << method,
				rFilename, CommonException, OSFileError);
		}
	}
#else // !HAVE_DECL_O_EXLOCK
	{ // if()
# if defined BOX_OPEN_LOCK
		if(errno == EBUSY)
# else // !BOX_OPEN_LOCK
		if(errno == EEXIST && (flags & O_EXCL))
# endif
		{
			// Lockfile already exists, and we tried to open it
			// exclusively, which means we failed to lock it.
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to open lockfile with " << method,
				rFilename, CommonException, OSFileError);
		}

		// If we didn't throw an exception above, it means that the lockfile is locked
		// by someone else, which is an expected error condition, signalled by returning
		// false instead of throwing.
		BOX_NOTICE("Failed to lock lockfile with " << method << ": " <<
			rFilename << ": already locked by another process?");
		return false;
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
#ifdef WIN32
		CloseHandle(fd);
#else
		::close(fd);
#endif
		BOX_NOTICE("Failed to lock lockfile " << rFilename << ": " << e.what());
		throw;
	}
#endif // HAVE_DECL_O_EXLOCK

	if(!FileExists(rFilename))
	{
		BOX_ERROR("Locked lockfile " << rFilename << ", but lockfile no longer "
			"exists, bailing out");
#ifdef WIN32
		CloseHandle(fd);
#else
		::close(fd);
#endif
		return false;
	}

	// Success
	mFileDescriptor = fd;
	BOX_TRACE("Successfully locked lockfile " << rFilename);

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
	if(mFileDescriptor == INVALID_FILE)
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

	if(EMU_UNLINK(mFileName.c_str()) != 0)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to delete lockfile"),
			CommonException, OSFileError);
	}
#endif // !WIN32

	// Close the file
#ifdef WIN32
	if(!CloseHandle(mFileDescriptor))
#else
	if(::close(mFileDescriptor) != 0)
#endif
	{
		// Don't try to release it again
		mFileDescriptor = INVALID_FILE;
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to close lockfile"),
			CommonException, OSFileError);
	}

	// Mark as unlocked, so we don't try to close it again if the unlink() fails.
	mFileDescriptor = INVALID_FILE;

#ifdef WIN32
	// On Windows we need to close the file before deleting it, otherwise
	// the system won't let us delete it.

	if(EMU_UNLINK(mFileName.c_str()) != 0)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to delete lockfile"),
			CommonException, OSFileError);
	}
#endif // WIN32

	BOX_TRACE("Released lock and deleted lockfile " << mFileName);
}
