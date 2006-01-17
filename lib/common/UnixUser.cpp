// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    UnixUser.cpp
//		Purpose: Interface for managing the UNIX user of the current process
//		Created: 21/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <pwd.h>
#include <unistd.h>

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




