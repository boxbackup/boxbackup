// --------------------------------------------------------------------------
//
// File
//		Name:    HousekeepStoreAccount.h
//		Purpose: Action class to perform housekeeping on a store account
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#ifndef HOUSEKEEPSTOREACCOUNT__H
#define HOUSEKEEPSTOREACCOUNT__H

#include <string>
#include <set>
#include <vector>

class BackupStoreDaemon;
class BackupStoreDirectory;


// --------------------------------------------------------------------------
//
// Class
//		Name:    HousekeepStoreAccount
//		Purpose: Action class to perform housekeeping on a store account
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
class HousekeepStoreAccount
{
public:
	HousekeepStoreAccount(int AccountID, const std::string &rStoreRoot, int StoreDiscSet, BackupStoreDaemon &rDaemon);
	~HousekeepStoreAccount();
	
	void DoHousekeeping();
	
	
private:
	// utility functions
	void MakeObjectFilename(int64_t ObjectID, std::string &rFilenameOut);

	bool ScanDirectory(int64_t ObjectID);
	bool DeleteFiles();
	bool DeleteEmptyDirectories();
	void DeleteFile(int64_t InDirectory, int64_t ObjectID, BackupStoreDirectory &rDirectory, const std::string &rDirectoryFilename, int64_t OriginalDirSizeInBlocks);

private:
	typedef struct
	{
		int64_t mObjectID;
		int64_t mInDirectory;
		int64_t mSizeInBlocks;
		int32_t mMarkNumber;
		int32_t mVersionAgeWithinMark;	// 0 == current, 1 latest old version, etc
	} DelEn;
	
	struct DelEnCompare
	{
		bool operator()(const DelEn &x, const DelEn &y);
	};
	
	int mAccountID;
	std::string mStoreRoot;
	int mStoreDiscSet;
	BackupStoreDaemon &mrDaemon;
	
	int64_t mDeletionSizeTarget;
	
	std::set<DelEn, DelEnCompare> mPotentialDeletions;
	int64_t mPotentialDeletionsTotalSize;
	int64_t mMaxSizeInPotentialDeletions;
	
	// List of directories which are empty, and might be good for deleting
	std::vector<int64_t> mEmptyDirectories;
	
	// The re-calculated blocks used stats
	int64_t mBlocksUsed;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;

	// Deltas from deletion
	int64_t mBlocksUsedDelta;
	int64_t mBlocksInOldFilesDelta;
	int64_t mBlocksInDeletedFilesDelta;
	int64_t mBlocksInDirectoriesDelta;
	
	// Deletion count
	int64_t mFilesDeleted;
	int64_t mEmptyDirectoriesDeleted;
	
	// Poll frequency
	int mCountUntilNextInterprocessMsgCheck;
};

#endif // HOUSEKEEPSTOREACCOUNT__H

