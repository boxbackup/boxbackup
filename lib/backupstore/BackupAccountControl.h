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
#include "BackupFileSystem.h"
#include "NamedLock.h"
#include "S3Client.h"
#include "UnixUser.h"

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
	virtual ~BackupAccountControl() { }
	void CheckSoftHardLimits(int64_t SoftLimit, int64_t HardLimit);
	int64_t SizeStringToBlocks(const char *string, int BlockSize);
	std::string BlockSizeToString(int64_t Blocks, int64_t MaxBlocks, int BlockSize);
	int PrintAccountInfo(const BackupStoreInfo& info, int BlockSize);
};

class BackupStoreAccountsControl : public BackupAccountControl
{
public:
	BackupStoreAccountsControl(const Configuration& config,
		bool machineReadableOutput = false)
	: BackupAccountControl(config, machineReadableOutput)
	{ }
	int BlockSizeOfDiscSet(int discSetNum);
	bool OpenAccount(int32_t ID, std::string &rRootDirOut,
		int &rDiscSetOut, std::auto_ptr<UnixUser> apUser, NamedLock* pLock);
	int SetLimit(int32_t ID, const char *SoftLimitStr,
		const char *HardLimitStr);
	int SetAccountName(int32_t ID, const std::string& rNewAccountName);
	int PrintAccountInfo(int32_t ID);
	int SetAccountEnabled(int32_t ID, bool enabled);
	int DeleteAccount(int32_t ID, bool AskForConfirmation);
	int CheckAccount(int32_t ID, bool FixErrors, bool Quiet,
		bool ReturnNumErrorsFound = false);
	int CreateAccount(int32_t ID, int32_t DiscNumber, int32_t SoftLimit,
		int32_t HardLimit);
	int HousekeepAccountNow(int32_t ID);
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

#endif // BACKUPACCOUNTCONTROL__H

