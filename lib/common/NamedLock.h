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

	typedef enum
	{
		LOCKTYPE_O_EXLOCK = 1,
		LOCKTYPE_WIN32 = 2,
		LOCKTYPE_F_SETLK = 3,
		LOCKTYPE_FLOCK = 4,
		LOCKTYPE_DUMB = 5,
	}
	LockType;

	LockType mMethod;
};

#endif // NAMEDLOCK__H

