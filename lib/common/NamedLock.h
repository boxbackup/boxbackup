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
# ifdef WIN32
	bool GotLock() {return mFileDescriptor != INVALID_HANDLE_VALUE;}
# else
	bool GotLock() {return mFileDescriptor != -1;}
# endif
	void ReleaseLock();

private:
# ifdef WIN32
	HANDLE mFileDescriptor;
# else
	int mFileDescriptor;
# endif

	std::string mFileName;
};

#endif // NAMEDLOCK__H

