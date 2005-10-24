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

class BackupStoreAccountDatabase;

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
	void Create(int32_t ID, int DiscSet, int64_t SizeSoftLimit, int64_t SizeHardLimit, const std::string &rAsUsername);

	bool AccountExists(int32_t ID);
	void GetAccountRoot(int32_t ID, std::string &rRootDirOut, int &rDiscSetOut) const;

private:
	std::string MakeAccountRootDir(int32_t ID, int DiscSet) const;

private:
	BackupStoreAccountDatabase &mrDatabase;
};

#endif // BACKUPSTOREACCOUNTS__H

