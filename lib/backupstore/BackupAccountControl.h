// --------------------------------------------------------------------------
//
// File
//		Name:    BackupAccountControl.h
//		Purpose: Client-side account management for Amazon S3 stores
//		Created: 2015/06/27
//
// --------------------------------------------------------------------------

#ifndef BACKUPACCOUNTCONTROL__H
#define BACKUPACCOUNTCONTROL__H

#include <string>

#include "BackupStoreAccountDatabase.h"
#include "HTTPResponse.h"
#include "NamedLock.h"
#include "S3Client.h"

class BackupStoreDirectory;
class BackupStoreInfo;
class Configuration;

class BackupAccountControl
{
protected:
	const Configuration& mConfig;
	bool mMachineReadableOutput;

public:
	BackupAccountControl(const Configuration& config,
		bool machineReadableOutput = false)
	: mConfig(config),
	  mMachineReadableOutput(machineReadableOutput)
	{ }
	void CheckSoftHardLimits(int64_t SoftLimit, int64_t HardLimit);
	int64_t SizeStringToBlocks(const char *string, int BlockSize);
	std::string BlockSizeToString(int64_t Blocks, int64_t MaxBlocks, int BlockSize);
	int PrintAccountInfo(const BackupStoreInfo& info, int BlockSize);
};

class S3BackupFileSystem
{
private:
	std::string mBasePath;
	S3Client& mrClient;
public:
	S3BackupFileSystem(const Configuration& config, const std::string& BasePath,
		S3Client& rClient)
	: mBasePath(BasePath),
	  mrClient(rClient)
	{ }
	std::string GetDirectoryURI(int64_t ObjectID);
	std::auto_ptr<HTTPResponse> GetDirectory(BackupStoreDirectory& rDir);
	int PutDirectory(BackupStoreDirectory& rDir);
};

class S3BackupAccountControl : public BackupAccountControl
{
private:
	std::string mBasePath;
	std::auto_ptr<S3Client> mapS3Client;
	std::auto_ptr<S3BackupFileSystem> mapFileSystem;
public:
	S3BackupAccountControl(const Configuration& config,
		bool machineReadableOutput = false);
	std::string GetFullPath(const std::string ObjectPath) const
	{
		return mBasePath + ObjectPath;
	}
	std::string GetFullURL(const std::string ObjectPath) const;
	int CreateAccount(const std::string& name, int32_t SoftLimit, int32_t HardLimit);
	int GetBlockSize() { return 4096; }
	HTTPResponse GetObject(const std::string& name)
	{
		return mapS3Client->GetObject(GetFullPath(name));
	}
	HTTPResponse PutObject(const std::string& name, IOStream& rStreamToSend,
		const char* pContentType = NULL)
	{
		return mapS3Client->PutObject(GetFullPath(name), rStreamToSend,
			pContentType);
	}
};

// max size of soft limit as percent of hard limit
#define MAX_SOFT_LIMIT_SIZE		97
#define S3_INFO_FILE_NAME		"boxbackup.info"
#define S3_NOTIONAL_BLOCK_SIZE		1048576

#endif // BACKUPACCOUNTCONTROL__H

