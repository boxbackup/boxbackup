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

#include "BackupStoreRefCountDatabase.h"

class BackupStoreDirectory;
class BackupFileSystem;

class HousekeepingCallback
{
	public:
	virtual ~HousekeepingCallback() {}
	virtual bool CheckForInterProcessMsg(int AccountNum = 0, int MaximumWaitTime = 0) = 0;
};

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
	HousekeepStoreAccount(BackupFileSystem& FileSystem,
		HousekeepingCallback* pHousekeepingCallback);
	~HousekeepStoreAccount();

	bool DoHousekeeping(bool KeepTryingForever = false);
	int GetErrorCount() { return mErrorCount; }

private:
	// utility functions
	bool ScanDirectory(int64_t ObjectID, BackupStoreInfo& rBackupStoreInfo);
	bool DeleteFiles(BackupStoreInfo& rBackupStoreInfo);
	void RemoveReferenceAndMaybeDeleteFile(int64_t FileID, int64_t DirID,
		const std::string& reason, BackupStoreInfo& rBackupStoreInfo);
	bool DeleteEmptyDirectories(BackupStoreInfo& rBackupStoreInfo);
	void DeleteEmptyDirectory(int64_t dirId, std::vector<int64_t>& rToExamine,
		BackupStoreInfo& rBackupStoreInfo);
	BackupStoreRefCountDatabase::refcount_t DeleteFile(int64_t InDirectory,
		int64_t ObjectID,
		BackupStoreDirectory &rDirectory,
		BackupStoreInfo& rBackupStoreInfo);
	void UpdateDirectorySize(BackupStoreDirectory &rDirectory,
		IOStream::pos_type old_size_in_blocks,
		IOStream::pos_type new_size_in_blocks);

	typedef struct
	{
		int64_t mObjectID;
		int64_t mInDirectory;
		int64_t mSizeInBlocks;
		int32_t mMarkNumber;
		int32_t mVersionAgeWithinMark;	// 0 == current, 1 latest old version, etc
		bool    mIsFlagDeleted; // false for files flagged "Old"
	} DelEn;

	struct DelEnCompare
	{
		bool operator()(const DelEn &x, const DelEn &y);
	};

	BackupFileSystem& mrFileSystem;
	HousekeepingCallback* mpHousekeepingCallback;

	int64_t mDeletionSizeTarget;

	std::vector<std::pair<int64_t, int64_t> > mDefiniteDeletions;
	std::set<DelEn, DelEnCompare> mPotentialDeletions;
	int64_t mPotentialDeletionsTotalSize;
	int64_t mMaxSizeInPotentialDeletions;

	// List of directories which are empty, and might be good for deleting
	std::vector<int64_t> mEmptyDirectories;

	// Count of errors found and fixed
	int64_t mErrorCount;

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

	// New reference count list
	BackupStoreRefCountDatabase* mpNewRefs;

	// Poll frequency
	int mCountUntilNextInterprocessMsgCheck;

	Logging::Tagger mTagWithClientID;
};

#endif // HOUSEKEEPSTOREACCOUNT__H

