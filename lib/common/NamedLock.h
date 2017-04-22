// --------------------------------------------------------------------------
//
// File
//		Name:    NamedLock.h
//		Purpose: A global named lock, implemented as a lock file in file system
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef NAMEDLOCK__H
#define NAMEDLOCK__H

// --------------------------------------------------------------------------
//
// Class
//		Name:    NamedLock
//		Purpose: A global named lock, implemented as a lock file in file system
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
class NamedLock
{
public:
	NamedLock();
	~NamedLock();
private:
	// No copying allowed
	NamedLock(const NamedLock &);

public:
	bool TryAndGetLock(const std::string& rFilename, int mode = 0755);
	bool GotLock() {return mFileDescriptor != INVALID_FILE;}
	void ReleaseLock();

private:
	tOSFileHandle mFileDescriptor;
	std::string mFileName;
};

#if HAVE_DECL_O_EXLOCK
#	define BOX_LOCK_TYPE_O_EXLOCK
#elif defined BOX_OPEN_LOCK
#	define BOX_LOCK_TYPE_WIN32
#elif defined HAVE_FLOCK
// This is preferable to F_OFD_SETLK because no byte ranges are involved
#	define BOX_LOCK_TYPE_FLOCK
#elif HAVE_DECL_F_OFD_SETLK
// This is preferable to F_SETLK because it's non-reentrant
#	define BOX_LOCK_TYPE_F_OFD_SETLK
#elif HAVE_DECL_F_SETLK
// This is not ideal because it's reentrant, but better than a dumb lock
// (reentrancy only matters in tests; in real use it's as good as F_OFD_SETLK).
#	define BOX_LOCK_TYPE_F_SETLK
#else
// We have no other way to get a lock, so all we can do is fail if the
// file already exists, and take the risk of stale locks.
#	define BOX_LOCK_TYPE_DUMB
#endif

#endif // NAMEDLOCK__H

