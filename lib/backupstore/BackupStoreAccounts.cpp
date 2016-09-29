// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreAccounts.cpp
//		Purpose: Account management for backup store server
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "BackupFileSystem.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "BoxPortsAndFiles.h"
#include "HousekeepStoreAccount.h"
#include "NamedLock.h"
#include "RaidFileController.h"
#include "RaidFileWrite.h"
#include "StoreStructure.h"
#include "UnixUser.h"
#include "Utils.h"

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
	::snprintf(accid, sizeof(accid) - 1, "%08x" DIRECTORY_SEPARATOR, ID);
	return std::string(BOX_RAIDFILE_ROOT_BBSTORED DIRECTORY_SEPARATOR) + 
		accid;
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

