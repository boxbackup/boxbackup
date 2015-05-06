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
//		Name:    BackupStoreAccounts::Create(int32_t, int, int64_t, int64_t, const std::string &)
//		Purpose: Create a new account on the specified disc set.
//			 If rAsUsername is not empty, then the account information will be written under the
//			 username specified.
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
		info->AdjustNumDirectories(1);
		
		// Save it back
		info->Save();

		// Create the refcount database
		BackupStoreRefCountDatabase::Create(Entry)->Commit();
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

void BackupStoreAccounts::LockAccount(int32_t ID, NamedLock& rNamedLock)
{
	const BackupStoreAccountDatabase::Entry &en(mrDatabase.GetEntry(ID));
	std::string rootDir = MakeAccountRootDir(ID, en.GetDiscSet());
	int discSet = en.GetDiscSet();

	std::string writeLockFilename;
	StoreStructure::MakeWriteLockFilename(rootDir, discSet, writeLockFilename);

	bool gotLock = false;
	int triesLeft = 8;
	do
	{
		gotLock = rNamedLock.TryAndGetLock(writeLockFilename,
			0600 /* restrictive file permissions */);
		
		if(!gotLock)
		{
			--triesLeft;
			::sleep(1);
		}
	}
	while (!gotLock && triesLeft > 0);

	if (!gotLock)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException,
			CouldNotLockStoreAccount, "Failed to get exclusive "
			"lock on account " << BOX_FORMAT_ACCOUNT(ID));
	}
}

BackupStoreAccountsControl::BackupStoreAccountsControl(
	const Configuration& config, bool machineReadableOutput)
: mConfig(config),
  mMachineReadableOutput(machineReadableOutput)
{ }

void BackupStoreAccountsControl::CheckSoftHardLimits(int64_t SoftLimit, int64_t HardLimit)
{
	if(SoftLimit > HardLimit)
	{
		BOX_FATAL("Soft limit must be less than the hard limit.");
		exit(1);
	}
	if(SoftLimit > ((HardLimit * MAX_SOFT_LIMIT_SIZE) / 100))
	{
		BOX_WARNING("We recommend setting the soft limit below " <<
			MAX_SOFT_LIMIT_SIZE << "% of the hard limit, or " <<
			HumanReadableSize((HardLimit * MAX_SOFT_LIMIT_SIZE)
				/ 100) << " in this case.");
	}
}

int BackupStoreAccountsControl::BlockSizeOfDiscSet(int discSetNum)
{
	// Get controller, check disc set number
	RaidFileController &controller(RaidFileController::GetController());
	if(discSetNum < 0 || discSetNum >= controller.GetNumDiscSets())
	{
		BOX_FATAL("Disc set " << discSetNum << " does not exist.");
		exit(1);
	}
	
	// Return block size
	return controller.GetDiscSet(discSetNum).GetBlockSize();
}

std::string BackupStoreAccountsControl::BlockSizeToString(int64_t Blocks, int64_t MaxBlocks, int discSetNum)
{
	return FormatUsageBar(Blocks, Blocks * BlockSizeOfDiscSet(discSetNum),
		MaxBlocks * BlockSizeOfDiscSet(discSetNum),
		mMachineReadableOutput);
}

int64_t BackupStoreAccountsControl::SizeStringToBlocks(const char *string, int discSetNum)
{
	// Find block size
	int blockSize = BlockSizeOfDiscSet(discSetNum);
	
	// Get number
	char *endptr = (char*)string;
	int64_t number = strtol(string, &endptr, 0);
	if(endptr == string || number == LONG_MIN || number == LONG_MAX)
	{
		BOX_FATAL("'" << string << "' is not a valid number.");
		exit(1);
	}
	
	// Check units
	switch(*endptr)
	{
	case 'M':
	case 'm':
		// Units: Mb
		return (number * 1024*1024) / blockSize;
		break;
		
	case 'G':
	case 'g':
		// Units: Gb
		return (number * 1024*1024*1024) / blockSize;
		break;
		
	case 'B':
	case 'b':
		// Units: Blocks
		// Easy! Just return the number specified.
		return number;
		break;
	
	default:
		BOX_FATAL(string << " has an invalid units specifier "
			"(use B for blocks, M for MB, G for GB, eg 2GB)");
		exit(1);
		break;		
	}
}

int BackupStoreAccountsControl::SetLimit(int32_t ID, const char *SoftLimitStr,
	const char *HardLimitStr)
{
	std::string rootDir;
	int discSetNum;
	std::auto_ptr<UnixUser> user; // used to reset uid when we return
	NamedLock writeLock;

	if(!OpenAccount(ID, rootDir, discSetNum, user, &writeLock))
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(ID)
			<< " to change limits.");
		return 1;
	}
	
	// Load the info
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(ID, rootDir,
		discSetNum, false /* Read/Write */));

	// Change the limits
	int64_t softlimit = SizeStringToBlocks(SoftLimitStr, discSetNum);
	int64_t hardlimit = SizeStringToBlocks(HardLimitStr, discSetNum);
	CheckSoftHardLimits(softlimit, hardlimit);
	info->ChangeLimits(softlimit, hardlimit);
	
	// Save
	info->Save();

	BOX_NOTICE("Limits on account " << BOX_FORMAT_ACCOUNT(ID) <<
		" changed to " << softlimit << " soft, " <<
		hardlimit << " hard.");

	return 0;
}

int BackupStoreAccountsControl::SetAccountName(int32_t ID, const std::string& rNewAccountName)
{
	std::string rootDir;
	int discSetNum;
	std::auto_ptr<UnixUser> user; // used to reset uid when we return
	NamedLock writeLock;

	if(!OpenAccount(ID, rootDir, discSetNum, user, &writeLock))
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(ID)
			<< " to change name.");
		return 1;
	}

	// Load the info
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(ID,
		rootDir, discSetNum, false /* Read/Write */));

	info->SetAccountName(rNewAccountName);
	
	// Save
	info->Save();

	BOX_NOTICE("Account " << BOX_FORMAT_ACCOUNT(ID) <<
		" name changed to " << rNewAccountName);

	return 0;
}

int BackupStoreAccountsControl::PrintAccountInfo(int32_t ID)
{
	std::string rootDir;
	int discSetNum;
	std::auto_ptr<UnixUser> user; // used to reset uid when we return

	if(!OpenAccount(ID, rootDir, discSetNum, user,
		NULL /* no write lock needed for this read-only operation */))
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(ID)
			<< " to display info.");
		return 1;
	}
	
	// Load it in
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(ID,
		rootDir, discSetNum, true /* ReadOnly */));
	
	// Then print out lots of info
	std::cout << FormatUsageLineStart("Account ID", mMachineReadableOutput) <<
		BOX_FORMAT_ACCOUNT(ID) << std::endl;
	std::cout << FormatUsageLineStart("Account Name", mMachineReadableOutput) <<
		info->GetAccountName() << std::endl;
	std::cout << FormatUsageLineStart("Last object ID", mMachineReadableOutput) <<
		BOX_FORMAT_OBJECTID(info->GetLastObjectIDUsed()) << std::endl;
	std::cout << FormatUsageLineStart("Used", mMachineReadableOutput) <<
		BlockSizeToString(info->GetBlocksUsed(),
			info->GetBlocksHardLimit(), discSetNum) << std::endl;
	std::cout << FormatUsageLineStart("Current files",
			mMachineReadableOutput) <<
		BlockSizeToString(info->GetBlocksInCurrentFiles(),
			info->GetBlocksHardLimit(), discSetNum) << std::endl;
	std::cout << FormatUsageLineStart("Old files", mMachineReadableOutput) <<
		BlockSizeToString(info->GetBlocksInOldFiles(),
			info->GetBlocksHardLimit(), discSetNum) << std::endl;
	std::cout << FormatUsageLineStart("Deleted files", mMachineReadableOutput) <<
		BlockSizeToString(info->GetBlocksInDeletedFiles(),
			info->GetBlocksHardLimit(), discSetNum) << std::endl;
	std::cout << FormatUsageLineStart("Directories", mMachineReadableOutput) <<
		BlockSizeToString(info->GetBlocksInDirectories(),
			info->GetBlocksHardLimit(), discSetNum) << std::endl;
	std::cout << FormatUsageLineStart("Soft limit", mMachineReadableOutput) <<
		BlockSizeToString(info->GetBlocksSoftLimit(),
			info->GetBlocksHardLimit(), discSetNum) << std::endl;
	std::cout << FormatUsageLineStart("Hard limit", mMachineReadableOutput) <<
		BlockSizeToString(info->GetBlocksHardLimit(),
			info->GetBlocksHardLimit(), discSetNum) << std::endl;
	std::cout << FormatUsageLineStart("Client store marker", mMachineReadableOutput) <<
		info->GetClientStoreMarker() << std::endl;
	std::cout << FormatUsageLineStart("Current Files", mMachineReadableOutput) <<
		info->GetNumCurrentFiles() << std::endl;
	std::cout << FormatUsageLineStart("Old Files", mMachineReadableOutput) <<
		info->GetNumOldFiles() << std::endl;
	std::cout << FormatUsageLineStart("Deleted Files", mMachineReadableOutput) <<
		info->GetNumDeletedFiles() << std::endl;
	std::cout << FormatUsageLineStart("Directories", mMachineReadableOutput) <<
		info->GetNumDirectories() << std::endl;
	std::cout << FormatUsageLineStart("Enabled", mMachineReadableOutput) <<
		(info->IsAccountEnabled() ? "yes" : "no") << std::endl;
	
	return 0;
}

int BackupStoreAccountsControl::SetAccountEnabled(int32_t ID, bool enabled)
{
	std::string rootDir;
	int discSetNum;
	std::auto_ptr<UnixUser> user; // used to reset uid when we return
	NamedLock writeLock;

	if(!OpenAccount(ID, rootDir, discSetNum, user, &writeLock))
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(ID)
			<< " to change enabled flag.");
		return 1;
	}
	
	// Load it in
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(ID,
		rootDir, discSetNum, false /* ReadOnly */));
	info->SetAccountEnabled(enabled);
	info->Save();
	return 0;
}

int BackupStoreAccountsControl::DeleteAccount(int32_t ID, bool AskForConfirmation)
{
	std::string rootDir;
	int discSetNum;
	std::auto_ptr<UnixUser> user; // used to reset uid when we return
	NamedLock writeLock;

	// Obtain a write lock, as the daemon user
	if(!OpenAccount(ID, rootDir, discSetNum, user, &writeLock))
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(ID)
			<< " for deletion.");
		return 1;
	}

	// Check user really wants to do this
	if(AskForConfirmation)
	{
		BOX_WARNING("Really delete account " << 
			BOX_FORMAT_ACCOUNT(ID) << "? (type 'yes' to confirm)");
		char response[256];
		if(::fgets(response, sizeof(response), stdin) == 0 || ::strcmp(response, "yes\n") != 0)
		{
			BOX_NOTICE("Deletion cancelled.");
			return 0;
		}
	}
	
	// Back to original user, but write lock is maintained
	user.reset();

	std::auto_ptr<BackupStoreAccountDatabase> db(
		BackupStoreAccountDatabase::Read(
			mConfig.GetKeyValue("AccountDatabase")));

	// Delete from account database
	db->DeleteEntry(ID);
	
	// Write back to disc
	db->Write();
	
	// Remove the store files...

	// First, become the user specified in the config file
	std::string username;
	{
		const Configuration &rserverConfig(mConfig.GetSubConfiguration("Server"));
		if(rserverConfig.KeyExists("User"))
		{
			username = rserverConfig.GetKeyValue("User");
		}
	}

	// Become the right user
	if(!username.empty())
	{
		// Username specified, change...
		user.reset(new UnixUser(username));
		user->ChangeProcessUser(true /* temporary */);
		// Change will be undone when user goes out of scope
	}

	// Secondly, work out which directories need wiping
	std::vector<std::string> toDelete;
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet discSet(rcontroller.GetDiscSet(discSetNum));
	for(RaidFileDiscSet::const_iterator i(discSet.begin()); i != discSet.end(); ++i)
	{
		if(std::find(toDelete.begin(), toDelete.end(), *i) == toDelete.end())
		{
			toDelete.push_back((*i) + DIRECTORY_SEPARATOR + rootDir);
		}
	}

	// NamedLock will throw an exception if it can't delete the lockfile,
	// which it can't if it doesn't exist. Now that we've deleted the account,
	// nobody can open it anyway, so it's safe to unlock.
	writeLock.ReleaseLock();

	int retcode = 0;

	// Thirdly, delete the directories...
	for(std::vector<std::string>::const_iterator d(toDelete.begin()); d != toDelete.end(); ++d)
	{
		BOX_NOTICE("Deleting store directory " << (*d) << "...");
		// Just use the rm command to delete the files
		std::string cmd("rm -rf ");
		cmd += *d;
		// Run command
		if(::system(cmd.c_str()) != 0)
		{
			BOX_ERROR("Failed to delete files in " << (*d) <<
				", delete them manually.");
			retcode = 1;
		}
	}
	
	// Success!
	return retcode;
}

bool BackupStoreAccountsControl::OpenAccount(int32_t ID, std::string &rRootDirOut,
	int &rDiscSetOut, std::auto_ptr<UnixUser> apUser, NamedLock* pLock)
{
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(
		BackupStoreAccountDatabase::Read(
			mConfig.GetKeyValue("AccountDatabase")));
	
	// Exists?
	if(!db->EntryExists(ID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(ID) << 
			" does not exist.");
		return false;
	}
	
	// Get info from the database
	BackupStoreAccounts acc(*db);
	acc.GetAccountRoot(ID, rRootDirOut, rDiscSetOut);

	// Get the user under which the daemon runs
	std::string username;
	{
		const Configuration &rserverConfig(mConfig.GetSubConfiguration("Server"));
		if(rserverConfig.KeyExists("User"))
		{
			username = rserverConfig.GetKeyValue("User");
		}
	}

	// Become the right user
	if(!username.empty())
	{
		// Username specified, change...
		apUser.reset(new UnixUser(username));
		apUser->ChangeProcessUser(true /* temporary */);
		// Change will be undone when apUser goes out of scope
		// in the caller.
	}

	if(pLock)
	{
		acc.LockAccount(ID, *pLock);
	}

	return true;
}

int BackupStoreAccountsControl::CheckAccount(int32_t ID, bool FixErrors, bool Quiet,
	bool ReturnNumErrorsFound)
{
	std::string rootDir;
	int discSetNum;
	std::auto_ptr<UnixUser> user; // used to reset uid when we return
	NamedLock writeLock;

	if(!OpenAccount(ID, rootDir, discSetNum, user,
		FixErrors ? &writeLock : NULL)) // don't need a write lock if not making changes
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(ID)
			<< " for checking.");
		return 1;
	}

	// Check it
	BackupStoreCheck check(rootDir, discSetNum, ID, FixErrors, Quiet);
	check.Check();

	if(ReturnNumErrorsFound)
	{
		return check.GetNumErrorsFound();
	}
	else
	{
		return check.ErrorsFound() ? 1 : 0;
	}
}

int BackupStoreAccountsControl::CreateAccount(int32_t ID, int32_t DiscNumber,
	int32_t SoftLimit, int32_t HardLimit)
{
	// Load in the account database 
	std::auto_ptr<BackupStoreAccountDatabase> db(
		BackupStoreAccountDatabase::Read(
			mConfig.GetKeyValue("AccountDatabase")));
	
	// Already exists?
	if(db->EntryExists(ID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(ID) << 
			" already exists.");
		return 1;
	}

	// Get the user under which the daemon runs
	std::string username;
	{
		const Configuration &rserverConfig(mConfig.GetSubConfiguration("Server"));
		if(rserverConfig.KeyExists("User"))
		{
			username = rserverConfig.GetKeyValue("User");
		}
	}
	
	// Create it.
	BackupStoreAccounts acc(*db);
	acc.Create(ID, DiscNumber, SoftLimit, HardLimit, username);
	
	BOX_NOTICE("Account " << BOX_FORMAT_ACCOUNT(ID) << " created.");

	return 0;
}

int BackupStoreAccountsControl::HousekeepAccountNow(int32_t ID)
{
	std::string rootDir;
	int discSetNum;
	std::auto_ptr<UnixUser> user; // used to reset uid when we return

	if(!OpenAccount(ID, rootDir, discSetNum, user,
		NULL /* housekeeping locks the account itself */))
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(ID)
			<< " for housekeeping.");
		return 1;
	}

	HousekeepStoreAccount housekeeping(ID, rootDir, discSetNum, NULL);
	bool success = housekeeping.DoHousekeeping();

	if(!success)
	{
		BOX_ERROR("Failed to lock account " << BOX_FORMAT_ACCOUNT(ID)
			<< " for housekeeping: perhaps a client is "
			"still connected?");
		return 1;
	}
	else
	{
		BOX_TRACE("Finished housekeeping on account " <<
			BOX_FORMAT_ACCOUNT(ID));
		return 0;
	}
}

