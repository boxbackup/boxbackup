// --------------------------------------------------------------------------
//
// File
//		Name:    HousekeepStoreAccount.cpp
//		Purpose: 
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <map>
#include <stdio.h>

#include "HousekeepStoreAccount.h"
#include "BackupStoreDaemon.h"
#include "StoreStructure.h"
#include "BackupStoreConstants.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreInfo.h"
#include "NamedLock.h"
#include "autogen_BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BufferedStream.h"

#include "MemLeakFindOn.h"

// check every 32 directories scanned/files deleted
#define POLL_INTERPROCESS_MSG_CHECK_FREQUENCY	32

// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::HousekeepStoreAccount(int, const std::string &, int, BackupStoreDaemon &)
//		Purpose: Constructor
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
HousekeepStoreAccount::HousekeepStoreAccount(int AccountID, const std::string &rStoreRoot, int StoreDiscSet, BackupStoreDaemon &rDaemon)
	: mAccountID(AccountID),
	  mStoreRoot(rStoreRoot),
	  mStoreDiscSet(StoreDiscSet),
	  mrDaemon(rDaemon),
	  mDeletionSizeTarget(0),
  	  mPotentialDeletionsTotalSize(0),
	  mMaxSizeInPotentialDeletions(0),
	  mBlocksUsed(0),
	  mBlocksInOldFiles(0),
	  mBlocksInDeletedFiles(0),
	  mBlocksInDirectories(0),
	  mBlocksUsedDelta(0),
	  mBlocksInOldFilesDelta(0),
	  mBlocksInDeletedFilesDelta(0),
	  mBlocksInDirectoriesDelta(0),
	  mFilesDeleted(0),
	  mEmptyDirectoriesDeleted(0),
	  mCountUntilNextInterprocessMsgCheck(POLL_INTERPROCESS_MSG_CHECK_FREQUENCY)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::~HousekeepStoreAccount()
//		Purpose: Destructor
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
HousekeepStoreAccount::~HousekeepStoreAccount()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::DoHousekeeping()
//		Purpose: Perform the housekeeping
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void HousekeepStoreAccount::DoHousekeeping()
{
	// Attempt to lock the account
	std::string writeLockFilename;
	StoreStructure::MakeWriteLockFilename(mStoreRoot, mStoreDiscSet, writeLockFilename);
	NamedLock writeLock;
	if(!writeLock.TryAndGetLock(writeLockFilename.c_str(), 0600 /* restrictive file permissions */))
	{
		// Couldn't lock the account -- just stop now
		return;
	}

	// Load the store info to find necessary info for the housekeeping
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(mAccountID, mStoreRoot, mStoreDiscSet, false /* Read/Write */));

	// Calculate how much should be deleted
	mDeletionSizeTarget = info->GetBlocksUsed() - info->GetBlocksSoftLimit();
	if(mDeletionSizeTarget < 0)
	{
		mDeletionSizeTarget = 0;
	}

	// Scan the directory for potential things to delete
	// This will also remove elegiable items marked with RemoveASAP
	bool continueHousekeeping = ScanDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID);

	// If scan directory stopped for some reason, probably parent instructed to teminate, stop now.
	if(!continueHousekeeping)
	{
		// If any files were marked "delete now", then update the size of the store.
		if(mBlocksUsedDelta != 0 || mBlocksInOldFilesDelta != 0 || mBlocksInDeletedFilesDelta != 0)
		{
			info->ChangeBlocksUsed(mBlocksUsedDelta);
			info->ChangeBlocksInOldFiles(mBlocksInOldFilesDelta);
			info->ChangeBlocksInDeletedFiles(mBlocksInDeletedFilesDelta);
			
			// Save the store info back
			info->Save();
		}
	
		return;
	}

	// Log any difference in opinion between the values recorded in the store info, and
	// the values just calculated for space usage.
	// BLOCK
	{
		int64_t used = info->GetBlocksUsed();
		int64_t usedOld = info->GetBlocksInOldFiles();
		int64_t usedDeleted = info->GetBlocksInDeletedFiles();
		int64_t usedDirectories = info->GetBlocksInDirectories();

		// If the counts were wrong, taking into account RemoveASAP items deleted, log a message
		if((used + mBlocksUsedDelta) != mBlocksUsed || (usedOld + mBlocksInOldFilesDelta) != mBlocksInOldFiles
			|| (usedDeleted + mBlocksInDeletedFilesDelta) != mBlocksInDeletedFiles || usedDirectories != mBlocksInDirectories)
		{
			// Log this
			::syslog(LOG_ERR, "On housekeeping, sizes in store do not match calculated sizes, correcting");
			::syslog(LOG_ERR, "different (store,calc): acc 0x%08x, used (%lld,%lld), old (%lld,%lld), deleted (%lld,%lld), dirs (%lld,%lld)",
				mAccountID,
				(used + mBlocksUsedDelta), mBlocksUsed, (usedOld + mBlocksInOldFilesDelta), mBlocksInOldFiles,
				(usedDeleted + mBlocksInDeletedFilesDelta), mBlocksInDeletedFiles, usedDirectories, mBlocksInDirectories);
		}
		
		// If the current values don't match, store them
		if(used != mBlocksUsed || usedOld != mBlocksInOldFiles
			|| usedDeleted != mBlocksInDeletedFiles || usedDirectories != (mBlocksInDirectories + mBlocksInDirectoriesDelta))
		{	
			// Set corrected values in store info
			info->CorrectAllUsedValues(mBlocksUsed, mBlocksInOldFiles, mBlocksInDeletedFiles, mBlocksInDirectories + mBlocksInDirectoriesDelta);
			info->Save();
		}
	}
	
	// Reset the delta counts for files, as they will include RemoveASAP flagged files deleted
	// during the initial scan.
	int64_t removeASAPBlocksUsedDelta = mBlocksUsedDelta;	// keep for reporting
	mBlocksUsedDelta = 0;
	mBlocksInOldFilesDelta = 0;
	mBlocksInDeletedFilesDelta = 0;
	
	// Go and delete items from the accounts
	bool deleteInterrupted = DeleteFiles();
	
	// If that wasn't interrupted, remove any empty directories which are also marked as deleted in their containing directory
	if(!deleteInterrupted)
	{
		deleteInterrupted = DeleteEmptyDirectories();
	}
	
	// Log deletion if anything was deleted
	if(mFilesDeleted > 0 || mEmptyDirectoriesDeleted > 0)
	{
		::syslog(LOG_INFO, "Account 0x%08x, removed %lld blocks (%lld files, %lld dirs)%s", mAccountID, 0 - (mBlocksUsedDelta + removeASAPBlocksUsedDelta),
			mFilesDeleted, mEmptyDirectoriesDeleted,
			deleteInterrupted?" was interrupted":"");
	}
	
	// Make sure the delta's won't cause problems if the counts are really wrong, and
	// it wasn't fixed because the store was updated during the scan.
	if(mBlocksUsedDelta 			< (0 - info->GetBlocksUsed())) 				mBlocksUsedDelta = 			(0 - info->GetBlocksUsed());
	if(mBlocksInOldFilesDelta 		< (0 - info->GetBlocksInOldFiles())) 		mBlocksInOldFilesDelta = 	(0 - info->GetBlocksInOldFiles());
	if(mBlocksInDeletedFilesDelta 	< (0 - info->GetBlocksInDeletedFiles())) 	mBlocksInDeletedFilesDelta =(0 - info->GetBlocksInDeletedFiles());
	if(mBlocksInDirectoriesDelta 	< (0 - info->GetBlocksInDirectories()))		mBlocksInDirectoriesDelta = (0 - info->GetBlocksInDirectories());
	
	// Update the usage counts in the store
	info->ChangeBlocksUsed(mBlocksUsedDelta);
	info->ChangeBlocksInOldFiles(mBlocksInOldFilesDelta);
	info->ChangeBlocksInDeletedFiles(mBlocksInDeletedFilesDelta);
	info->ChangeBlocksInDirectories(mBlocksInDirectoriesDelta);
	
	// Save the store info back
	info->Save();
	
	// Explicity release the lock (would happen automatically on going out of scope, included for code clarity)
	writeLock.ReleaseLock();
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::MakeObjectFilename(int64_t, std::string &)
//		Purpose: Generate and place the filename for a given object ID
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void HousekeepStoreAccount::MakeObjectFilename(int64_t ObjectID, std::string &rFilenameOut)
{
	// Delegate to utility function
	StoreStructure::MakeObjectFilename(ObjectID, mStoreRoot, mStoreDiscSet, rFilenameOut, false /* don't bother ensuring the directory exists */);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::ScanDirectory(int64_t)
//		Purpose: Private. Scan a directory for potenitally deleteable items, and
//				 add them to the list. Returns true if the scan should continue.
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
bool HousekeepStoreAccount::ScanDirectory(int64_t ObjectID)
{
#ifndef WIN32
	if((--mCountUntilNextInterprocessMsgCheck) <= 0)
	{
		mCountUntilNextInterprocessMsgCheck = POLL_INTERPROCESS_MSG_CHECK_FREQUENCY;
		// Check for having to stop
		if(mrDaemon.CheckForInterProcessMsg(mAccountID))	// include account ID here as the specified account is locked
		{
			// Need to abort now
			return false;
		}
	}
#endif

	// Get the filename
	std::string objectFilename;
	MakeObjectFilename(ObjectID, objectFilename);

	// Open it.
	std::auto_ptr<RaidFileRead> dirStream(RaidFileRead::Open(mStoreDiscSet, objectFilename));
	
	// Add the size of the directory on disc to the size being calculated
	int64_t originalDirSizeInBlocks = dirStream->GetDiscUsageInBlocks();
	mBlocksInDirectories += originalDirSizeInBlocks;
	mBlocksUsed += originalDirSizeInBlocks;
	
	// Read the directory in
	BackupStoreDirectory dir;
	BufferedStream buf(*dirStream);
	dir.ReadFromStream(buf, IOStream::TimeOutInfinite);
	dirStream->Close();
	
	// Is it empty?
	if(dir.GetNumberOfEntries() == 0)
	{
		// Add it to the list of directories to potentially delete
		mEmptyDirectories.push_back(dir.GetObjectID());
	}
	
	// BLOCK
	{
		// Remove any files which are marked for removal as soon as they become old
		// or deleted.
		bool deletedSomething = false;
		do
		{
			// Iterate through the directory
			deletedSomething = false;
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;
			while((en = i.Next(BackupStoreDirectory::Entry::Flags_File)) != 0)
			{
				int16_t enFlags = en->GetFlags();
				if((enFlags & BackupStoreDirectory::Entry::Flags_RemoveASAP) != 0
					&& (enFlags & (BackupStoreDirectory::Entry::Flags_Deleted | BackupStoreDirectory::Entry::Flags_OldVersion)) != 0)
				{
					// Delete this immediately.
					DeleteFile(ObjectID, en->GetObjectID(), dir, objectFilename, originalDirSizeInBlocks);
					
					// flag as having done something
					deletedSomething = true;
					
					// Must start the loop from the beginning again, as iterator is now
					// probably invalid.
					break;
				}
			}
		} while(deletedSomething);
	}
	
	// BLOCK
	{
		// Add files to the list of potential deletions

		// map to count the distance from the mark
		std::map<std::pair<BackupStoreFilename, int32_t>, int32_t> markVersionAges;
			// map of pair (filename, mark number) -> version age

		// NOTE: use a reverse iterator to allow the distance from mark stuff to work
		BackupStoreDirectory::ReverseIterator i(dir);
		BackupStoreDirectory::Entry *en = 0;

		while((en = i.Next(BackupStoreDirectory::Entry::Flags_File)) != 0)
		{
			// Update recalculated usage sizes
			int16_t enFlags = en->GetFlags();
			int64_t enSizeInBlocks = en->GetSizeInBlocks();
			mBlocksUsed += enSizeInBlocks;
			if(enFlags & BackupStoreDirectory::Entry::Flags_OldVersion) mBlocksInOldFiles += enSizeInBlocks;
			if(enFlags & BackupStoreDirectory::Entry::Flags_Deleted) mBlocksInDeletedFiles += enSizeInBlocks;
					
			// Work out ages of this version from the last mark
			int32_t enVersionAge = 0;
			std::map<std::pair<BackupStoreFilename, int32_t>, int32_t>::iterator enVersionAgeI(markVersionAges.find(std::pair<BackupStoreFilename, int32_t>(en->GetName(), en->GetMarkNumber())));
			if(enVersionAgeI != markVersionAges.end())
			{
				enVersionAge = enVersionAgeI->second + 1;
				enVersionAgeI->second = enVersionAge;
			}
			else
			{
				markVersionAges[std::pair<BackupStoreFilename, int32_t>(en->GetName(), en->GetMarkNumber())] = enVersionAge;
			}
			// enVersionAge is now the age of this version.
			
			// Potentially add it to the list if it's deleted, if it's an old version or deleted
			if((enFlags & (BackupStoreDirectory::Entry::Flags_Deleted | BackupStoreDirectory::Entry::Flags_OldVersion)) != 0)
			{
				// Is deleted / old version.
				DelEn d;
				d.mObjectID = en->GetObjectID();
				d.mInDirectory = ObjectID;
				d.mSizeInBlocks = en->GetSizeInBlocks();
				d.mMarkNumber = en->GetMarkNumber();
				d.mVersionAgeWithinMark = enVersionAge;
				
				// Add it to the list
				mPotentialDeletions.insert(d);
				
				// Update various counts
				mPotentialDeletionsTotalSize += d.mSizeInBlocks;
				if(d.mSizeInBlocks > mMaxSizeInPotentialDeletions) mMaxSizeInPotentialDeletions = d.mSizeInBlocks;
				
				// Too much in the list of potential deletions?
				// (check against the deletion target + the max size in deletions, so that we never delete things
				// and take the total size below the deletion size target)
				if(mPotentialDeletionsTotalSize > (mDeletionSizeTarget + mMaxSizeInPotentialDeletions))
				{
					int64_t sizeToRemove = mPotentialDeletionsTotalSize - (mDeletionSizeTarget + mMaxSizeInPotentialDeletions);
					bool recalcMaxSize = false;
					
					while(sizeToRemove > 0)
					{
						// Make iterator for the last element, while checking that there's something there in the first place.
						std::set<DelEn, DelEnCompare>::iterator i(mPotentialDeletions.end());
						if(i != mPotentialDeletions.begin())
						{
							// Nothing left in set
							break;
						}
						// Make this into an iterator pointing to the last element in the set
						--i;
						
						// Delete this one?
						if(sizeToRemove > i->mSizeInBlocks)
						{
							sizeToRemove -= i->mSizeInBlocks;
							if(i->mSizeInBlocks >= mMaxSizeInPotentialDeletions)
							{
								// Will need to recalculate the maximum size now, because we've just deleted that element
								recalcMaxSize = true;
							}
							mPotentialDeletions.erase(i);
						}
						else
						{
							// Over the size to remove, so stop now
							break;
						}
					}
					
					if(recalcMaxSize)
					{
						// Because an object which was the maximum size recorded was deleted from the set
						// it's necessary to recalculate this maximum.
						mMaxSizeInPotentialDeletions = 0;
						std::set<DelEn, DelEnCompare>::const_iterator i(mPotentialDeletions.begin());
						for(; i != mPotentialDeletions.end(); ++i)
						{
							if(i->mSizeInBlocks > mMaxSizeInPotentialDeletions)
							{
								mMaxSizeInPotentialDeletions = i->mSizeInBlocks;
							}
						}
					}
				}
			}
		}
	}

	{
		// Recurse into subdirectories
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next(BackupStoreDirectory::Entry::Flags_Dir)) != 0)
		{
			// Next level
			ASSERT((en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir) == BackupStoreDirectory::Entry::Flags_Dir);
			
			if(!ScanDirectory(en->GetObjectID()))
			{
				// Halting operation
				return false;
			}
		}
	}
	
	return true;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::DelEnCompare::operator()(const HousekeepStoreAccount::DelEn &, const HousekeepStoreAccount::DelEnd &)
//		Purpose: Comparison function for set
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
bool HousekeepStoreAccount::DelEnCompare::operator()(const HousekeepStoreAccount::DelEn &x, const HousekeepStoreAccount::DelEn &y)
{
	// STL spec says this:
	// A Strict Weak Ordering is a Binary Predicate that compares two objects, returning true if the first precedes the second. 

	// The sort order here is intended to preserve the entries of most value, that is, the newest objects
	// which are on a mark boundary.
	
	// Reverse order age, so oldest goes first
	if(x.mVersionAgeWithinMark > y.mVersionAgeWithinMark)
	{
		return true;
	}
	else if(x.mVersionAgeWithinMark < y.mVersionAgeWithinMark)
	{
		return false;
	}

	// but mark number in ascending order, so that the oldest marks are deleted first
	if(x.mMarkNumber < y.mMarkNumber)
	{
		return true;
	}
	else if(x.mMarkNumber > y.mMarkNumber)
	{
		return false;
	}

	// Just compare object ID now to put the oldest objects first
	return x.mObjectID < y.mObjectID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::DeleteFiles()
//		Purpose: Delete the files targetted for deletion, returning true if the operation was interrupted
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
bool HousekeepStoreAccount::DeleteFiles()
{
	// Only delete files if the deletion target is greater than zero
	// (otherwise we delete one file each time round, which gradually deletes the old versions)
	if(mDeletionSizeTarget <= 0)
	{
		// Not interrupted
		return false;
	}

	// Iterate through the set of potential deletions, until enough has been deleted.
	// (there is likely to be more in the set than should be actually deleted).
	for(std::set<DelEn, DelEnCompare>::iterator i(mPotentialDeletions.begin()); i != mPotentialDeletions.end(); ++i)
	{
#ifndef WIN32
		if((--mCountUntilNextInterprocessMsgCheck) <= 0)
		{
			mCountUntilNextInterprocessMsgCheck = POLL_INTERPROCESS_MSG_CHECK_FREQUENCY;
			// Check for having to stop
			if(mrDaemon.CheckForInterProcessMsg(mAccountID))	// include account ID here as the specified account is now locked
			{
				// Need to abort now
				return true;
			}
		}
#endif

		// Load up the directory it's in
		// Get the filename
		std::string dirFilename;
		BackupStoreDirectory dir;
		int64_t dirSizeInBlocksOrig = 0;
		{
			MakeObjectFilename(i->mInDirectory, dirFilename);
			std::auto_ptr<RaidFileRead> dirStream(RaidFileRead::Open(mStoreDiscSet, dirFilename));
			dirSizeInBlocksOrig = dirStream->GetDiscUsageInBlocks();
			dir.ReadFromStream(*dirStream, IOStream::TimeOutInfinite);
		}
		
		// Delete the file
		DeleteFile(i->mInDirectory, i->mObjectID, dir, dirFilename, dirSizeInBlocksOrig);
		
		// Stop if the deletion target has been matched or exceeded
		// (checking here rather than at the beginning will tend to reduce the
		// space to slightly less than the soft limit, which will allow the backup
		// client to start uploading files again)
		if((0 - mBlocksUsedDelta) >= mDeletionSizeTarget)
		{
			break;
		}
	}

	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::DeleteFile(int64_t, int64_t, BackupStoreDirectory &, const std::string &, int64_t)
//		Purpose: Delete a file. Takes the directory already loaded in and the filename,
//				 for efficiency in both the usage senarios.
//		Created: 15/7/04
//
// --------------------------------------------------------------------------
void HousekeepStoreAccount::DeleteFile(int64_t InDirectory, int64_t ObjectID, BackupStoreDirectory &rDirectory, const std::string &rDirectoryFilename, int64_t OriginalDirSizeInBlocks)
{
	// Find the entry inside the directory
	bool wasDeleted = false;
	bool wasOldVersion = false;
	int64_t deletedFileSizeInBlocks = 0;
	// A pointer to an object which requires commiting if the directory save goes OK
	std::auto_ptr<RaidFileWrite> padjustedEntry;
	// BLOCK
	{
		BackupStoreDirectory::Entry *pentry = rDirectory.FindEntryByID(ObjectID);
		if(pentry == 0)
		{
			::syslog(LOG_ERR, "acc 0x%08x, object %lld not found in dir %lld, logic error/corruption? Run bbstoreaccounts check <accid> fix", mAccountID, ObjectID, InDirectory);
			return;
		}
		
		// Record the flags it's got set
		wasDeleted = ((pentry->GetFlags() & BackupStoreDirectory::Entry::Flags_Deleted) != 0);
		wasOldVersion = ((pentry->GetFlags() & BackupStoreDirectory::Entry::Flags_OldVersion) != 0);
		// Check this should be deleted
		if(!wasDeleted && !wasOldVersion)
		{
			// Things changed size we were last around
			return;
		}
		
		// Record size
		deletedFileSizeInBlocks = pentry->GetSizeInBlocks();
		
		// If the entry is involved in a chain of patches, it needs to be handled
		// a bit more carefully.
		if(pentry->GetDependsNewer() != 0 && pentry->GetDependsOlder() == 0)
		{
			// This entry is a patch from a newer entry. Just need to update the info on that entry.
			BackupStoreDirectory::Entry *pnewer = rDirectory.FindEntryByID(pentry->GetDependsNewer());
			if(pnewer == 0 || pnewer->GetDependsOlder() != ObjectID)
			{
				THROW_EXCEPTION(BackupStoreException, PatchChainInfoBadInDirectory);
			}
			// Change the info in the newer entry so that this no longer points to this entry
			pnewer->SetDependsOlder(0);
		}
		else if(pentry->GetDependsOlder() != 0)
		{
			BackupStoreDirectory::Entry *polder = rDirectory.FindEntryByID(pentry->GetDependsOlder());
			if(pentry->GetDependsNewer() == 0)
			{
				// There exists an older version which depends on this one. Need to combine the two over that one.

				// Adjust the other entry in the directory
				if(polder == 0 || polder->GetDependsNewer() != ObjectID)
				{
					THROW_EXCEPTION(BackupStoreException, PatchChainInfoBadInDirectory);
				}
				// Change the info in the older entry so that this no longer points to this entry
				polder->SetDependsNewer(0);
			}
			else
			{
				// This entry is in the middle of a chain, and two patches need combining.
				
				// First, adjust the directory entries
				BackupStoreDirectory::Entry *pnewer = rDirectory.FindEntryByID(pentry->GetDependsNewer());
				if(pnewer == 0 || pnewer->GetDependsOlder() != ObjectID
					|| polder == 0 || polder->GetDependsNewer() != ObjectID)
				{
					THROW_EXCEPTION(BackupStoreException, PatchChainInfoBadInDirectory);
				}
				// Remove the middle entry from the linked list by simply using the values from this entry
				pnewer->SetDependsOlder(pentry->GetDependsOlder());
				polder->SetDependsNewer(pentry->GetDependsNewer());
			}
			
			// COMMON CODE to both cases

			// Generate the filename of the older version
			std::string objFilenameOlder;
			MakeObjectFilename(pentry->GetDependsOlder(), objFilenameOlder);
			// Open it twice (it's the diff)
			std::auto_ptr<RaidFileRead> pdiff(RaidFileRead::Open(mStoreDiscSet, objFilenameOlder));
			std::auto_ptr<RaidFileRead> pdiff2(RaidFileRead::Open(mStoreDiscSet, objFilenameOlder));
			// Open this file
			std::string objFilename;
			MakeObjectFilename(ObjectID, objFilename);
			std::auto_ptr<RaidFileRead> pobjectBeingDeleted(RaidFileRead::Open(mStoreDiscSet, objFilename));
			// And open a write file to overwrite the other directory entry
			padjustedEntry.reset(new RaidFileWrite(mStoreDiscSet, objFilenameOlder));
			padjustedEntry->Open(true /* allow overwriting */);

			if(pentry->GetDependsNewer() == 0)
			{
				// There exists an older version which depends on this one. Need to combine the two over that one.
				BackupStoreFile::CombineFile(*pdiff, *pdiff2, *pobjectBeingDeleted, *padjustedEntry);
			}
			else
			{
				// This entry is in the middle of a chain, and two patches need combining.
				BackupStoreFile::CombineDiffs(*pobjectBeingDeleted, *pdiff, *pdiff2, *padjustedEntry);
			}
			// The file will be committed later when the directory is safely commited.
			
			// Work out the adjusted size
			int64_t newSize = padjustedEntry->GetDiscUsageInBlocks();
			int64_t sizeDelta = newSize - polder->GetSizeInBlocks();
			mBlocksUsedDelta += sizeDelta;
			if((polder->GetFlags() & BackupStoreDirectory::Entry::Flags_Deleted) != 0)
			{
				mBlocksInDeletedFilesDelta += sizeDelta;
			}
			if((polder->GetFlags() & BackupStoreDirectory::Entry::Flags_OldVersion) != 0)
			{
				mBlocksInOldFilesDelta += sizeDelta;
			}
			polder->SetSizeInBlocks(newSize);
		}
		
		// pentry no longer valid
	}
	
	// Delete it from the directory
	rDirectory.DeleteEntry(ObjectID);
	
	// Save directory back to disc
	// BLOCK
	int64_t dirRevisedSize = 0;
	{
		RaidFileWrite writeDir(mStoreDiscSet, rDirectoryFilename);
		writeDir.Open(true /* allow overwriting */);
		rDirectory.WriteToStream(writeDir);

		// get the disc usage (must do this before commiting it)
		dirRevisedSize = writeDir.GetDiscUsageInBlocks();

		// Commit directory
		writeDir.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);

		// adjust usage counts for this directory
		if(dirRevisedSize > 0)
		{
			int64_t adjust = dirRevisedSize - OriginalDirSizeInBlocks;
			mBlocksUsedDelta += adjust;
			mBlocksInDirectoriesDelta += adjust;
		}
	}

	// Commit any new adjusted entry
	if(padjustedEntry.get() != 0)
	{
		padjustedEntry->Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
		padjustedEntry.reset();	// delete it now
	}

	// Delete from disc
	{
		std::string objFilename;
		MakeObjectFilename(ObjectID, objFilename);
		RaidFileWrite del(mStoreDiscSet, objFilename);
		del.Delete();
	}

	// Adjust counts for the file
	++mFilesDeleted;
	mBlocksUsedDelta -= deletedFileSizeInBlocks;
	if(wasDeleted) mBlocksInDeletedFilesDelta -= deletedFileSizeInBlocks;
	if(wasOldVersion) mBlocksInOldFilesDelta -= deletedFileSizeInBlocks;
	
	// Delete the directory?
	// Do this if... dir has zero entries, and is marked as deleted in it's containing directory
	if(rDirectory.GetNumberOfEntries() == 0)
	{
		// Candidate for deletion
		mEmptyDirectories.push_back(InDirectory);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::DeleteEmptyDirectories()
//		Purpose: Remove any empty directories which are also marked as deleted in their containing directory,
//				 returning true if the opertaion was interrupted
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
bool HousekeepStoreAccount::DeleteEmptyDirectories()
{
	while(mEmptyDirectories.size() > 0)
	{
		std::vector<int64_t> toExamine;

		// Go through list
		for(std::vector<int64_t>::const_iterator i(mEmptyDirectories.begin()); i != mEmptyDirectories.end(); ++i)
		{
#ifndef WIN32
			if((--mCountUntilNextInterprocessMsgCheck) <= 0)
			{
				mCountUntilNextInterprocessMsgCheck = POLL_INTERPROCESS_MSG_CHECK_FREQUENCY;
				// Check for having to stop
				if(mrDaemon.CheckForInterProcessMsg(mAccountID))	// include account ID here as the specified account is now locked
				{
					// Need to abort now
					return true;
				}
			}
#endif

			// Do not delete the root directory
			if(*i == BACKUPSTORE_ROOT_DIRECTORY_ID)
			{
				continue;
			}

			// Load up the directory to potentially delete
			std::string dirFilename;
			BackupStoreDirectory dir;
			int64_t dirSizeInBlocks = 0;
			{
				MakeObjectFilename(*i, dirFilename);
				// Check it actually exists (just in case it gets added twice to the list)
				if(!RaidFileRead::FileExists(mStoreDiscSet, dirFilename))
				{
					// doesn't exist, next!
					continue;
				}
				// load
				std::auto_ptr<RaidFileRead> dirStream(RaidFileRead::Open(mStoreDiscSet, dirFilename));
				dirSizeInBlocks = dirStream->GetDiscUsageInBlocks();
				dir.ReadFromStream(*dirStream, IOStream::TimeOutInfinite);
			}

			// Make sure this directory is actually empty
			if(dir.GetNumberOfEntries() != 0)
			{
				// Not actually empty, try next one
				continue;
			}

			// Candiate for deletion... open containing directory
			std::string containingDirFilename;
			BackupStoreDirectory containingDir;
			int64_t containingDirSizeInBlocksOrig = 0;
			{
				MakeObjectFilename(dir.GetContainerID(), containingDirFilename);
				std::auto_ptr<RaidFileRead> containingDirStream(RaidFileRead::Open(mStoreDiscSet, containingDirFilename));
				containingDirSizeInBlocksOrig = containingDirStream->GetDiscUsageInBlocks();
				containingDir.ReadFromStream(*containingDirStream, IOStream::TimeOutInfinite);
			}

			// Find the entry
			BackupStoreDirectory::Entry *pdirentry = containingDir.FindEntryByID(dir.GetObjectID());
			if((pdirentry != 0) && ((pdirentry->GetFlags() & BackupStoreDirectory::Entry::Flags_Deleted) != 0))
			{
				// Should be deleted
				containingDir.DeleteEntry(dir.GetObjectID());

				// Is the containing dir now a candidate for deletion?
				if(containingDir.GetNumberOfEntries() == 0)
				{
					toExamine.push_back(containingDir.GetObjectID());
				}

				// Write revised parent directory
				RaidFileWrite writeDir(mStoreDiscSet, containingDirFilename);
				writeDir.Open(true /* allow overwriting */);
				containingDir.WriteToStream(writeDir);

				// get the disc usage (must do this before commiting it)
				int64_t dirSize = writeDir.GetDiscUsageInBlocks();

				// Commit directory
				writeDir.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);

				// adjust usage counts for this directory
				if(dirSize > 0)
				{
					int64_t adjust = dirSize - containingDirSizeInBlocksOrig;
					mBlocksUsedDelta += adjust;
					mBlocksInDirectoriesDelta += adjust;
				}

				// Delete the directory itself
				{
					RaidFileWrite del(mStoreDiscSet, dirFilename);
					del.Delete();
				}

				// And adjust usage counts for the directory that's just been deleted
				mBlocksUsedDelta -= dirSizeInBlocks;
				mBlocksInDirectoriesDelta -= dirSizeInBlocks;

				// Update count
				++mEmptyDirectoriesDeleted;
			}
		}

		// Remove contents of empty directories
		mEmptyDirectories.clear();
		// Swap in new, so it's examined next time round
		mEmptyDirectories.swap(toExamine);
	}
	
	// Not interrupted
	return false;
}




