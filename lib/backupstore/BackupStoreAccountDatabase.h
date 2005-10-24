// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreAccountDatabase.h
//		Purpose: Database of accounts for the backup store
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREACCOUNTDATABASE__H
#define BACKUPSTOREACCOUNTDATABASE__H

#include <memory>
#include <vector>

#include "BoxTime.h"

class _BackupStoreAccountDatabase;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreAccountDatabase
//		Purpose: Database of accounts for the backup store
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
class BackupStoreAccountDatabase
{
public:
	friend class _BackupStoreAccountDatabase;	// to stop compiler warnings
	~BackupStoreAccountDatabase();
private:
	BackupStoreAccountDatabase(const char *Filename);
	BackupStoreAccountDatabase(const BackupStoreAccountDatabase &);
public:

	static std::auto_ptr<BackupStoreAccountDatabase> Read(const char *Filename);
	void Write();

	class Entry
	{
	public:
		Entry();
		Entry(int32_t ID, int DiscSet);
		Entry(const Entry &rEntry);
		~Entry();

		int32_t GetID() const {return mID;}
		int GetDiscSet() const {return mDiscSet;}
		
	private:
		int32_t mID;
		int mDiscSet;
	};

	bool EntryExists(int32_t ID) const;
	const Entry &GetEntry(int32_t ID) const;
	void AddEntry(int32_t ID, int DiscSet);
	void DeleteEntry(int32_t ID);

	// This interface should change in the future. But for now it'll do.
	void GetAllAccountIDs(std::vector<int32_t> &rIDsOut);

private:
	void ReadFile() const;	// const in concept only
	void CheckUpToDate() const;	// const in concept only
	box_time_t GetDBFileModificationTime() const;

private:
	mutable _BackupStoreAccountDatabase *pImpl;
};

#endif // BACKUPSTOREACCOUNTDATABASE__H

