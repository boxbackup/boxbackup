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
#include <iostream>

#include "autogen_CommonException.h"
#include "autogen_BackupStoreException.h"
#include "BackupAccountControl.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreInfo.h"
#include "Configuration.h"
#include "HTTPResponse.h"
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
    std::cout << FormatUsageLineStart("Version Count limit", mMachineReadableOutput) <<
        info.GetVersionCountLimit() << std::endl;
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

int S3BackupAccountControl::CreateAccount(const std::string& name, int64_t SoftLimit,
    int64_t HardLimit, int32_t VersionsLimit)
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
        SoftLimit, HardLimit, VersionsLimit);
	info.SetAccountName(name);

	// And an empty directory
	BackupStoreDirectory rootDir(BACKUPSTORE_ROOT_DIRECTORY_ID, BACKUPSTORE_ROOT_DIRECTORY_ID);
	int64_t rootDirSize = mapFileSystem->PutDirectory(rootDir);

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

std::string S3BackupFileSystem::GetDirectoryURI(int64_t ObjectID)
{
	std::ostringstream out;
	out << mBasePath << "dirs/" << BOX_FORMAT_OBJECTID(ObjectID) << ".dir";
	return out.str();
}

std::auto_ptr<HTTPResponse> S3BackupFileSystem::GetDirectory(BackupStoreDirectory& rDir)
{
	std::string uri = GetDirectoryURI(rDir.GetObjectID());
	HTTPResponse response = mrClient.GetObject(uri);
	mrClient.CheckResponse(response,
		std::string("Failed to download directory: ") + uri);
	return std::auto_ptr<HTTPResponse>(new HTTPResponse(response));
}

int S3BackupFileSystem::PutDirectory(BackupStoreDirectory& rDir)
{
	CollectInBufferStream out;
	rDir.WriteToStream(out);
	out.SetForReading();

	std::string uri = GetDirectoryURI(rDir.GetObjectID());
	HTTPResponse response = mrClient.PutObject(uri, out);
	mrClient.CheckResponse(response,
		std::string("Failed to upload directory: ") + uri);

	int blocks = (out.GetSize() + S3_NOTIONAL_BLOCK_SIZE - 1) / S3_NOTIONAL_BLOCK_SIZE;
	return blocks;
}

