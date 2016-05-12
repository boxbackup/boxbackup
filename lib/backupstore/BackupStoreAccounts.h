// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreAccounts.h
//		Purpose: Account management for backup store server
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREACCOUNTS__H
#define BACKUPSTOREACCOUNTS__H

#include <string>

#include "HousekeepStoreAccount.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupAccountControl.h"
#include "NamedLock.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreAccounts
//		Purpose: Account management for backup store server
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
class BackupStoreAccounts
{
public:
	BackupStoreAccounts(BackupStoreAccountDatabase &rDatabase);
	~BackupStoreAccounts();
private:
	BackupStoreAccounts(const BackupStoreAccounts &rToCopy);

public:
	void Create(int32_t ID, int DiscSet, int64_t SizeSoftLimit,
        int64_t SizeHardLimit, int32_t VersionsLimit, const std::string &rAsUsername);

	bool AccountExists(int32_t ID);
	void GetAccountRoot(int32_t ID, std::string &rRootDirOut, int &rDiscSetOut) const;
	static std::string GetAccountRoot(const
		BackupStoreAccountDatabase::Entry &rEntry)
	{
		return MakeAccountRootDir(rEntry.GetID(), rEntry.GetDiscSet());
	}
	void LockAccount(int32_t ID, NamedLock& rNamedLock);

private:
	static std::string MakeAccountRootDir(int32_t ID, int DiscSet);

private:
	BackupStoreAccountDatabase &mrDatabase;
};

class Configuration;
class UnixUser;


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
        const char *HardLimitStr, const char *VersionsLimitStr="0");
	int SetAccountName(int32_t ID, const std::string& rNewAccountName);
	int PrintAccountInfo(int32_t ID);
	int SetAccountEnabled(int32_t ID, bool enabled);
	int DeleteAccount(int32_t ID, bool AskForConfirmation);
	int CheckAccount(int32_t ID, bool FixErrors, bool Quiet,
		bool ReturnNumErrorsFound = false);
    int CreateAccount(int32_t ID, int32_t DiscNumber, int64_t SoftLimit,
        int64_t HardLimit, int32_t VersionsLimit);
	int HousekeepAccountNow(int32_t ID,  int32_t flags);
};

// max size of soft limit as percent of hard limit
#define MAX_SOFT_LIMIT_SIZE		97

#endif // BACKUPSTOREACCOUNTS__H

