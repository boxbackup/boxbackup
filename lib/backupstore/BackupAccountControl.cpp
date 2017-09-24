// --------------------------------------------------------------------------
//
// File
//		Name:    BackupAccountControl.cpp
//		Purpose: Client-side account management for Amazon S3 stores
//		Created: 2015/06/27
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <climits>
#include <cstdio>
#include <iostream>

#include "autogen_BackupStoreException.h"
#include "autogen_CommonException.h"
#include "BackupAccountControl.h"
#include "BackupFileSystem.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreInfo.h"
#include "Configuration.h"
#include "HTTPResponse.h"
#include "HousekeepStoreAccount.h"
#include "RaidFileController.h"
#include "RaidFileWrite.h"
#include "UnixUser.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#define OPEN_ACCOUNT(read_write) \
	try \
	{ \
		OpenAccount(read_write); \
	} \
	catch(BoxException &e) \
	{ \
		if(EXCEPTION_IS_TYPE(e, BackupStoreException, CouldNotLockStoreAccount)) \
		{ \
			BOX_ERROR("Failed to open account: " << e.what()); \
			return 1; \
		} \
		throw; \
	}


void BackupAccountControl::CheckSoftHardLimits(int64_t SoftLimit, int64_t HardLimit)
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


int64_t BackupAccountControl::SizeStringToBlocks(const char *string, int blockSize)
{
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


std::string BackupAccountControl::BlockSizeToString(int64_t Blocks, int64_t MaxBlocks, int BlockSize)
{
	return FormatUsageBar(Blocks, Blocks * BlockSize, MaxBlocks * BlockSize,
		mMachineReadableOutput);
}


int BackupStoreAccountControl::BlockSizeOfDiscSet(int discSetNum)
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


int BackupAccountControl::SetLimit(const char *SoftLimitStr,
	const char *HardLimitStr)
{
	int64_t softlimit = SizeStringToBlocks(SoftLimitStr, GetBlockSize());
	int64_t hardlimit = SizeStringToBlocks(HardLimitStr, GetBlockSize());
	return BackupAccountControl::SetLimit(softlimit, hardlimit);
}


int BackupAccountControl::SetLimit(int64_t softlimit, int64_t hardlimit)
{
	CheckSoftHardLimits(softlimit, hardlimit);

	// Change the limits
	OPEN_ACCOUNT(true); // readWrite
	BackupStoreInfo &info(mapFileSystem->GetBackupStoreInfo(false)); // !ReadOnly
	info.ChangeLimits(softlimit, hardlimit);
	mapFileSystem->PutBackupStoreInfo(info);

	BOX_NOTICE("Limits on account " << mapFileSystem->GetAccountIdentifier() << " "
		"changed to " << softlimit << " blocks soft, " << hardlimit << " hard.");

	return 0;
}


int BackupAccountControl::SetAccountName(const std::string& rNewAccountName)
{
	OPEN_ACCOUNT(true); // readWrite
	BackupStoreInfo &info(mapFileSystem->GetBackupStoreInfo(false)); // !ReadOnly
	info.SetAccountName(rNewAccountName);
	mapFileSystem->PutBackupStoreInfo(info);

	BOX_NOTICE("Name of account " << mapFileSystem->GetAccountIdentifier() << " "
		"changed to " << rNewAccountName);

	return 0;
}


int BackupAccountControl::PrintAccountInfo()
{
	OPEN_ACCOUNT(false); // !readWrite

	BackupStoreInfo& info(mapFileSystem->GetBackupStoreInfo(true)); // ReadOnly
	int BlockSize = GetBlockSize();

	// Then print out lots of info
	std::cout << FormatUsageLineStart("Account ID", mMachineReadableOutput) <<
		BOX_FORMAT_ACCOUNT(info.GetAccountID()) << std::endl;
	std::cout << FormatUsageLineStart("Account Name", mMachineReadableOutput) <<
		info.GetAccountName() << std::endl;
	std::cout << FormatUsageLineStart("Last object ID", mMachineReadableOutput) <<
		BOX_FORMAT_OBJECTID(info.GetLastObjectIDUsed()) << std::endl;
	std::cout << FormatUsageLineStart("Used", mMachineReadableOutput) <<
		BlockSizeToString(info.GetBlocksUsed(),
			info.GetBlocksHardLimit(), BlockSize) << std::endl;
	std::cout << FormatUsageLineStart("Current files",
			mMachineReadableOutput) <<
		BlockSizeToString(info.GetBlocksInCurrentFiles(),
			info.GetBlocksHardLimit(), BlockSize) << std::endl;
	std::cout << FormatUsageLineStart("Old files", mMachineReadableOutput) <<
		BlockSizeToString(info.GetBlocksInOldFiles(),
			info.GetBlocksHardLimit(), BlockSize) << std::endl;
	std::cout << FormatUsageLineStart("Deleted files", mMachineReadableOutput) <<
		BlockSizeToString(info.GetBlocksInDeletedFiles(),
			info.GetBlocksHardLimit(), BlockSize) << std::endl;
	std::cout << FormatUsageLineStart("Directories", mMachineReadableOutput) <<
		BlockSizeToString(info.GetBlocksInDirectories(),
			info.GetBlocksHardLimit(), BlockSize) << std::endl;
	std::cout << FormatUsageLineStart("Soft limit", mMachineReadableOutput) <<
		BlockSizeToString(info.GetBlocksSoftLimit(),
			info.GetBlocksHardLimit(), BlockSize) << std::endl;
	std::cout << FormatUsageLineStart("Hard limit", mMachineReadableOutput) <<
		BlockSizeToString(info.GetBlocksHardLimit(),
			info.GetBlocksHardLimit(), BlockSize) << std::endl;
	std::cout << FormatUsageLineStart("Client store marker", mMachineReadableOutput) <<
		info.GetClientStoreMarker() << std::endl;
	std::cout << FormatUsageLineStart("Current Files", mMachineReadableOutput) <<
		info.GetNumCurrentFiles() << std::endl;
	std::cout << FormatUsageLineStart("Old Files", mMachineReadableOutput) <<
		info.GetNumOldFiles() << std::endl;
	std::cout << FormatUsageLineStart("Deleted Files", mMachineReadableOutput) <<
		info.GetNumDeletedFiles() << std::endl;
	std::cout << FormatUsageLineStart("Directories", mMachineReadableOutput) <<
		info.GetNumDirectories() << std::endl;
	std::cout << FormatUsageLineStart("Enabled", mMachineReadableOutput) <<
		(info.IsAccountEnabled() ? "yes" : "no") << std::endl;

	return 0;
}


int BackupAccountControl::SetAccountEnabled(bool enabled)
{
	OPEN_ACCOUNT(true); // readWrite
	BackupStoreInfo &info(mapFileSystem->GetBackupStoreInfo(false)); // !ReadOnly
	info.SetAccountEnabled(enabled);
	mapFileSystem->PutBackupStoreInfo(info);

	BOX_NOTICE("Account " << mapFileSystem->GetAccountIdentifier() << " is now " <<
		(enabled ? "enabled" : "disabled"));

	return 0;
}


int BackupAccountControl::CreateAccount(int32_t AccountID, int32_t SoftLimit,
	int32_t HardLimit, const std::string& AccountName)
{
	// We definitely need a lock to do something this destructive!
	mapFileSystem->GetLock();

	BackupStoreInfo info(AccountID, SoftLimit, HardLimit);
	info.SetAccountName(AccountName);

	// And an empty directory
	BackupStoreDirectory rootDir(BACKUPSTORE_ROOT_DIRECTORY_ID,
		BACKUPSTORE_ROOT_DIRECTORY_ID);
	mapFileSystem->PutDirectory(rootDir);
	int64_t rootDirSize = rootDir.GetUserInfo1_SizeInBlocks();

	// Update the store info to reflect the size of the root directory
	info.ChangeBlocksUsed(rootDirSize);
	info.ChangeBlocksInDirectories(rootDirSize);
	info.AdjustNumDirectories(1);
	int64_t id = info.AllocateObjectID();
	ASSERT(id == BACKUPSTORE_ROOT_DIRECTORY_ID);

	mapFileSystem->PutBackupStoreInfo(info);

	// Now get the info file again, and report any differences, to check that it
	// really worked.
	std::auto_ptr<BackupStoreInfo> copy = mapFileSystem->GetBackupStoreInfoUncached();
	ASSERT(info.ReportChangesTo(*copy) == 0);

	// We also need to create and upload a fresh refcount DB.
	BackupStoreRefCountDatabase& refcount(
		mapFileSystem->GetPotentialRefCountDatabase());
	refcount.Commit();

	return 0;
}


int BackupStoreAccountControl::DeleteAccount(bool AskForConfirmation)
{
	// Obtain a write lock, as the daemon user
	// We definitely need a lock to do something this destructive!
	OPEN_ACCOUNT(true); // readWrite

	// Check user really wants to do this
	if(AskForConfirmation)
	{
		BOX_WARNING("Really delete account " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << "? (type 'yes' to confirm)");
		char response[256];
		if(::fgets(response, sizeof(response), stdin) == 0 || ::strcmp(response, "yes\n") != 0)
		{
			BOX_NOTICE("Deletion cancelled.");
			return 0;
		}
	}

	// Back to original user, but write lock is maintained
	mapChangeUser.reset();

	std::auto_ptr<BackupStoreAccountDatabase> db(
		BackupStoreAccountDatabase::Read(
			mConfig.GetKeyValue("AccountDatabase")));

	// Delete from account database
	db->DeleteEntry(mAccountID);

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
		mapChangeUser.reset(new UnixUser(username));
		mapChangeUser->ChangeProcessUser(true /* temporary */);
		// Change will be undone when user goes out of scope
	}

	// Secondly, work out which directories need wiping
	std::vector<std::string> toDelete;
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet discSet(rcontroller.GetDiscSet(mDiscSetNum));
	for(RaidFileDiscSet::const_iterator i(discSet.begin()); i != discSet.end(); ++i)
	{
		if(std::find(toDelete.begin(), toDelete.end(), *i) == toDelete.end())
		{
			toDelete.push_back((*i) + DIRECTORY_SEPARATOR + mRootDir);
		}
	}

	// NamedLock will throw an exception if it can't delete the lockfile,
	// which it can't if it doesn't exist. Now that we've deleted the account,
	// nobody can open it anyway, so it's safe to unlock.
	mapFileSystem->ReleaseLock();

	int retcode = 0;

	// Thirdly, delete the directories...
	for(std::vector<std::string>::const_iterator d(toDelete.begin()); d != toDelete.end(); ++d)
	{
		BOX_NOTICE("Deleting store directory " << (*d) << "...");
		// Just use the rm command to delete the files
#ifdef WIN32
		std::string cmd("rmdir /s/q ");
		std::string dir = *d;

		// rmdir doesn't understand forward slashes, so replace them all.
		for(std::string::iterator i = dir.begin(); i != dir.end(); i++)
		{
			if(*i == '/')
			{
				*i = '\\';
			}
		}
		cmd += dir;
#else
		std::string cmd("rm -rf ");
		cmd += *d;
#endif
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


void BackupStoreAccountControl::OpenAccount(bool readWrite)
{
	if(mapFileSystem.get())
	{
		// Can use existing BackupFileSystem.

		if(readWrite && !mapFileSystem->HaveLock())
		{
			// Need to acquire a lock first.
			mapFileSystem->GetLock();
		}

		return;
	}

	// Load in the account database
	std::auto_ptr<BackupStoreAccountDatabase> db(
		BackupStoreAccountDatabase::Read(
			mConfig.GetKeyValue("AccountDatabase")));

	// Exists?
	if(!db->EntryExists(mAccountID))
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, AccountDoesNotExist,
			"Failed to open account " << BOX_FORMAT_ACCOUNT(mAccountID) <<
			": does not exist");
	}

	// Get info from the database
	BackupStoreAccounts acc(*db);
	acc.GetAccountRoot(mAccountID, mRootDir, mDiscSetNum);

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
		mapChangeUser.reset(new UnixUser(username));
		mapChangeUser->ChangeProcessUser(true /* temporary */);
		// Change will be undone when this BackupStoreAccountControl is destroyed
	}

	mapFileSystem.reset(new RaidBackupFileSystem(mAccountID, mRootDir, mDiscSetNum));

	if(readWrite)
	{
		mapFileSystem->GetLock();
	}
}


int BackupStoreAccountControl::CheckAccount(bool FixErrors, bool Quiet,
	bool ReturnNumErrorsFound)
{
	// Don't need a write lock if not making changes.
	OPEN_ACCOUNT(FixErrors); // readWrite

	// Check it
	BackupStoreCheck check(*mapFileSystem, FixErrors, Quiet);
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


int BackupStoreAccountControl::CreateAccount(int32_t DiscNumber, int32_t SoftLimit,
	int32_t HardLimit)
{
	CheckSoftHardLimits(SoftLimit, HardLimit);

	// Load in the account database
	std::auto_ptr<BackupStoreAccountDatabase> db(
		BackupStoreAccountDatabase::Read(
			mConfig.GetKeyValue("AccountDatabase")));

	// Already exists?
	if(db->EntryExists(mAccountID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(mAccountID) <<
			" already exists.");
		return 1;
	}

	db->AddEntry(mAccountID, DiscNumber);

	// As the original user, write the modified account database back
	db->Write();

	// Get the user under which the daemon runs
	std::string username;
	{
		const Configuration &rserverConfig(mConfig.GetSubConfiguration("Server"));
		if(rserverConfig.KeyExists("User"))
		{
			username = rserverConfig.GetKeyValue("User");
		}
	}

	// Become the user specified in the config file?
	if(!username.empty())
	{
		// Username specified, change...
		mapChangeUser.reset(new UnixUser(username));
		mapChangeUser->ChangeProcessUser(true /* temporary */);
		// Change will be undone when this BackupStoreAccountControl is destroyed
	}

	// Get directory name:
	BackupStoreAccounts acc(*db);
	acc.GetAccountRoot(mAccountID, mRootDir, mDiscSetNum);

	// Create the account root directory on disc:
	RaidFileWrite::CreateDirectory(mDiscSetNum, mRootDir, true /* recursive */);

	// Create the BackupStoreInfo and BackupStoreRefCountDatabase files:
	mapFileSystem.reset(new RaidBackupFileSystem(mAccountID, mRootDir, mDiscSetNum));
	BackupAccountControl::CreateAccount(mAccountID, SoftLimit, HardLimit, "");

	BOX_NOTICE("Account " << BOX_FORMAT_ACCOUNT(mAccountID) << " created.");

	return 0;
}


int BackupStoreAccountControl::HousekeepAccountNow()
{
	// Housekeeping locks the account itself, so we can't.
	OPEN_ACCOUNT(false); // readWrite

	HousekeepStoreAccount housekeeping(*mapFileSystem, NULL);
	bool success = housekeeping.DoHousekeeping();

	if(!success)
	{
		BOX_ERROR("Failed to lock account " << BOX_FORMAT_ACCOUNT(mAccountID)
			<< " for housekeeping: perhaps a client is "
			"still connected?");
		return 1;
	}
	else
	{
		BOX_TRACE("Finished housekeeping on account " <<
			BOX_FORMAT_ACCOUNT(mAccountID));
		return 0;
	}
}


S3BackupAccountControl::S3BackupAccountControl(const Configuration& config,
	bool machineReadableOutput)
: BackupAccountControl(config, machineReadableOutput)
{
	if(!mConfig.SubConfigurationExists("S3Store"))
	{
		THROW_EXCEPTION_MESSAGE(CommonException,
			InvalidConfiguration,
			"The S3Store configuration subsection is required "
			"when S3Store mode is enabled");
	}
	const Configuration s3config = mConfig.GetSubConfiguration("S3Store");

	mBasePath = s3config.GetKeyValue("BasePath");
	if(mBasePath.size() == 0)
	{
		mBasePath = "/";
	}
	else
	{
		if(mBasePath[0] != '/' || mBasePath[mBasePath.size() - 1] != '/')
		{
			THROW_EXCEPTION_MESSAGE(CommonException,
				InvalidConfiguration,
				"If S3Store.BasePath is not empty then it must start and "
				"end with a slash, e.g. '/subdir/', but it currently does not.");
		}
	}

	mapS3Client.reset(new S3Client(s3config));
	std::string cache_dir = s3config.GetKeyValue("CacheDirectory");
	mapFileSystem.reset(new S3BackupFileSystem(mConfig, mBasePath, cache_dir,
		*mapS3Client));
}


int S3BackupAccountControl::CreateAccount(const std::string& name, int32_t SoftLimit,
	int32_t HardLimit)
{
	// Try getting the info file. If we get a 200 response then it already
	// exists, and we should bail out. If we get a 404 then it's safe to
	// continue. Otherwise something else is wrong and we should bail out.
	S3BackupFileSystem& s3fs(*(S3BackupFileSystem *)(mapFileSystem.get()));
	try
	{
		// We expect this to throw a FileNotFound HTTPException.
		HideSpecificExceptionGuard guard(HTTPException::ExceptionType,
			HTTPException::FileNotFound);
		s3fs.GetBackupStoreInfoUncached();

		// If it doesn't, then the file already exists, which is bad.
		BOX_FATAL("The BackupStoreInfo file already exists at this URL: " <<
			s3fs.GetObjectURL(s3fs.GetMetadataURI(S3_INFO_FILE_NAME)));
		return 1;
	}
	catch(HTTPException &e)
	{
		if(EXCEPTION_IS_TYPE(e, HTTPException, FileNotFound))
		{
			// This is what we want to see, so don't do anything rash.
		}
		else
		{
			// This is not what we wanted to see, so reraise the exception.
			throw;
		}
	}

	// Create the BackupStoreInfo and BackupStoreRefCountDatabase files:
	BackupAccountControl::CreateAccount(S3_FAKE_ACCOUNT_ID, SoftLimit, HardLimit, name);

	return 0;
}
