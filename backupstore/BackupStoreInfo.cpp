// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreInfo.cpp
//		Purpose: Main backup store information storage
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>

#include "BackupStoreInfo.h"
#include "BackupStoreException.h"
#include "RaidFileWrite.h"
#include "RaidFileRead.h"

#include "MemLeakFindOn.h"

// set packing to one byte
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

// ******************
// make sure the defaults in CreateNew are modified!
// ******************
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
} info_StreamFormat;

#define INFO_MAGIC_VALUE	0x34832476

// Use default packing
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif

#ifdef NDEBUG
	#define 	NUM_DELETED_DIRS_BLOCK	256
#else
	#define 	NUM_DELETED_DIRS_BLOCK	2
#endif

#define INFO_FILENAME	"info"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::BackupStoreInfo()
//		Purpose: Default constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreInfo::BackupStoreInfo()
	: mAccountID(-1),
	  mDiscSet(-1),
	  mReadOnly(true),
	  mIsModified(false),
	  mClientStoreMarker(0),
	  mLastObjectIDUsed(-1),
	  mBlocksUsed(0),
	  mBlocksInOldFiles(0),
	  mBlocksInDeletedFiles(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::~BackupStoreInfo
//		Purpose: Destructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreInfo::~BackupStoreInfo()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::CreateNew(int32_t, const std::string &, int)
//		Purpose: Create a new info file on disc
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::CreateNew(int32_t AccountID, const std::string &rRootDir, int DiscSet, int64_t BlockSoftLimit, int64_t BlockHardLimit)
{
	// Initial header (is entire file)
	info_StreamFormat hdr = {
		htonl(INFO_MAGIC_VALUE), // mMagicValue
		htonl(AccountID), // mAccountID
		0, // mClientStoreMarker
		hton64(1), // mLastObjectIDUsed (which is the root directory)
		0, // mBlocksUsed
		0, // mBlocksInOldFiles
		0, // mBlocksInDeletedFiles
		0, // mBlocksInDirectories
		hton64(BlockSoftLimit), // mBlocksSoftLimit
		hton64(BlockHardLimit), // mBlocksHardLimit
		0, // mCurrentMarkNumber
		0, // mOptionsPresent
		0 // mNumberDeletedDirectories
	};
	
	// Generate the filename
	ASSERT(rRootDir[rRootDir.size() - 1] == DIRECTORY_SEPARATOR_ASCHAR);
	std::string fn(rRootDir + INFO_FILENAME);
	
	// Open the file for writing
	RaidFileWrite rf(DiscSet, fn);
	rf.Open(false);		// no overwriting, as this is a new file
	
	// Write header
	rf.Write(&hdr, sizeof(hdr));
	
	// Commit it to disc, converting it to RAID now
	rf.Commit(true);
	
	// Done.
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::Load(int32_t, const std::string &, int, bool)
//		Purpose: Loads the info from disc, given the root information. Can be marked as read only.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreInfo> BackupStoreInfo::Load(int32_t AccountID, const std::string &rRootDir, int DiscSet, bool ReadOnly, int64_t *pRevisionID)
{
	// Generate the filename
	std::string fn(rRootDir + DIRECTORY_SEPARATOR INFO_FILENAME);
	
	// Open the file for reading (passing on optional request for revision ID)
	std::auto_ptr<RaidFileRead> rf(RaidFileRead::Open(DiscSet, fn, pRevisionID));
	
	// Read in a header
	info_StreamFormat hdr;
	if(!rf->ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */))
	{
		THROW_EXCEPTION(BackupStoreException, CouldNotLoadStoreInfo)
	}
	
	// Check it
	if(ntohl(hdr.mMagicValue) != INFO_MAGIC_VALUE || (int32_t)ntohl(hdr.mAccountID) != AccountID)
	{
		THROW_EXCEPTION(BackupStoreException, BadStoreInfoOnLoad)
	}
	
	// Make new object
	std::auto_ptr<BackupStoreInfo> info(new BackupStoreInfo);
	
	// Put in basic location info
	info->mAccountID = AccountID;
	info->mDiscSet = DiscSet;
	info->mFilename = fn;
	info->mReadOnly = ReadOnly;
	
	// Insert info from file
	info->mClientStoreMarker	= ntoh64(hdr.mClientStoreMarker);
	info->mLastObjectIDUsed		= ntoh64(hdr.mLastObjectIDUsed);
	info->mBlocksUsed 			= ntoh64(hdr.mBlocksUsed);
	info->mBlocksInOldFiles 	= ntoh64(hdr.mBlocksInOldFiles);
	info->mBlocksInDeletedFiles	= ntoh64(hdr.mBlocksInDeletedFiles);
	info->mBlocksInDirectories	= ntoh64(hdr.mBlocksInDirectories);
	info->mBlocksSoftLimit		= ntoh64(hdr.mBlocksSoftLimit);
	info->mBlocksHardLimit		= ntoh64(hdr.mBlocksHardLimit);
	
	// Load up array of deleted objects
	int64_t numDelObj = ntoh64(hdr.mNumberDeletedDirectories);
	
	// Then load them in
	if(numDelObj > 0)
	{
		int64_t objs[NUM_DELETED_DIRS_BLOCK];
		
		int64_t toload = numDelObj;
		while(toload > 0)
		{
			// How many in this one?
			int b = (toload > NUM_DELETED_DIRS_BLOCK)?NUM_DELETED_DIRS_BLOCK:((int)(toload));
			
			if(!rf->ReadFullBuffer(objs, b * sizeof(int64_t), 0 /* not interested in bytes read if this fails */))
			{
				THROW_EXCEPTION(BackupStoreException, CouldNotLoadStoreInfo)
			}
			
			// Add them
			for(int t = 0; t < b; ++t)
			{
				info->mDeletedDirectories.push_back(ntoh64(objs[t]));
			}
			
			// Number loaded
			toload -= b;
		}
	}

	// Final check
	if(static_cast<int64_t>(info->mDeletedDirectories.size()) != numDelObj)
	{
		THROW_EXCEPTION(BackupStoreException, BadStoreInfoOnLoad)
	}
	
	// return it to caller
	return info;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::CreateForRegeneration(...)
//		Purpose: Return an object which can be used to save for regeneration.
//		Created: 23/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreInfo> BackupStoreInfo::CreateForRegeneration(int32_t AccountID, const std::string &rRootDir,
	int DiscSet, int64_t LastObjectID, int64_t BlocksUsed, int64_t BlocksInOldFiles,
	int64_t BlocksInDeletedFiles, int64_t BlocksInDirectories, int64_t BlockSoftLimit, int64_t BlockHardLimit)
{
	// Generate the filename
	std::string fn(rRootDir + DIRECTORY_SEPARATOR INFO_FILENAME);
	
	// Make new object
	std::auto_ptr<BackupStoreInfo> info(new BackupStoreInfo);
	
	// Put in basic info
	info->mAccountID = AccountID;
	info->mDiscSet = DiscSet;
	info->mFilename = fn;
	info->mReadOnly = false;
	
	// Insert info starting info
	info->mClientStoreMarker	= 0;
	info->mLastObjectIDUsed		= LastObjectID;
	info->mBlocksUsed 			= BlocksUsed;
	info->mBlocksInOldFiles 	= BlocksInOldFiles;
	info->mBlocksInDeletedFiles	= BlocksInDeletedFiles;
	info->mBlocksInDirectories	= BlocksInDirectories;
	info->mBlocksSoftLimit		= BlockSoftLimit;
	info->mBlocksHardLimit		= BlockHardLimit;
	
	// return it to caller
	return info;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::Save()
//		Purpose: Save modified info back to disc
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::Save()
{
	// Make sure we're initialised (although should never come to this)
	if(mFilename.empty() || mAccountID == -1 || mDiscSet == -1)
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}

	// Can we do this?
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	// Then... open a write file
	RaidFileWrite rf(mDiscSet, mFilename);
	rf.Open(true);		// allow overwriting
	
	// Make header
	info_StreamFormat hdr;
	hdr.mMagicValue 				= htonl(INFO_MAGIC_VALUE);
	hdr.mAccountID 					= htonl(mAccountID);
	hdr.mClientStoreMarker			= hton64(mClientStoreMarker);
	hdr.mLastObjectIDUsed			= hton64(mLastObjectIDUsed);
	hdr.mBlocksUsed 				= hton64(mBlocksUsed);
	hdr.mBlocksInOldFiles 			= hton64(mBlocksInOldFiles);
	hdr.mBlocksInDeletedFiles 		= hton64(mBlocksInDeletedFiles);
	hdr.mBlocksInDirectories		= hton64(mBlocksInDirectories);
	hdr.mBlocksSoftLimit			= hton64(mBlocksSoftLimit);
	hdr.mBlocksHardLimit			= hton64(mBlocksHardLimit);
	hdr.mCurrentMarkNumber			= 0;
	hdr.mOptionsPresent				= 0;
	hdr.mNumberDeletedDirectories	= hton64(mDeletedDirectories.size());
	
	// Write header
	rf.Write(&hdr, sizeof(hdr));
	
	// Write the deleted object list
	if(mDeletedDirectories.size() > 0)
	{
		int64_t objs[NUM_DELETED_DIRS_BLOCK];
		
		int tosave = mDeletedDirectories.size();
		std::vector<int64_t>::iterator i(mDeletedDirectories.begin());
		while(tosave > 0)
		{
			// How many in this one?
			int b = (tosave > NUM_DELETED_DIRS_BLOCK)?NUM_DELETED_DIRS_BLOCK:((int)(tosave));
			
			// Add them
			for(int t = 0; t < b; ++t)
			{
				ASSERT(i != mDeletedDirectories.end());
				objs[t] = hton64((*i));
				i++;
			}

			// Write			
			rf.Write(objs, b * sizeof(int64_t));
			
			// Number saved
			tosave -= b;
		}
	}

	// Commit it to disc, converting it to RAID now
	rf.Commit(true);
	
	// Mark is as not modified
	mIsModified = false;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksUsed(int32_t)
//		Purpose: Change number of blocks used, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksUsed(int64_t Delta)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	if((mBlocksUsed + Delta) < 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoBlockDeltaMakesValueNegative)
	}
	
	mBlocksUsed += Delta;
	
	mIsModified = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksInOldFiles(int32_t)
//		Purpose: Change number of blocks in old files, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksInOldFiles(int64_t Delta)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	if((mBlocksInOldFiles + Delta) < 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoBlockDeltaMakesValueNegative)
	}
	
	mBlocksInOldFiles += Delta;
	
	mIsModified = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksInDeletedFiles(int32_t)
//		Purpose: Change number of blocks in deleted files, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksInDeletedFiles(int64_t Delta)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	if((mBlocksInDeletedFiles + Delta) < 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoBlockDeltaMakesValueNegative)
	}
	
	mBlocksInDeletedFiles += Delta;
	
	mIsModified = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeBlocksInDirectories(int32_t)
//		Purpose: Change number of blocks in directories, by a delta amount
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeBlocksInDirectories(int64_t Delta)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	if((mBlocksInDirectories + Delta) < 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoBlockDeltaMakesValueNegative)
	}
	
	mBlocksInDirectories += Delta;
	
	mIsModified = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::CorrectAllUsedValues(int64_t, int64_t, int64_t, int64_t)
//		Purpose: Set all the usage counts to specific values -- use for correcting in housekeeping
//				 if something when wrong during the backup connection, and the store info wasn't
//				 saved back to disc.
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
void BackupStoreInfo::CorrectAllUsedValues(int64_t Used, int64_t InOldFiles, int64_t InDeletedFiles, int64_t InDirectories)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	// Set the values
	mBlocksUsed = Used;
	mBlocksInOldFiles = InOldFiles;
	mBlocksInDeletedFiles = InDeletedFiles;
	mBlocksInDirectories = InDirectories;
	
	mIsModified = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::AddDeletedDirectory(int64_t)
//		Purpose: Add a directory ID to the deleted list
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::AddDeletedDirectory(int64_t DirID)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	mDeletedDirectories.push_back(DirID);
	
	mIsModified = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::RemovedDeletedDirectory(int64_t)
//		Purpose: Remove a directory from the deleted list
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreInfo::RemovedDeletedDirectory(int64_t DirID)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	std::vector<int64_t>::iterator i(std::find(mDeletedDirectories.begin(), mDeletedDirectories.end(), DirID));
	if(i == mDeletedDirectories.end())
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoDirNotInList)
	}
	mDeletedDirectories.erase(i);
	
	mIsModified = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::ChangeLimits(int64_t, int64_t)
//		Purpose: Change the soft and hard limits
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
void BackupStoreInfo::ChangeLimits(int64_t BlockSoftLimit, int64_t BlockHardLimit)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}

	mBlocksSoftLimit = BlockSoftLimit;
	mBlocksHardLimit = BlockHardLimit;
	
	mIsModified = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::AllocateObjectID()
//		Purpose: Allocate an ID for a new object in the store.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
int64_t BackupStoreInfo::AllocateObjectID()
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	if(mLastObjectIDUsed < 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotInitialised)
	}
	
	// Return the next object ID
	return ++mLastObjectIDUsed;
	
	mIsModified = true;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreInfo::SetClientStoreMarker(int64_t)
//		Purpose: Sets the client store marker
//		Created: 2003/10/29
//
// --------------------------------------------------------------------------
void BackupStoreInfo::SetClientStoreMarker(int64_t ClientStoreMarker)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoIsReadOnly)
	}
	
	mClientStoreMarker = ClientStoreMarker;
	
	mIsModified = true;
}



