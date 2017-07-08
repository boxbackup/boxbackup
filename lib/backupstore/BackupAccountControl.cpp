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

#include "autogen_CommonException.h"
#include "autogen_BackupStoreException.h"
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
#include "NamedLock.h"
#include "RaidFileController.h"
#include "UnixUser.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

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

int BackupAccountControl::PrintAccountInfo(const BackupStoreInfo& info,
	int BlockSize)
{
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

int BackupStoreAccountControl::SetLimit(int32_t ID, const char *SoftLimitStr,
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
	int blocksize = BlockSizeOfDiscSet(discSetNum);
	int64_t softlimit = SizeStringToBlocks(SoftLimitStr, blocksize);
	int64_t hardlimit = SizeStringToBlocks(HardLimitStr, blocksize);
	CheckSoftHardLimits(softlimit, hardlimit);
	info->ChangeLimits(softlimit, hardlimit);

	// Save
	info->Save();

	BOX_NOTICE("Limits on account " << BOX_FORMAT_ACCOUNT(ID) <<
		" changed to " << softlimit << " soft, " <<
		hardlimit << " hard.");

	return 0;
}

int BackupStoreAccountControl::SetAccountName(int32_t ID, const std::string& rNewAccountName)
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

int BackupStoreAccountControl::PrintAccountInfo(int32_t ID)
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

	return BackupAccountControl::PrintAccountInfo(*info,
		BlockSizeOfDiscSet(discSetNum));
}

int BackupStoreAccountControl::SetAccountEnabled(int32_t ID, bool enabled)
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

int BackupStoreAccountControl::DeleteAccount(int32_t ID, bool AskForConfirmation)
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

bool BackupStoreAccountControl::OpenAccount(int32_t ID, std::string &rRootDirOut,
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

int BackupStoreAccountControl::CheckAccount(int32_t ID, bool FixErrors, bool Quiet,
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

int BackupStoreAccountControl::CreateAccount(int32_t ID, int32_t DiscNumber,
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

int BackupStoreAccountControl::HousekeepAccountNow(int32_t ID)
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

	mapS3Client.reset(new S3Client(
		s3config.GetKeyValue("HostName"),
		s3config.GetKeyValueInt("Port"),
		s3config.GetKeyValue("AccessKey"),
		s3config.GetKeyValue("SecretKey")));

	mapFileSystem.reset(new S3BackupFileSystem(mConfig, mBasePath, *mapS3Client));
}

std::string S3BackupAccountControl::GetFullURL(const std::string ObjectPath) const
{
	const Configuration s3config = mConfig.GetSubConfiguration("S3Store");
	return std::string("http://") + s3config.GetKeyValue("HostName") + ":" +
		s3config.GetKeyValue("Port") + GetFullPath(ObjectPath);
}

int S3BackupAccountControl::CreateAccount(const std::string& name, int32_t SoftLimit,
	int32_t HardLimit)
{
	// Try getting the info file. If we get a 200 response then it already
	// exists, and we should bail out. If we get a 404 then it's safe to
	// continue. Otherwise something else is wrong and we should bail out.
	std::string info_url = GetFullURL(S3_INFO_FILE_NAME);

	HTTPResponse response = GetObject(S3_INFO_FILE_NAME);
	if(response.GetResponseCode() == HTTPResponse::Code_OK)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, AccountAlreadyExists,
			"The BackupStoreInfo file already exists at this URL: " <<
			info_url);
	}

	if(response.GetResponseCode() != HTTPResponse::Code_NotFound)
	{
		mapS3Client->CheckResponse(response, std::string("Failed to check for an "
			"existing BackupStoreInfo file at this URL: ") + info_url);
	}

	BackupStoreInfo info(0, // fake AccountID for S3 stores
		info_url, // FileName,
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

	CollectInBufferStream out;
	info.Save(out);
	out.SetForReading();

	response = PutObject(S3_INFO_FILE_NAME, out);
	mapS3Client->CheckResponse(response, std::string("Failed to upload the new BackupStoreInfo "
		"file to this URL: ") + info_url);

	// Now get the file again, to check that it really worked.
	response = GetObject(S3_INFO_FILE_NAME);
	mapS3Client->CheckResponse(response, std::string("Failed to download the new BackupStoreInfo "
		"file that we just created: ") + info_url);

	return 0;
}

