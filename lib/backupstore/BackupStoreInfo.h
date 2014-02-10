// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreInfo.h
//		Purpose: Main backup store information storage
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREINFO__H
#define BACKUPSTOREINFO__H

#include <memory>
#include <string>
#include <vector>

#include "CollectInBufferStream.h"

class BackupStoreCheck;

// set packing to one byte
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

// ******************
// make sure the defaults in CreateNew are modified!
// ******************
// Old version, grandfathered, do not change!
typedef struct
{
	int32_t mMagicValue;	// also the version number
	int32_t mAccountID;
	int64_t mClientStoreMarker;
	int64_t mLastObjectIDUsed;
	int64_t mBlocksUsed;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;
	int64_t mBlocksSoftLimit;
	int64_t mBlocksHardLimit;
	uint32_t mCurrentMarkNumber;
	uint32_t mOptionsPresent;		// bit mask of optional elements present
	int64_t mNumberDeletedDirectories;
	// Then loads of int64_t IDs for the deleted directories
} info_StreamFormat_1;

#define INFO_MAGIC_VALUE_1	0x34832476
#define INFO_MAGIC_VALUE_2	0x494e4632 /* INF2 */

// Use default packing
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif

#define INFO_FILENAME	"info"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreInfo
//		Purpose: Main backup store information storage
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
class BackupStoreInfo
{
	friend class BackupStoreCheck;
public:
	~BackupStoreInfo();
private:
	// Creation through static functions only
	BackupStoreInfo();
	// No copying allowed
	BackupStoreInfo(const BackupStoreInfo &);
	
public:
	// Create a New account, saving a blank info object to the disc
	static void CreateNew(int32_t AccountID, const std::string &rRootDir, int DiscSet, int64_t BlockSoftLimit, int64_t BlockHardLimit);
	
	// Load it from the store
	static std::auto_ptr<BackupStoreInfo> Load(int32_t AccountID, const std::string &rRootDir, int DiscSet, bool ReadOnly, int64_t *pRevisionID = 0);
	
	// Has info been modified?
	bool IsModified() const {return mIsModified;}
	
	// Save modified infomation back to store
	void Save(bool allowOverwrite = true);
	
	// Data access functions
	int32_t GetAccountID() const {return mAccountID;}
	int64_t GetLastObjectIDUsed() const {return mLastObjectIDUsed;}
	int64_t GetBlocksUsed() const {return mBlocksUsed;}
	int64_t GetBlocksInCurrentFiles() const {return mBlocksInCurrentFiles;}
	int64_t GetBlocksInOldFiles() const {return mBlocksInOldFiles;}
	int64_t GetBlocksInDeletedFiles() const {return mBlocksInDeletedFiles;}
	int64_t GetBlocksInDirectories() const {return mBlocksInDirectories;}
	const std::vector<int64_t> &GetDeletedDirectories() const {return mDeletedDirectories;}
	int64_t GetBlocksSoftLimit() const {return mBlocksSoftLimit;}
	int64_t GetBlocksHardLimit() const {return mBlocksHardLimit;}
	int64_t GetNumCurrentFiles() const {return mNumCurrentFiles;}
	int64_t GetNumOldFiles() const {return mNumOldFiles;}
	int64_t GetNumDeletedFiles() const {return mNumDeletedFiles;}
	int64_t GetNumDirectories() const {return mNumDirectories;}
	bool IsAccountEnabled() const {return mAccountEnabled;}
	bool IsReadOnly() const {return mReadOnly;}
	int GetDiscSetNumber() const {return mDiscSet;}

	int ReportChangesTo(BackupStoreInfo& rOldInfo);
	
	// Data modification functions
	void ChangeBlocksUsed(int64_t Delta);
	void ChangeBlocksInCurrentFiles(int64_t Delta);
	void ChangeBlocksInOldFiles(int64_t Delta);
	void ChangeBlocksInDeletedFiles(int64_t Delta);
	void ChangeBlocksInDirectories(int64_t Delta);
	void CorrectAllUsedValues(int64_t Used, int64_t InOldFiles, int64_t InDeletedFiles, int64_t InDirectories);
	void AddDeletedDirectory(int64_t DirID);
	void RemovedDeletedDirectory(int64_t DirID);
	void ChangeLimits(int64_t BlockSoftLimit, int64_t BlockHardLimit);
	void AdjustNumCurrentFiles(int64_t increase);
	void AdjustNumOldFiles(int64_t increase);
	void AdjustNumDeletedFiles(int64_t increase);
	void AdjustNumDirectories(int64_t increase);
	
	// Object IDs
	int64_t AllocateObjectID();
		
	// Client marker set and get
	int64_t GetClientStoreMarker() const {return mClientStoreMarker;}
	void SetClientStoreMarker(int64_t ClientStoreMarker);

	const std::string& GetAccountName() const { return mAccountName; }
	void SetAccountName(const std::string& rName);
	const CollectInBufferStream& GetExtraData() const { return mExtraData; }
	void SetAccountEnabled(bool IsEnabled) {mAccountEnabled = IsEnabled; }

	/**
         * @return a new BackupStoreInfo with the requested properties.
	 * This is exposed to allow testing, do not use otherwise!
	 */
	static std::auto_ptr<BackupStoreInfo> CreateForRegeneration(
		int32_t AccountID, const std::string &rAccountName,
		const std::string &rRootDir, int DiscSet,
		int64_t LastObjectID, int64_t BlocksUsed,
		int64_t BlocksInCurrentFiles, int64_t BlocksInOldFiles,
		int64_t BlocksInDeletedFiles, int64_t BlocksInDirectories,
		int64_t BlockSoftLimit, int64_t BlockHardLimit,
		bool AccountEnabled, IOStream& ExtraData);

	typedef struct
	{
		int64_t mLastObjectIDUsed;
		int64_t mBlocksUsed;
		int64_t mBlocksInCurrentFiles;
		int64_t mBlocksInOldFiles;
		int64_t mBlocksInDeletedFiles;
		int64_t mBlocksInDirectories;
		int64_t mNumCurrentFiles;
		int64_t mNumOldFiles;
		int64_t mNumDeletedFiles;
		int64_t mNumDirectories;
	} Adjustment;

private:
	// Location information
	// Be VERY careful about changing types of these values, as
	// they now define the sizes of fields on disk (via Archive).
	int32_t mAccountID;
	std::string mAccountName;
	int mDiscSet;
	std::string mFilename;
	bool mReadOnly;
	bool mIsModified;
	
	// Client infomation
	int64_t mClientStoreMarker;
	
	// Account information
	int64_t mLastObjectIDUsed;
	int64_t mBlocksUsed;
	int64_t mBlocksInCurrentFiles;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;
	int64_t mBlocksSoftLimit;
	int64_t mBlocksHardLimit;
	int64_t mNumCurrentFiles;
	int64_t mNumOldFiles;
	int64_t mNumDeletedFiles;
	int64_t mNumDirectories;
	std::vector<int64_t> mDeletedDirectories;
	bool mAccountEnabled;
	CollectInBufferStream mExtraData;
};

#endif // BACKUPSTOREINFO__H


