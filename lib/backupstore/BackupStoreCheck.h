// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreCheck.h
//		Purpose: Check a store for consistency
//		Created: 21/4/04
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTORECHECK__H
#define BACKUPSTORECHECK__H

#include <string>
#include <map>
#include <vector>
#include <set>

#include "NamedLock.h"
class IOStream;
class BackupStoreFilename;

/*

The following problems can be fixed:

	* Spurious files deleted
	* Corrupted files deleted
	* Root ID as file, deleted
	* Dirs with wrong object id inside, deleted
	* Direcetory entries pointing to non-existant files, deleted
	* Doubly references files have second reference deleted
	* Wrong directory container IDs fixed
	* Missing root recreated
	* Reattach files which exist, but aren't referenced
		- files go into directory original directory, if it still exists
		- missing directories are inferred, and recreated
		- or if all else fails, go into lost+found
		- file dir entries take the original name and mod time
		- directories go into lost+found
	* Container IDs on directories corrected
	* Inside directories,
		- only one object per name has old version clear
		- IDs aren't duplicated
	* Bad store info files regenerated
	* Bad sizes of files in directories fixed

*/


// Size of blocks in the list of IDs
#ifdef BOX_RELEASE_BUILD
	#define BACKUPSTORECHECK_BLOCK_SIZE		(64*1024)
#else
	#define BACKUPSTORECHECK_BLOCK_SIZE		8
#endif

// The object ID type -- can redefine to uint32_t to produce a lower memory version for smaller stores
typedef int64_t BackupStoreCheck_ID_t;
// Can redefine the size type for lower memory usage too
typedef int64_t BackupStoreCheck_Size_t;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreCheck
//		Purpose: Check a store for consistency
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
class BackupStoreCheck
{
public:
	BackupStoreCheck(const std::string &rStoreRoot, int DiscSetNumber, int32_t AccountID, bool FixErrors, bool Quiet);
	~BackupStoreCheck();
private:
	// no copying
	BackupStoreCheck(const BackupStoreCheck &);
	BackupStoreCheck &operator=(const BackupStoreCheck &);
public:

	// Do the exciting things
	void Check();
	
	bool ErrorsFound() {return mNumberErrorsFound > 0;}

private:
	enum
	{
		// Bit mask
		Flags_IsDir = 1,
		Flags_IsContained = 2,
		// Mask
		Flags__MASK = 3,
		// Number of bits
		Flags__NumFlags = 2,
		// Items per uint8_t
		Flags__NumItemsPerEntry = 4	// ie 8 / 2
	};

	typedef struct
	{
		// Note use arrays within the block, rather than the more obvious array of
		// objects, to be more memory efficient -- think alignment of the byte values.
		uint8_t mFlags[BACKUPSTORECHECK_BLOCK_SIZE * Flags__NumFlags / Flags__NumItemsPerEntry];
		BackupStoreCheck_ID_t mID[BACKUPSTORECHECK_BLOCK_SIZE];
		BackupStoreCheck_ID_t mContainer[BACKUPSTORECHECK_BLOCK_SIZE];
		BackupStoreCheck_Size_t mObjectSizeInBlocks[BACKUPSTORECHECK_BLOCK_SIZE];
	} IDBlock;
	
	// Phases of the check
	void CheckObjects();
	void CheckDirectories();
	void CheckRoot();
	void CheckUnattachedObjects();
	void FixDirsWithWrongContainerID();
	void FixDirsWithLostDirs();
	void WriteNewStoreInfo();

	// Checking functions
	int64_t CheckObjectsScanDir(int64_t StartID, int Level, const std::string &rDirName);
	void CheckObjectsDir(int64_t StartID);
	bool CheckAndAddObject(int64_t ObjectID, const std::string &rFilename);
	int64_t CheckFile(int64_t ObjectID, IOStream &rStream);
	int64_t CheckDirInitial(int64_t ObjectID, IOStream &rStream);	

	// Fixing functions
	bool TryToRecreateDirectory(int64_t MissingDirectoryID);
	void InsertObjectIntoDirectory(int64_t ObjectID, int64_t DirectoryID, bool IsDirectory);
	int64_t GetLostAndFoundDirID();
	void CreateBlankDirectory(int64_t DirectoryID, int64_t ContainingDirID);

	// Data handling
	void FreeInfo();
	void AddID(BackupStoreCheck_ID_t ID, BackupStoreCheck_ID_t Container, BackupStoreCheck_Size_t ObjectSize, bool IsFile);
	IDBlock *LookupID(BackupStoreCheck_ID_t ID, int32_t &rIndexOut);
	inline void SetFlags(IDBlock *pBlock, int32_t Index, uint8_t Flags)
	{
		ASSERT(pBlock != 0);
		ASSERT(Index < BACKUPSTORECHECK_BLOCK_SIZE);
		ASSERT(Flags < (1 << Flags__NumFlags));
		
		pBlock->mFlags[Index / Flags__NumItemsPerEntry]
			|= (Flags << ((Index % Flags__NumItemsPerEntry) * Flags__NumFlags));
	}
	inline uint8_t GetFlags(IDBlock *pBlock, int32_t Index)
	{
		ASSERT(pBlock != 0);
		ASSERT(Index < BACKUPSTORECHECK_BLOCK_SIZE);

		return (pBlock->mFlags[Index / Flags__NumItemsPerEntry] >> ((Index % Flags__NumItemsPerEntry) * Flags__NumFlags)) & Flags__MASK;
	}
	
#ifndef BOX_RELEASE_BUILD
	void DumpObjectInfo();
	#define DUMP_OBJECT_INFO DumpObjectInfo();
#else
	#define DUMP_OBJECT_INFO
#endif
	
private:
	std::string mStoreRoot;
	int mDiscSetNumber;
	int32_t mAccountID;
	bool mFixErrors;
	bool mQuiet;
	
	int64_t mNumberErrorsFound;
	
	// Lock for the store account
	NamedLock mAccountLock;
	
	// Storage for ID data
	typedef std::map<BackupStoreCheck_ID_t, IDBlock*> Info_t;
	Info_t mInfo;
	BackupStoreCheck_ID_t mLastIDInInfo;
	IDBlock *mpInfoLastBlock;
	int32_t mInfoLastBlockEntries;
	
	// List of stuff to fix
	std::vector<BackupStoreCheck_ID_t> mDirsWithWrongContainerID;
	// This is a map of lost dir ID -> existing dir ID
	std::map<BackupStoreCheck_ID_t, BackupStoreCheck_ID_t> mDirsWhichContainLostDirs;
	
	// Set of extra directories added
	std::set<BackupStoreCheck_ID_t> mDirsAdded;
	
	// Misc stuff
	int32_t mLostDirNameSerial;
	int64_t mLostAndFoundDirectoryID;
	
	// Usage
	int64_t mBlocksUsed;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;
};

#endif // BACKUPSTORECHECK__H

