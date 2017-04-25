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

#include "BackupFileSystem.h"
#include "BackupStoreDirectory.h"
#include "NamedLock.h"
#include "Utils.h" // for object_exists_t

class IOStream;
class BackupStoreFilename;
class BackupStoreRefCountDatabase;

/*

The following problems can be fixed:

	* Spurious files deleted
	* Corrupted files deleted
	* Root ID as file, deleted
	* Dirs with wrong object id in header, deleted
	* Doubly referenced files have second reference deleted
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
		- entries pointing to non-existant files are deleted
		- patches depending on non-existent objects are deleted
	* Bad store info and refcount files regenerated
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
	BackupStoreCheck(BackupFileSystem& rFileSystem, bool FixErrors, bool Quiet);
	~BackupStoreCheck();

private:
	// no copying
	BackupStoreCheck(const BackupStoreCheck &);
	BackupStoreCheck &operator=(const BackupStoreCheck &);

public:
	// Do the exciting things
	void Check();

	bool ErrorsFound() {return mNumberErrorsFound > 0;}
	inline int64_t GetNumErrorsFound()
	{
		return mNumberErrorsFound;
	}

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
	object_exists_t CheckAndAddObject(int64_t ObjectID);
	bool CheckDirectory(BackupStoreDirectory& dir);
	bool CheckDirectoryEntry(BackupStoreDirectory::Entry& rEntry,
		int64_t DirectoryID, bool& rIsModified);
	void CountDirectoryEntries(BackupStoreDirectory& dir);
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
	int32_t mAccountID;
	std::string mAccountName;
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
	std::map<BackupStoreCheck_ID_t, BackupStoreCheck_ID_t>
		mDirsWhichContainLostDirs;

	// Set of extra directories added
	std::set<BackupStoreCheck_ID_t> mDirsAdded;

	// The refcount database, being reconstructed as the check/fix progresses
	BackupStoreRefCountDatabase* mpNewRefs;
	// And a holder for the auto_ptr to a new refcount DB in the temporary directory
	// (not the one created by BackupFileSystem::GetPotentialRefCountDatabase()):
	std::auto_ptr<BackupStoreRefCountDatabase> mapOwnNewRefs;
	// Abstracted interface to software-RAID filesystem
	BackupFileSystem& mrFileSystem;

	// Misc stuff
	int32_t mLostDirNameSerial;
	int64_t mLostAndFoundDirectoryID;

	// Usage
	int64_t mBlocksUsed;
	int64_t mBlocksInCurrentFiles;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;
	int64_t mNumCurrentFiles;
	int64_t mNumOldFiles;
	int64_t mNumDeletedFiles;
	int64_t mNumDirectories;
	int mTimeout;
};

#endif // BACKUPSTORECHECK__H

