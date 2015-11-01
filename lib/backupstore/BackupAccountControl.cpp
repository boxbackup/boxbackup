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
#include "BackupStoreAccounts.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreInfo.h"
#include "Configuration.h"
#include "HTTPResponse.h"
#include "HousekeepStoreAccount.h"
#include "RaidFileController.h"
#include "UnixUser.h"

#include "MemLeakFindOn.h"

#define LOAD_BACKUP_STORE_INFO(readWrite) \
	if(!LoadBackupStoreInfo(readWrite)) \
	{ \
		return 1; \
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

int BackupAccountControl::PrintAccountInfo()
{
	LOAD_BACKUP_STORE_INFO(false); // !readWrite
	BackupStoreInfo& info(*mapStoreInfo);
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

bool BackupStoreAccountControl::LoadBackupStoreInfo(bool readWrite)
{
	if(!OpenAccount(true)) // readWrite
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(mAccountID));
		return false;
	}

	// Load the info
	mapStoreInfo = mapFileSystem->GetBackupStoreInfo(mAccountID, false); // !ReadOnly
	return true;
}

std::string BackupStoreAccountControl::GetAccountIdentifier()
{
	std::ostringstream oss;
	oss << BOX_FORMAT_ACCOUNT(mAccountID) << " (" <<
		mapStoreInfo->GetAccountName() << ")";
	return oss.str();
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
	LOAD_BACKUP_STORE_INFO(true); // readWrite

	mapStoreInfo->ChangeLimits(softlimit, hardlimit);
	mapFileSystem->PutBackupStoreInfo(*mapStoreInfo);

	BOX_NOTICE("Limits on account " << GetAccountIdentifier() << " changed to " <<
		softlimit << " blocks soft, " << hardlimit << " hard.");

	return 0;
}

int BackupAccountControl::SetAccountName(const std::string& rNewAccountName)
{
	LOAD_BACKUP_STORE_INFO(true); // readWrite
	mapStoreInfo->SetAccountName(rNewAccountName);
	mapFileSystem->PutBackupStoreInfo(*mapStoreInfo);

	BOX_NOTICE("Name of account " << GetAccountIdentifier() << " changed to " <<
		rNewAccountName);

	return 0;
}

int BackupAccountControl::SetAccountEnabled(bool enabled)
{
	LOAD_BACKUP_STORE_INFO(true); // readWrite
	mapStoreInfo->SetAccountEnabled(enabled);
	mapFileSystem->PutBackupStoreInfo(*mapStoreInfo);

	BOX_NOTICE("Account " << GetAccountIdentifier() << " is now " <<
		(enabled ? "enabled" : "disabled"));

	return 0;
}

int BackupStoreAccountControl::DeleteAccount(bool AskForConfirmation)
{
	// Obtain a write lock, as the daemon user
	if(!OpenAccount(true)) // readWrite
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(mAccountID)
			<< " for deletion.");
		return 1;
	}

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

bool BackupStoreAccountControl::OpenAccount(bool readWrite)
{
	// Load in the account database
	std::auto_ptr<BackupStoreAccountDatabase> db(
		BackupStoreAccountDatabase::Read(
			mConfig.GetKeyValue("AccountDatabase")));

	// Exists?
	if(!db->EntryExists(mAccountID))
	{
		BOX_ERROR("Account " << BOX_FORMAT_ACCOUNT(mAccountID) <<
			" does not exist.");
		return false;
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

	mapFileSystem.reset(new RaidBackupFileSystem(mRootDir, mDiscSetNum));

	if(readWrite)
	{
		mapFileSystem->GetLock();
	}

	return true;
}

int BackupStoreAccountControl::CheckAccount(bool FixErrors, bool Quiet,
	bool ReturnNumErrorsFound)
{
	if(!OpenAccount(FixErrors)) // don't need a write lock if not making changes
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(mAccountID)
			<< " for checking.");
		return 1;
	}

	// Check it
	BackupStoreCheck check(mRootDir, mDiscSetNum, mAccountID, FixErrors, Quiet);
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
	acc.Create(mAccountID, DiscNumber, SoftLimit, HardLimit, username);

	BOX_NOTICE("Account " << BOX_FORMAT_ACCOUNT(mAccountID) << " created.");

	return 0;
}

int BackupStoreAccountControl::HousekeepAccountNow()
{
	if(!OpenAccount(false)) // !readWrite; housekeeping locks the account itself
	{
		BOX_ERROR("Failed to open account " << BOX_FORMAT_ACCOUNT(mAccountID)
			<< " for housekeeping.");
		return 1;
	}

	HousekeepStoreAccount housekeeping(mAccountID, mRootDir, mDiscSetNum, NULL);
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

	std::string BasePath = s3config.GetKeyValue("BasePath");
	if(BasePath.size() == 0)
	{
		BasePath = "/";
	}
	else
	{
		if(BasePath[0] != '/' || BasePath[BasePath.size() - 1] != '/')
		{
			THROW_EXCEPTION_MESSAGE(CommonException,
				InvalidConfiguration,
				"If S3Store.BasePath is not empty then it must start and "
				"end with a slash, e.g. '/subdir/', but it currently does not.");
		}
	}

	mapS3Client.reset(new S3Client(
		s3config.GetKeyValue("HostName"),
		s3config.GetKeyValueInt("Port"),
		s3config.GetKeyValue("AccessKey"),
		s3config.GetKeyValue("SecretKey")));

	mapFileSystem.reset(new S3BackupFileSystem(mConfig, BasePath, *mapS3Client));
}

std::string S3BackupAccountControl::GetAccountIdentifier()
{
	std::ostringstream oss;
	oss << "'" << mapStoreInfo->GetAccountName() << "'";
	return oss.str();
}

int S3BackupAccountControl::CreateAccount(const std::string& name, int32_t SoftLimit,
	int32_t HardLimit)
{
	// Try getting the info file. If we get a 200 response then it already
	// exists, and we should bail out. If we get a 404 then it's safe to
	// continue. Otherwise something else is wrong and we should bail out.
	S3BackupFileSystem& s3fs(*(S3BackupFileSystem *)(mapFileSystem.get()));
	std::string info_url = s3fs.GetObjectURL(S3_INFO_FILE_NAME);
	HTTPResponse response = s3fs.GetObject(S3_INFO_FILE_NAME);
	if(response.GetResponseCode() != HTTPResponse::Code_NotFound)
	{
		BOX_FATAL("The BackupStoreInfo file already exists at this URL: " <<
			info_url);
		return 1;
	}
	else if(response.GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		// 404 not found is exactly what we want here.
	}
	else
	{
		BOX_FATAL("CreateAccount failed: " << info_url << ": " <<
			HTTPResponse::ResponseCodeToString(response.GetResponseCode()));
		return 1;
	}

	BackupStoreInfo info(0, // fake AccountID for S3 stores
		SoftLimit, HardLimit);
	info.SetAccountName(name);

	// And an empty directory
	BackupStoreDirectory rootDir(BACKUPSTORE_ROOT_DIRECTORY_ID, BACKUPSTORE_ROOT_DIRECTORY_ID);
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
	LOAD_BACKUP_STORE_INFO(false); // !readWrite
	ASSERT(info.ReportChangesTo(*mapStoreInfo) == 0);

	return 0;
}

bool S3BackupAccountControl::LoadBackupStoreInfo(bool readWrite)
{
	if(mapStoreInfo.get())
	{
		return true;
	}

	mapStoreInfo = mapFileSystem->GetBackupStoreInfo(
		0, // fake AccountID for S3 stores
		!readWrite); // ReadOnly

	return true;
}
