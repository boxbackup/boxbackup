// --------------------------------------------------------------------------
//
// File
//		Name:    HousekeepStoreAccount.cpp
//		Purpose: Run housekeeping on a server-side account. Removes
//			 files and directories which are marked as RemoveASAP,
//			 and Old and Deleted objects as necessary to bring the
//			 account back under its soft limit.
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include <map>

#include "autogen_BackupStoreException.h"
#include "BackupConstants.h"
#include "BackupFileSystem.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "BufferedStream.h"
#include "HousekeepStoreAccount.h"
#include "NamedLock.h"

#include "MemLeakFindOn.h"

// check every 32 directories scanned/files deleted
#define POLL_INTERPROCESS_MSG_CHECK_FREQUENCY	32

// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::HousekeepStoreAccount(
//			 BackupFileSystem&, HousekeepingCallback*)
//		Purpose: Constructor
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
HousekeepStoreAccount::HousekeepStoreAccount(BackupFileSystem& FileSystem,
	HousekeepingCallback* pHousekeepingCallback)
	: mrFileSystem(FileSystem),
	  mpHousekeepingCallback(pHousekeepingCallback),
	  mDeletionSizeTarget(0),
	  mPotentialDeletionsTotalSize(0),
	  mMaxSizeInPotentialDeletions(0),
	  mErrorCount(0),
	  mBlocksUsed(0),
	  mBlocksInOldFiles(0),
	  mBlocksInDeletedFiles(0),
	  mBlocksInDirectories(0),
	  mBlocksUsedDelta(0),
	  mBlocksInCurrentFilesDelta(0),
	  mBlocksInOldFilesDelta(0),
	  mBlocksInDeletedFilesDelta(0),
	  mBlocksInDirectoriesDelta(0),
	  mFilesDeleted(0),
	  mEmptyDirectoriesDeleted(0),
	  mpNewRefs(NULL),
	  mCountUntilNextInterprocessMsgCheck(POLL_INTERPROCESS_MSG_CHECK_FREQUENCY)
{
	std::ostringstream tag;
	tag << "hk=" << FileSystem.GetAccountIdentifier();
	mTagWithClientID.Change(tag.str());
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
	if(mpNewRefs)
	{
		// Discard() can throw exception, but destructors aren't supposed to do that, so
		// just catch and log them.
		try
		{
			mpNewRefs->Discard();
		}
		catch(BoxException &e)
		{
			BOX_ERROR("Failed to destroy housekeeper: discarding the refcount "
				"database threw an exception: " << e.what());
		}

		mpNewRefs = NULL;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::DoHousekeeping()
//		Purpose: Perform the housekeeping
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
bool HousekeepStoreAccount::DoHousekeeping(bool KeepTryingForever)
{
	BOX_TRACE("Starting housekeeping on account " <<
		mrFileSystem.GetAccountIdentifier());

	// Attempt to lock the account. If KeepTryingForever is false, then only
	// try once, and return false if that fails.
	try
	{
		mrFileSystem.GetLock(KeepTryingForever ? BackupFileSystem::KEEP_TRYING_FOREVER : 1);
	}
	catch(BackupStoreException &e)
	{
		if(EXCEPTION_IS_TYPE(e, BackupStoreException, CouldNotLockStoreAccount))
		{
			// Couldn't lock the account -- just stop now
			return false;
		}
		else
		{
			// something unexpected went wrong
			throw;
		}
	}

	// Load the store info to find necessary info for the housekeeping
	BackupStoreInfo* pInfo = &(mrFileSystem.GetBackupStoreInfo(false)); // !ReadOnly
	std::auto_ptr<BackupStoreInfo> apOldInfo = mrFileSystem.GetBackupStoreInfoUncached();

	// Calculate how much should be deleted
	mDeletionSizeTarget = pInfo->GetBlocksUsed() - pInfo->GetBlocksSoftLimit();
	if(mDeletionSizeTarget < 0)
	{
		mDeletionSizeTarget = 0;
	}

	mpNewRefs = &mrFileSystem.GetPotentialRefCountDatabase();

	// Scan the directory for potential things to delete
	// This will also find and enqueue eligible items marked with RemoveASAP
	bool continueHousekeeping = ScanDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, *pInfo);

	if(!continueHousekeeping)
	{
		// The scan was incomplete, so the new block counts are
		// incorrect, we can't rely on them, so discard them.
		mrFileSystem.DiscardBackupStoreInfo(*pInfo);
		pInfo = &(mrFileSystem.GetBackupStoreInfo(false)); // !ReadOnly

		// We're about to reset counters and exit, so report what
		// happened now.
		BOX_INFO("Housekeeping on account " <<
			mrFileSystem.GetAccountIdentifier() << " removed " <<
			(0 - mBlocksUsedDelta) << " blocks (" << mFilesDeleted <<
			" files, " << mEmptyDirectoriesDeleted << " dirs) and the "
			"directory scan was interrupted");
	}

	if(!continueHousekeeping)
	{
		// Report any UNexpected changes, and consider them to be errors.
		// Do this before applying the expected changes below.
		mErrorCount += pInfo->ReportChangesTo(*apOldInfo);
	}

	// If scan directory stopped for some reason, probably parent
	// instructed to terminate, stop now.
	//
	// We can only update the refcount database if we successfully
	// finished our scan of all directories, otherwise we don't actually
	// know which of the new counts are valid and which aren't
	// (we might not have seen second references to some objects, etc.).

	if(!continueHousekeeping)
	{
		mpNewRefs->Discard();
		mpNewRefs = NULL;
		mrFileSystem.PutBackupStoreInfo(*pInfo);
		return false;
	}

	// Try to load the old reference count database and check whether
	// any counts have changed. We want to compare the mpNewRefs to
	// apOldRefs before we delete any files, because that will also change
	// the reference count in a way that's not an error.

	try
	{
		BackupStoreRefCountDatabase& old_refs(
			mrFileSystem.GetPermanentRefCountDatabase(true)); // ReadOnly
		mErrorCount += mpNewRefs->ReportChangesTo(old_refs);
	}
	catch(BoxException &e)
	{
		BOX_WARNING("Reference count database was missing or "
			"corrupted during housekeeping, cannot check it for "
			"errors.");
		mErrorCount++;
	}

	// Go and delete items from the accounts
	bool deleteInterrupted = DeleteFiles(*pInfo);

	// If that wasn't interrupted, remove any empty directories which
	// are also marked as deleted in their containing directory
	if(!deleteInterrupted)
	{
		deleteInterrupted = DeleteEmptyDirectories(*pInfo);
	}

	// Log deletion if anything was deleted
	if(mFilesDeleted > 0 || mEmptyDirectoriesDeleted > 0)
	{
		BOX_INFO("Housekeeping on account " << mrFileSystem.GetAccountIdentifier() << " "
			"removed " << -mBlocksUsedDelta << " blocks (" << mFilesDeleted << " "
			"files, " << mEmptyDirectoriesDeleted << " dirs)" <<
			(deleteInterrupted?" and was interrupted":""));
	}

	// Make sure the delta's won't cause problems if the counts are
	// really wrong, and it wasn't fixed because the store was
	// updated during the scan.
	if(mBlocksUsedDelta < (0 - pInfo->GetBlocksUsed()))
	{
		mBlocksUsedDelta = (0 - pInfo->GetBlocksUsed());
	}
	if(mBlocksInCurrentFilesDelta < (0 - pInfo->GetBlocksInCurrentFiles()))
	{
		mBlocksInCurrentFilesDelta = (0 - pInfo->GetBlocksInCurrentFiles());
	}
	if(mBlocksInOldFilesDelta < (0 - pInfo->GetBlocksInOldFiles()))
	{
		mBlocksInOldFilesDelta = (0 - pInfo->GetBlocksInOldFiles());
	}
	if(mBlocksInDeletedFilesDelta < (0 - pInfo->GetBlocksInDeletedFiles()))
	{
		mBlocksInDeletedFilesDelta = (0 - pInfo->GetBlocksInDeletedFiles());
	}
	if(mBlocksInDirectoriesDelta < (0 - pInfo->GetBlocksInDirectories()))
	{
		mBlocksInDirectoriesDelta = (0 - pInfo->GetBlocksInDirectories());
	}

	// Update the usage counts in the store
	pInfo->ChangeBlocksUsed(mBlocksUsedDelta);
	pInfo->ChangeBlocksInCurrentFiles(mBlocksInCurrentFilesDelta);
	pInfo->ChangeBlocksInOldFiles(mBlocksInOldFilesDelta);
	pInfo->ChangeBlocksInDeletedFiles(mBlocksInDeletedFilesDelta);
	pInfo->ChangeBlocksInDirectories(mBlocksInDirectoriesDelta);

	// Save the store info back
	mrFileSystem.PutBackupStoreInfo(*pInfo);

	// force file to be saved and closed before releasing the lock below
	mpNewRefs->Commit();
	mpNewRefs = NULL;

	// Explicitly release the lock (would happen automatically on going out of scope,
	// included for code clarity)
	mrFileSystem.ReleaseLock();

	BOX_TRACE("Finished housekeeping on account " <<
		mrFileSystem.GetAccountIdentifier());
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::ScanDirectory(int64_t)
//		Purpose: Private. Scan a directory for potentially deleteable
//			 items, and add them to the list. Returns true if the
//			 scan should continue.
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
bool HousekeepStoreAccount::ScanDirectory(int64_t ObjectID,
	BackupStoreInfo& rBackupStoreInfo)
{
#ifndef WIN32
	if((--mCountUntilNextInterprocessMsgCheck) <= 0)
	{
		mCountUntilNextInterprocessMsgCheck =
			POLL_INTERPROCESS_MSG_CHECK_FREQUENCY;

		// Check for having to stop
		// Include account ID here as the specified account is locked
		int account_id = mrFileSystem.GetAccountID();
		if(mpHousekeepingCallback &&
			mpHousekeepingCallback->CheckForInterProcessMsg(account_id))
		{
			// Need to abort now
			return false;
		}
	}
#endif

	// Read the directory in
	BackupStoreDirectory dir;
	mrFileSystem.GetDirectory(ObjectID, dir);

	// Add the size of the directory on disc to the size being calculated
	int64_t originalDirSizeInBlocks = dir.GetUserInfo1_SizeInBlocks();
	ASSERT(originalDirSizeInBlocks > 0);
	mBlocksInDirectories += originalDirSizeInBlocks;
	mBlocksUsed += originalDirSizeInBlocks;

	// Is it empty?
	if(dir.GetNumberOfEntries() == 0)
	{
		// Add it to the list of directories to potentially delete
		mEmptyDirectories.push_back(dir.GetObjectID());
	}

	// Calculate reference counts first, before we start requesting
	// files to be deleted.
	// BLOCK
	{
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;

		while((en = i.Next()) != 0)
		{
			// This directory references this object
			mpNewRefs->AddReference(en->GetObjectID());
		}
	}

	// BLOCK
	{
		// Add to mDefiniteDeletions any files which are marked for removal as soon as
		// they become old or deleted.

		// Iterate through the directory
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next(BackupStoreDirectory::Entry::Flags_File)) != 0)
		{
			int16_t enFlags = en->GetFlags();
			if((enFlags & BackupStoreDirectory::Entry::Flags_RemoveASAP) != 0
				&& (en->IsDeleted() || en->IsOld()))
			{
				if(!mrFileSystem.CanMergePatchesEasily() &&
					en->GetRequiredByObject() != 0)
				{
					BOX_ERROR("Cannot delete file " <<
						BOX_FORMAT_OBJECTID(en->GetObjectID()) <<
						" flagged as RemoveASAP because "
						"another file depends on it (" <<
						BOX_FORMAT_OBJECTID(en->GetDependsOnObject()) <<
						" and the filesystem does not "
						"support merging patches easily");
					continue;
				}

				mDefiniteDeletions.push_back(
					std::pair<int64_t, int64_t>(en->GetObjectID(),
						ObjectID)); // of the directory

				// Because we are definitely deleting this file, we don't need
				// housekeeping to delete potential files to free up the space
				// that it occupies, so reduce the deletion target by this file's
				// size.
				if(mDeletionSizeTarget > 0)
				{
					mDeletionSizeTarget -= en->GetSizeInBlocks();
					if(mDeletionSizeTarget < 0)
					{
						mDeletionSizeTarget = 0;
					}
				}
			}
		}
	}

	// BLOCK
	{
		// Add files to the list of potential deletions

		// map to count the distance from the mark
		typedef std::pair<std::string, int32_t> version_t;
		std::map<version_t, int32_t> markVersionAges;
			// map of pair (filename, mark number) -> version age

		// NOTE: use a reverse iterator to allow the distance from mark stuff to work
		BackupStoreDirectory::ReverseIterator i(dir);
		BackupStoreDirectory::Entry *en = 0;

		while((en = i.Next(BackupStoreDirectory::Entry::Flags_File)) != 0)
		{
			// Update recalculated usage sizes
			int64_t enSizeInBlocks = en->GetSizeInBlocks();
			mBlocksUsed += enSizeInBlocks;
			if(en->IsOld()) mBlocksInOldFiles += enSizeInBlocks;
			if(en->IsDeleted()) mBlocksInDeletedFiles += enSizeInBlocks;

			// Work out ages of this version from the last mark
			int32_t enVersionAge = 0;
			std::map<version_t, int32_t>::iterator enVersionAgeI(
				markVersionAges.find(
					version_t(en->GetName().GetEncodedFilename(),
						en->GetMarkNumber())));
			if(enVersionAgeI != markVersionAges.end())
			{
				enVersionAge = enVersionAgeI->second + 1;
				enVersionAgeI->second = enVersionAge;
			}
			else
			{
				markVersionAges[version_t(en->GetName().GetEncodedFilename(), en->GetMarkNumber())] = enVersionAge;
			}
			// enVersionAge is now the age of this version.

			// Add it to the list of potential files to remove, if it's an old version
			// or deleted:
			if(en->IsOld() || en->IsDeleted())
			{
				if(!mrFileSystem.CanMergePatchesEasily() &&
					en->GetRequiredByObject() != 0)
				{
					BOX_TRACE("Cannot remove old/deleted file " <<
						BOX_FORMAT_OBJECTID(en->GetObjectID()) <<
						" now, because another file depends on it (" <<
						BOX_FORMAT_OBJECTID(en->GetDependsOnObject()) <<
						" and the filesystem does not support merging "
						"patches easily");
					continue;
				}

				// Is deleted / old version.
				DelEn d;
				d.mObjectID = en->GetObjectID();
				d.mInDirectory = ObjectID;
				d.mSizeInBlocks = en->GetSizeInBlocks();
				d.mMarkNumber = en->GetMarkNumber();
				d.mVersionAgeWithinMark = enVersionAge;
				d.mIsFlagDeleted = en->IsDeleted();

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

	// Recurse into subdirectories
	{
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next(BackupStoreDirectory::Entry::Flags_Dir)) != 0)
		{
			ASSERT(en->IsDir());

			if(!ScanDirectory(en->GetObjectID(), rBackupStoreInfo))
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
//		Purpose: Delete the files targeted for deletion, returning
//			 true if the operation was interrupted
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
bool HousekeepStoreAccount::DeleteFiles(BackupStoreInfo& rBackupStoreInfo)
{
	// Delete all the definite deletions first, because we promised that we would, and because
	// the deletion target might only be zero because we are definitely deleting enough files
	// to free up all required space. So if we didn't delete them, the store would remain over
	// its target size.
	for(std::vector<std::pair<int64_t, int64_t> >::iterator i = mDefiniteDeletions.begin();
		i != mDefiniteDeletions.end(); i++)
	{
		int64_t FileID = i->first;
		int64_t DirID = i->second;
		RemoveReferenceAndMaybeDeleteFile(FileID, DirID, "RemoveASAP", rBackupStoreInfo);
	}

	// Only delete potentially deletable files if the deletion target is greater than zero
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
			int account_id = mrFileSystem.GetAccountID();
			// Check for having to stop
			if(mpHousekeepingCallback &&
				// include account ID here as the specified account is now locked
				mpHousekeepingCallback->CheckForInterProcessMsg(account_id))
			{
				// Need to abort now. Return true to signal that we were interrupted.
				return true;
			}
		}
#endif

		RemoveReferenceAndMaybeDeleteFile(i->mObjectID, i->mInDirectory,
			(i->mIsFlagDeleted ? "deleted" : "old"), rBackupStoreInfo);

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

void HousekeepStoreAccount::RemoveReferenceAndMaybeDeleteFile(int64_t FileID, int64_t DirID,
	const std::string& reason, BackupStoreInfo& rBackupStoreInfo)
{
	// Load up the directory it's in
	// Get the filename
	BackupStoreDirectory dir;
	mrFileSystem.GetDirectory(DirID, dir);

	// Delete the file
	BackupStoreRefCountDatabase::refcount_t refs =
		DeleteFile(DirID, FileID, dir, rBackupStoreInfo);
	if(refs == 0)
	{
		BOX_INFO("Housekeeping removed " << reason << " file " <<
			BOX_FORMAT_OBJECTID(FileID) << " from dir " << BOX_FORMAT_OBJECTID(DirID));
	}
	else
	{
		BOX_TRACE("Housekeeping preserved " << reason << " file " <<
			BOX_FORMAT_OBJECTID(FileID) << " in dir " << BOX_FORMAT_OBJECTID(DirID) <<
			" with " << refs << " references");
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::DeleteFile(int64_t, int64_t,
//			 BackupStoreDirectory &, const std::string &, int64_t)
//		Purpose: Delete a file. Takes the directory already loaded
//			 in and the filename, for efficiency in both the
//			 usage scenarios. Returns the number of references
//			 remaining. If it's zero, the file was removed from
//			 disk as unused.
//		Created: 15/7/04
//
// --------------------------------------------------------------------------

BackupStoreRefCountDatabase::refcount_t HousekeepStoreAccount::DeleteFile(
	int64_t InDirectory, int64_t ObjectID, BackupStoreDirectory &rDirectory,
	BackupStoreInfo& rBackupStoreInfo)
{
	// Find the entry inside the directory
	bool wasDeleted = false;
	bool wasOldVersion = false;
	int64_t deletedFileSizeInBlocks = 0;

	// A pointer to an object which requires committing if the directory save goes OK
	std::auto_ptr<BackupFileSystem::Transaction> ap_combine_files_trans;

	// BLOCK
	{
		BackupStoreRefCountDatabase::refcount_t refs =
			mpNewRefs->GetRefCount(ObjectID);

		BackupStoreDirectory::Entry *pentry = rDirectory.FindEntryByID(ObjectID);
		if(pentry == 0)
		{
			BOX_ERROR("Housekeeping on account " <<
				mrFileSystem.GetAccountIdentifier() << " "
				"found error: object " <<
				BOX_FORMAT_OBJECTID(ObjectID) << " "
				"not found in dir " <<
				BOX_FORMAT_OBJECTID(InDirectory) << ", "
				"indicates logic error/corruption? Run "
				"bbstoreaccounts check <accid> fix");
			mErrorCount++;
			return refs;
		}

		// Record the flags it's got set
		wasDeleted = pentry->IsDeleted();
		wasOldVersion = pentry->IsOld();
		// Check this should be deleted
		if(!wasDeleted && !wasOldVersion)
		{
			// Things changed since we were last around
			return refs;
		}

		// Record size
		deletedFileSizeInBlocks = pentry->GetSizeInBlocks();

		if(refs > 1)
		{
			// Not safe to merge patches if someone else has a
			// reference to this object, so just remove the
			// directory entry and return.
			rDirectory.DeleteEntry(ObjectID);
			if(wasDeleted)
			{
				rBackupStoreInfo.AdjustNumDeletedFiles(-1);
			}

			if(wasOldVersion)
			{
				rBackupStoreInfo.AdjustNumOldFiles(-1);
			}

			mpNewRefs->RemoveReference(ObjectID);
			return refs - 1;
		}

		// If the entry is involved in a chain of patches, it needs to be handled a bit
		// more carefully.

		BackupStoreDirectory::Entry *p_required = NULL, *p_requirer = NULL;

		if(pentry->GetDependsOnObject() != 0)
		{
			p_required = rDirectory.FindEntryByID(pentry->GetDependsOnObject());
			if(p_required == NULL ||
				p_required->GetRequiredByObject() != ObjectID)
			{
				THROW_EXCEPTION(BackupStoreException,
					PatchChainInfoBadInDirectory);
			}
		}

		if(pentry->GetRequiredByObject() != 0)
		{
			p_requirer = rDirectory.FindEntryByID(pentry->GetRequiredByObject());
			if(p_requirer == 0 || p_requirer->GetDependsOnObject() != ObjectID)
			{
				THROW_EXCEPTION(BackupStoreException,
					PatchChainInfoBadInDirectory);
			}
		}

		if(pentry->GetDependsOnObject() != 0 && pentry->GetRequiredByObject() == 0)
		{
			// This entry is a patch with no dependencies, so we can just delete it and
			// update the dependency info on the directory entry.
			// Change the info in the entry that this entry depends on, so that it no
			// longer points back to this entry:
			p_required->SetRequiredByObject(0);
		}
		else if(pentry->GetRequiredByObject() != 0)
		{
			// We should have checked whether the BackupFileSystem can merge patches
			// before this point:
			ASSERT(mrFileSystem.CanMergePatchesEasily());

			if(pentry->GetDependsOnObject() == 0)
			{
				// This object is at the end of a patch chain: it is a complete
				// file, but there exists another object which depends on it. Need
				// to combine the two, replacing that one (so that we can delete
				// this one).

				// Adjust the other entry in the directory, so that it no longer
				// points to this entry:
				p_requirer->SetDependsOnObject(0);

				// Actually combine the patch and file, but don't commit
				// the resulting file yet.
				ap_combine_files_trans = mrFileSystem.CombineFile(
					pentry->GetRequiredByObject(), ObjectID);
			}
			else
			{
				// This entry (pentry, ObjectID) is in the middle of a chain, and
				// two patches need combining.

				// First, adjust the directory entries
				if(p_required == 0 ||
					p_required->GetRequiredByObject() != ObjectID)
				{
					THROW_EXCEPTION(BackupStoreException,
						PatchChainInfoBadInDirectory);
				}
				// Remove the middle entry from the linked list by simply using the values from this entry
				p_required->SetRequiredByObject(pentry->GetRequiredByObject());
				p_requirer->SetDependsOnObject(pentry->GetDependsOnObject());

				// Actually combine the patch and file, but don't commit
				// the resulting file yet.
				ap_combine_files_trans = mrFileSystem.CombineDiffs(
					pentry->GetRequiredByObject(), ObjectID);
			}

			// COMMON CODE to both cases. The file will be committed later,
			// after the updated directory is safely committed.

			// Work out the adjusted size
			int64_t newSize = ap_combine_files_trans->GetNumBlocks();
			int64_t sizeDelta = newSize - p_requirer->GetSizeInBlocks();
			mBlocksUsedDelta += sizeDelta;
			if(p_requirer->IsDeleted())
			{
				mBlocksInDeletedFilesDelta += sizeDelta;
			}
			if(p_requirer->IsOld())
			{
				mBlocksInOldFilesDelta += sizeDelta;
			}
			if(!p_requirer->IsOld() && !p_requirer->IsDeleted())
			{
				mBlocksInCurrentFilesDelta += sizeDelta;
			}
			p_requirer->SetSizeInBlocks(newSize);
		}
	}

	// Delete it from the directory
	rDirectory.DeleteEntry(ObjectID);

	// Save directory back to disc
	// BLOCK
	{
		int64_t original_size = rDirectory.GetUserInfo1_SizeInBlocks();
		mrFileSystem.PutDirectory(rDirectory);

		// Adjust block counts if the directory itself changed in size
		int64_t new_size = rDirectory.GetUserInfo1_SizeInBlocks();
		int64_t adjust = new_size - original_size;
		mBlocksUsedDelta += adjust;
		mBlocksInDirectoriesDelta += adjust;

		UpdateDirectorySize(rDirectory, original_size, new_size);
	}

	// Commit the combined file to permanent storage
	if(ap_combine_files_trans.get() != 0)
	{
		ap_combine_files_trans->Commit();
		ap_combine_files_trans.reset(); // delete it now
	}

	// Drop reference count by one. Must now be zero, to delete the file.
	bool remaining_refs = mpNewRefs->RemoveReference(ObjectID);
	ASSERT(!remaining_refs);

	// Delete from disc
	BOX_TRACE("Removing unreferenced object " << BOX_FORMAT_OBJECTID(ObjectID));
	mrFileSystem.DeleteFile(ObjectID);

	// Adjust counts for the file
	++mFilesDeleted;
	mBlocksUsedDelta -= deletedFileSizeInBlocks;

	if(wasDeleted)
	{
		mBlocksInDeletedFilesDelta -= deletedFileSizeInBlocks;
		rBackupStoreInfo.AdjustNumDeletedFiles(-1);
	}

	if(wasOldVersion)
	{
		mBlocksInOldFilesDelta -= deletedFileSizeInBlocks;
		rBackupStoreInfo.AdjustNumOldFiles(-1);
	}

	// Delete the directory?
	// Do this if... dir has zero entries, and is marked as deleted in it's containing directory
	if(rDirectory.GetNumberOfEntries() == 0)
	{
		// Candidate for deletion
		mEmptyDirectories.push_back(InDirectory);
	}

	return 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::UpdateDirectorySize(
//			 BackupStoreDirectory& rDirectory,
//			 IOStream::pos_type old_size_in_blocks,
//			 IOStream::pos_type new_size_in_blocks)
//		Purpose: Update the directory size, modifying the parent
//			 directory's entry for this directory if necessary.
//		Created: 05/03/14
//
// --------------------------------------------------------------------------

void HousekeepStoreAccount::UpdateDirectorySize(
	BackupStoreDirectory& rDirectory,
	IOStream::pos_type old_size_in_blocks,
	IOStream::pos_type new_size_in_blocks)
{
	// The directory itself should already have been updated by the FileSystem.
	ASSERT(rDirectory.GetUserInfo1_SizeInBlocks() == new_size_in_blocks);

	if(new_size_in_blocks == old_size_in_blocks)
	{
		// No need to update the entry for this directory in its parent directory.
		return;
	}

	if(rDirectory.GetObjectID() == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		// The root directory has no parent, so no entry for it that might need
		// updating.
		return;
	}

	BackupStoreDirectory parent;
	mrFileSystem.GetDirectory(rDirectory.GetContainerID(), parent);

	BackupStoreDirectory::Entry* en =
		parent.FindEntryByID(rDirectory.GetObjectID());
	ASSERT(en);

	if(en->GetSizeInBlocks() != old_size_in_blocks)
	{
		BOX_WARNING("Directory " <<
			BOX_FORMAT_OBJECTID(rDirectory.GetObjectID()) <<
			" entry in directory " <<
			BOX_FORMAT_OBJECTID(rDirectory.GetContainerID()) <<
			" had incorrect size " << en->GetSizeInBlocks() <<
			", should have been " << old_size_in_blocks);
		mErrorCount++;
	}

	en->SetSizeInBlocks(new_size_in_blocks);
	mrFileSystem.PutDirectory(parent);
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
bool HousekeepStoreAccount::DeleteEmptyDirectories(BackupStoreInfo& rBackupStoreInfo)
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
				int account_id = mrFileSystem.GetAccountID();
				// Check for having to stop
				if(mpHousekeepingCallback &&
					// include account ID here as the specified account is now locked
					mpHousekeepingCallback->CheckForInterProcessMsg(account_id))
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

			DeleteEmptyDirectory(*i, toExamine, rBackupStoreInfo);
		}

		// Remove contents of empty directories
		mEmptyDirectories.clear();
		// Swap in new, so it's examined next time round
		mEmptyDirectories.swap(toExamine);
	}

	// Not interrupted
	return false;
}

void HousekeepStoreAccount::DeleteEmptyDirectory(int64_t dirId,
	std::vector<int64_t>& rToExamine, BackupStoreInfo& rBackupStoreInfo)
{
	// Load up the directory to potentially delete
	std::string dirFilename;
	BackupStoreDirectory dir;

	// BLOCK
	{
		// Check it actually exists (just in case it gets added twice to the list)
		ASSERT(mrFileSystem.ObjectExists(dirId));
		if(!mrFileSystem.ObjectExists(dirId))
		{
			// doesn't exist, next!
			return;
		}
		// load
		mrFileSystem.GetDirectory(dirId, dir);
	}

	// Make sure this directory is actually empty
	if(dir.GetNumberOfEntries() != 0)
	{
		// Not actually empty, try next one
		return;
	}

	// Candidate for deletion... open containing directory
	BackupStoreDirectory containingDir;
	mrFileSystem.GetDirectory(dir.GetContainerID(), containingDir);

	// Find the entry
	BackupStoreDirectory::Entry *pdirentry =
		containingDir.FindEntryByID(dir.GetObjectID());

	if((pdirentry != 0) && pdirentry->IsDeleted())
	{
		// Should be deleted
		containingDir.DeleteEntry(dir.GetObjectID());

		// Is the containing dir now a candidate for deletion?
		if(containingDir.GetNumberOfEntries() == 0)
		{
			rToExamine.push_back(containingDir.GetObjectID());
		}

		// Write revised parent directory
		int64_t old_size = containingDir.GetUserInfo1_SizeInBlocks();
		mrFileSystem.PutDirectory(containingDir);
		int64_t new_size = containingDir.GetUserInfo1_SizeInBlocks();

		// Removing an entry from the directory may have changed its size, so we
		// might need to update its parent as well.
		UpdateDirectorySize(containingDir, old_size, new_size);

		// adjust usage counts for this directory
		if(new_size > 0)
		{
			int64_t adjust = new_size - old_size;
			mBlocksUsedDelta += adjust;
			mBlocksInDirectoriesDelta += adjust;
		}

		if (mpNewRefs->RemoveReference(dir.GetObjectID()))
		{
			// Still referenced
			BOX_TRACE("Housekeeping spared empty deleted dir " <<
				BOX_FORMAT_OBJECTID(dirId) << " due to " <<
				mpNewRefs->GetRefCount(dir.GetObjectID()) <<
				" remaining references");
			return;
		}

		// Delete the directory itself
		BOX_INFO("Housekeeping removing empty deleted dir " <<
			BOX_FORMAT_OBJECTID(dirId));
		mrFileSystem.DeleteDirectory(dirId);

		// And adjust usage counts for the directory that's
		// just been deleted
		int64_t dirSizeInBlocks = dir.GetUserInfo1_SizeInBlocks();
		mBlocksUsedDelta -= dirSizeInBlocks;
		mBlocksInDirectoriesDelta -= dirSizeInBlocks;

		// Update count
		++mEmptyDirectoriesDeleted;
		rBackupStoreInfo.AdjustNumDirectories(-1);
	}
}

