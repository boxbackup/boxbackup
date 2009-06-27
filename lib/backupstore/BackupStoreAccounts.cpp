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
#include "BackupStoreRefCountDatabase.h"
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
	// Create the entry in the database
	BackupStoreAccountDatabase::Entry Entry(mrDatabase.AddEntry(ID,
		DiscSet));
	
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

		// Create the refcount database
		BackupStoreRefCountDatabase::CreateNew(Entry);
		std::auto_ptr<BackupStoreRefCountDatabase> refcount(
			BackupStoreRefCountDatabase::Load(Entry, false));
		refcount->AddReference(BACKUPSTORE_ROOT_DIRECTORY_ID);
	}

	// As the original user...
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
std::string BackupStoreAccounts::MakeAccountRootDir(int32_t ID, int DiscSet)
{
	char accid[64];	// big enough!
	::sprintf(accid, "%08x" DIRECTORY_SEPARATOR, ID);
	return std::string(std::string(BOX_RAIDFILE_ROOT_BBSTORED 
		DIRECTORY_SEPARATOR) + accid);
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


