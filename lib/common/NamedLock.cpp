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
	std::string method_name;

#ifdef BOX_LOCK_TYPE_O_EXLOCK
	flags |= O_NONBLOCK | O_EXLOCK;
	method_name = "O_EXLOCK";
#elif defined BOX_LOCK_TYPE_WIN32
	flags |= BOX_OPEN_LOCK;
	method_name = "BOX_OPEN_LOCK";
#elif defined BOX_LOCK_TYPE_F_OFD_SETLK
	method_name = "no special flags (for F_OFD_SETLK)";
#elif defined BOX_LOCK_TYPE_F_SETLK
	method_name = "no special flags (for F_SETLK)";
#elif defined BOX_LOCK_TYPE_FLOCK
	method_name = "no special flags (for flock())";
#elif defined BOX_LOCK_TYPE_DUMB
	// We have no other way to get a lock, so all we can do is fail if the
	// file already exists, and take the risk of stale locks.
	flags |= O_EXCL;
	method_name = "O_EXCL";
	method = LOCKTYPE_DUMB;
#else
#	error "Unknown locking type"
#endif

	BOX_TRACE("Trying to create lockfile " << rFilename << " using " << method_name);

#ifdef WIN32
	HANDLE fd = openfile(rFilename.c_str(), flags, mode);
	if(fd == INVALID_HANDLE_VALUE)
#else
	int fd = ::open(rFilename.c_str(), flags, mode);
	if(fd == -1)
#endif
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
			// Lockfile already exists, and we tried to open it
			// exclusively, which means we failed to lock it, which
			// means that it's locked by someone else, which is an
			// expected error condition, signalled by returning
			// false instead of throwing.
			BOX_NOTICE("Failed to lock lockfile with " << method_name << ": " <<
				rFilename << ": already locked by another process?");
			return false;
		}
		else
		{
			THROW_SYS_FILE_ERROR("Failed to open lockfile with " << method_name,
				rFilename, CommonException, OSFileError);
		}
	}

	try
	{
#ifdef BOX_LOCK_TYPE_FLOCK
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
#elif defined BOX_LOCK_TYPE_F_SETLK || defined BOX_LOCK_TYPE_F_OFD_SETLK
		struct flock desc;
		desc.l_type = F_WRLCK;
		desc.l_whence = SEEK_SET;
		desc.l_start = 0;
		desc.l_len = 0;
		desc.l_pid = 0;
		BOX_TRACE("Trying to lock lockfile " << rFilename << " using fcntl()");
#	if defined BOX_LOCK_TYPE_F_OFD_SETLK
		if(::fcntl(fd, F_OFD_SETLK, &desc) != 0)
#	else // BOX_LOCK_TYPE_F_SETLK
		if(::fcntl(fd, F_SETLK, &desc) != 0)
#	endif
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
#endif
	}
	catch(BoxException &e)
	{
#ifdef WIN32
		CloseHandle(fd);
#else
		::close(fd);
#endif
		THROW_FILE_ERROR("Failed to lock lockfile: " << e.what(), rFilename,
			CommonException, NamedLockFailed);
	}

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
	BOX_TRACE("Successfully locked lockfile " << rFilename << " using " << method_name);

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

	bool success = true;

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
		// Let the function continue so that we close the file and reset the handle
		// before returning the error to the user.
		success = false;
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
		success = false;
	}
#endif // WIN32

	if(!success)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(mFileName, "Failed to delete lockfile"),
			CommonException, OSFileError);
	}

	BOX_TRACE("Released lock and deleted lockfile " << mFileName);
}
