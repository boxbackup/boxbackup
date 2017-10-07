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
#include "S3Client.h"
#include "UnixUser.h"

class BackupStoreDirectory;
class BackupStoreInfo;
class Configuration;
class NamedLock;
class UnixUser;

class BackupAccountControl
{
protected:
	const Configuration& mConfig;
	bool mMachineReadableOutput;
	std::auto_ptr<BackupFileSystem> mapFileSystem;

	virtual void OpenAccount(bool readWrite) = 0;
	virtual int GetBlockSize()
	{
		return mapFileSystem->GetBlockSize();
	}
	virtual int SetLimit(int64_t softlimit, int64_t hardlimit);

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
	virtual int SetLimit(const char *SoftLimitStr, const char *HardLimitStr);
	virtual int SetAccountName(const std::string& rNewAccountName);
	virtual int PrintAccountInfo();
	virtual int SetAccountEnabled(bool enabled);
	virtual BackupFileSystem& GetFileSystem()
	{
		ASSERT(mapFileSystem.get() != NULL);
		return *mapFileSystem;
	}
	virtual BackupFileSystem* GetCurrentFileSystem() { return mapFileSystem.get(); }
	int CreateAccount(int32_t AccountID, int32_t SoftLimit, int32_t HardLimit,
		const std::string& AccountName);
};


class BackupStoreAccountControl : public BackupAccountControl
{
private:
	int32_t mAccountID;
	std::string mRootDir;
	int mDiscSetNum;
	std::auto_ptr<UnixUser> mapChangeUser; // used to reset uid when we return

	virtual void OpenAccount(bool readWrite);

public:
	BackupStoreAccountControl(const Configuration& config, int32_t AccountID,
		bool machineReadableOutput = false)
	: BackupAccountControl(config, machineReadableOutput),
	  mAccountID(AccountID),
	  mDiscSetNum(0)
	{ }
	virtual int GetBlockSize()
	{
		return BlockSizeOfDiscSet(mDiscSetNum);
	}
	int BlockSizeOfDiscSet(int discSetNum);
	int DeleteAccount(bool AskForConfirmation);
	int CheckAccount(bool FixErrors, bool Quiet,
		bool ReturnNumErrorsFound = false);
	int CreateAccount(int32_t DiscNumber, int32_t SoftLimit, int32_t HardLimit);
	int HousekeepAccountNow();
};

class S3BackupAccountControl : public BackupAccountControl
{
private:
	std::string mBasePath;
	std::auto_ptr<S3Client> mapS3Client;
	// mapFileSystem is inherited from BackupAccountControl

	virtual void OpenAccount(bool readWrite) { }

public:
	S3BackupAccountControl(const Configuration& config,
		bool machineReadableOutput = false);
	virtual ~S3BackupAccountControl()
	{
		// Destroy mapFileSystem before mapS3Client, because it may need it
		// for cleanup.
		mapFileSystem.reset();
	}
	int CreateAccount(const std::string& name, int32_t SoftLimit, int32_t HardLimit);
	int GetBlockSize() { return 4096; }
};

// max size of soft limit as percent of hard limit
#define MAX_SOFT_LIMIT_SIZE		97

#endif // BACKUPACCOUNTCONTROL__H

