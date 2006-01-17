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
//		Name:    BackupStoreAccounts.cpp
//		Purpose: Account management for backup store server
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "BoxPortsAndFiles.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreAccountDatabase.h"
#include "RaidFileWrite.h"
#include "BackupStoreInfo.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreConstants.h"
#include "UnixUser.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccounts::BackupStoreAccounts(BackupStoreAccountDatabase &)
//		Purpose: Constructor
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
BackupStoreAccounts::BackupStoreAccounts(BackupStoreAccountDatabase &rDatabase)
	: mrDatabase(rDatabase)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccounts::~BackupStoreAccounts()
//		Purpose: Destructor
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
BackupStoreAccounts::~BackupStoreAccounts()
{
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccounts::Create(int32_t, int, int64_t, int64_t, const std::string &)
//		Purpose: Create a new account on the specified disc set.
//				 If rAsUsername is not empty, then the account information will be written under the
//				 username specified.
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
void BackupStoreAccounts::Create(int32_t ID, int DiscSet, int64_t SizeSoftLimit, int64_t SizeHardLimit, const std::string &rAsUsername)
{
	{
		// Become the user specified in the config file?
		std::auto_ptr<UnixUser> user;
		if(!rAsUsername.empty())
		{
			// Username specified, change...
			user.reset(new UnixUser(rAsUsername.c_str()));
			user->ChangeProcessUser(true /* temporary */);
			// Change will be undone at the end of this function
		}

		// Get directory name
		std::string dirName(MakeAccountRootDir(ID, DiscSet));
	
		// Create a directory on disc
		RaidFileWrite::CreateDirectory(DiscSet, dirName, true /* recursive */);
		
		// Create an info file
		BackupStoreInfo::CreateNew(ID, dirName, DiscSet, SizeSoftLimit, SizeHardLimit);
		
		// And an empty directory
		BackupStoreDirectory rootDir(BACKUPSTORE_ROOT_DIRECTORY_ID, BACKUPSTORE_ROOT_DIRECTORY_ID);
		int64_t rootDirSize = 0;
		// Write it, knowing the directory scheme
		{
			RaidFileWrite rf(DiscSet, dirName + "o01");
			rf.Open();
			rootDir.WriteToStream(rf);
			rootDirSize = rf.GetDiscUsageInBlocks();
			rf.Commit(true);
		}
	
		// Update the store info to reflect the size of the root directory
		std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(ID, dirName, DiscSet, false /* ReadWrite */));
		info->ChangeBlocksUsed(rootDirSize);
		info->ChangeBlocksInDirectories(rootDirSize);
		
		// Save it back
		info->Save();
	}

	// As the original user...

	// Create the entry in the database
	mrDatabase.AddEntry(ID, DiscSet);
	
	// Write the database back
	mrDatabase.Write();	
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccounts::GetAccountRoot(int32_t, std::string &, int &)
//		Purpose: Gets the root of an account, returning the info via references
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
void BackupStoreAccounts::GetAccountRoot(int32_t ID, std::string &rRootDirOut, int &rDiscSetOut) const
{
	// Find the account
	const BackupStoreAccountDatabase::Entry &en(mrDatabase.GetEntry(ID));
	
	rRootDirOut = MakeAccountRootDir(ID, en.GetDiscSet());
	rDiscSetOut = en.GetDiscSet();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccounts::MakeAccountRootDir(int32_t, int)
//		Purpose: Private. Generates a root directory name for the account
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
std::string BackupStoreAccounts::MakeAccountRootDir(int32_t ID, int DiscSet) const
{
	char accid[64];	// big enough!
	::sprintf(accid, "%08x/", ID);
	return std::string(std::string(BOX_RAIDFILE_ROOT_BBSTORED DIRECTORY_SEPARATOR) + accid);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreAccounts::AccountExists(int32_t)
//		Purpose: Does an account exist?
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
bool BackupStoreAccounts::AccountExists(int32_t ID)
{
	return mrDatabase.EntryExists(ID);
}


