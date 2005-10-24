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
	bool TryAndGetLock(const char *Filename, int mode = 0755);
	bool GotLock() {return mFileDescriptor != -1;}
	void ReleaseLock();
	
	
private:
	int mFileDescriptor;
};

#endif // NAMEDLOCK__H

