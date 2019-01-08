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

#include <string>

#include "Exception.h"
#include "NamedLock.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

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
	if(mapLockFile.get())
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
	if(mapLockFile.get())
	{
		THROW_EXCEPTION(CommonException, NamedLockAlreadyLockingSomething)
	}

	try
	{
		int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef WIN32
		// Automatically delete file on close
		flags |= O_TEMPORARY;
#endif

		// This exception message doesn't add any useful information, so hide it:
		HideSpecificExceptionGuard hide(CommonException::ExceptionType,
			CommonException::FileLockingConflict);

		mapLockFile.reset(new FileStream(rFilename, flags, mode, FileStream::EXCLUSIVE));
	}
	catch(BoxException &e)
	{
		if(EXCEPTION_IS_TYPE(e, CommonException, FileLockingConflict))
		{
			BOX_NOTICE("Failed to lock lockfile: " << rFilename << ": already locked "
				"by another process");
			return false;
		}
		else
		{
			throw;
		}
	}

	if(!FileExists(rFilename))
	{
		BOX_ERROR("Locked lockfile " << rFilename << ", but lockfile no longer exists, "
			"bailing out");
		mapLockFile.reset();
		return false;
	}

	// Success
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
	if(!mapLockFile.get())
	{
		THROW_EXCEPTION(CommonException, NamedLockNotHeld)
	}

	std::string filename = mapLockFile->GetFileName();
	bool success = true;

#ifndef WIN32
	// Delete the file. We need to do this before closing the filehandle, if we used flock() or
	// fcntl() to lock it, otherwise someone could acquire the lock, release and delete it
	// between us closing (and hence releasing) and deleting it, and we'd fail when it came to
	// deleting the file. This happens in tests much more often than you'd expect!
	//
	// This doesn't apply on Windows (of course) because we can't delete the file while we still
	// have it open. So we open it with the O_TEMPORARY flag so that it's deleted automatically
	// when we close it, avoiding any race condition.

	if(EMU_UNLINK(filename.c_str()) != 0)
	{
		// Let the function continue so that we close the file and reset the handle
		// before returning the error to the user.
		success = false;
	}
#endif // !WIN32

	// Close the file
	mapLockFile->Close();

	// Don't try to release it again
	mapLockFile.reset();

	if(!success)
	{
		THROW_EMU_ERROR(
			BOX_FILE_MESSAGE(filename, "Failed to delete lockfile after unlocking it"),
			CommonException, NamedLockFailed);
	}

	BOX_TRACE("Released lock and deleted lockfile " << filename);
}
