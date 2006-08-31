// --------------------------------------------------------------------------
//
// File
//		Name:    UnixUser.cpp
//		Purpose: Interface for managing the UNIX user of the current process
//		Created: 21/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_PWD_H
	#include <pwd.h>
#endif

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "UnixUser.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    UnixUser::UnixUser(const char *)
//		Purpose: Constructor, initialises to info of given username
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
UnixUser::UnixUser(const char *Username)
	: mUID(0),
	  mGID(0),
	  mRevertOnDestruction(false)
{
	// Get password info
	struct passwd *pwd = ::getpwnam(Username);
	if(pwd == 0)
	{
		THROW_EXCEPTION(CommonException, CouldNotLookUpUsername)
	}
	
	// Store UID and GID
	mUID = pwd->pw_uid;
	mGID = pwd->pw_gid;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    UnixUser::UnixUser(uid_t, gid_t)
//		Purpose: Construct from given UNIX user ID and group ID
//		Created: 15/3/04
//
// --------------------------------------------------------------------------
UnixUser::UnixUser(uid_t UID, gid_t GID)
	: mUID(UID),
	  mGID(GID),
	  mRevertOnDestruction(false)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    UnixUser::~UnixUser()
//		Purpose: Destructor -- reverts to previous user if the change wasn't perminant
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
UnixUser::~UnixUser()
{
	if(mRevertOnDestruction)
	{
		// Revert to "real" user and group id of the process
		if(::setegid(::getgid()) != 0
			|| ::seteuid(::getuid()) != 0)
		{
			THROW_EXCEPTION(CommonException, CouldNotRestoreProcessUser)
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    UnixUser::ChangeProcessUser(bool)
//		Purpose: Change the process user and group ID to the user. If Temporary == true
//				 the process username will be changed back when the object is destructed.
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
void UnixUser::ChangeProcessUser(bool Temporary)
{
	if(Temporary)
	{
		// Change temporarily (change effective only)
		if(::setegid(mGID) != 0
			|| ::seteuid(mUID) != 0)
		{
			THROW_EXCEPTION(CommonException, CouldNotChangeProcessUser)
		}
		
		// Mark for change on destruction
		mRevertOnDestruction = true;
	}
	else
	{
		// Change perminantely (change all UIDs and GIDs)
		if(::setgid(mGID) != 0
			|| ::setuid(mUID) != 0)
		{
			THROW_EXCEPTION(CommonException, CouldNotChangeProcessUser)
		}
	}
}




