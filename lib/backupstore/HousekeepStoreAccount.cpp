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
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "BufferedStream.h"
#include "HousekeepStoreAccount.h"
#include "NamedLock.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "StoreStructure.h"

#include "MemLeakFindOn.h"

// check every 32 directories scanned/files deleted
#define POLL_INTERPROCESS_MSG_CHECK_FREQUENCY	32

// --------------------------------------------------------------------------
//
// Function
//		Name:    HousekeepStoreAccount::HousekeepStoreAccount(int,
//			 const std::string &, int, BackupStoreDaemon &)
//		Purpose: Constructor
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
HousekeepStoreAccount::HousekeepStoreAccount(int AccountID,
	const std::string &rStoreRoot, int StoreDiscSet,
	HousekeepingCallback* pHousekeepingCallback)
	: mAccountID(AccountID),
	  mStoreRoot(rStoreRoot),
	  mStoreDiscSet(StoreDiscSet),
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
	  mBlocksInOldFilesDelta(0),
	  mBlocksInDeletedFilesDelta(0),
	  mBlocksInDirectoriesDelta(0),
	  mFilesDeleted(0),
	  mEmptyDirectoriesDeleted(0),
	  mCountUntilNextInterprocessMsgCheck(POLL_INTERPROCESS_MSG_CHECK_FREQUENCY)
{
	std::ostringstream tag;
	tag << "hk=" << BOX_FORMAT_ACCOUNT(mAccountID);
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
	if(mapNewRefs.get())
	{
		// Discard() can throw exception, but destructors aren't supposed to do that, so
		// just catch and log them.
		try
		{
			mapNewRefs->Discard();
		}
		catch(BoxException &e)
		{
			BOX_ERROR("Failed to destroy housekeeper: discarding the refcount "
				"database threw an exception: " << e.what());
		}
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
		BOX_FORMAT_ACCOUNT(mAccountID));

	// Attempt to lock the account
	std::string writeLockFilename;
	StoreStructure::MakeWriteLockFilename(mStoreRoot, mStoreDiscSet,
		writeLockFilename);
	NamedLock writeLock;
	if(!writeLock.TryAndGetLock(writeLockFilename.c_str(),
		0600 /* restrictive file permissions */))
	{
		if(KeepTryingForever)
		{
			BOX_INFO("Failed to lock account for housekeeping, "
				"still trying...");
			while(!writeLock.TryAndGetLock(writeLockFilename,
				0600 /* restrictive file permissions */))
			{
				sleep(1);
			}
		}
		else
		{
			// Couldn't lock the account -- just stop now
			return false;
		}
	}

	// Load the store info to find necessary info for the housekeeping
	std::auto_ptr<BackupStoreInfo> pInfo(BackupStoreInfo::Load(mAccountID,
		mStoreRoot, mStoreDiscSet, false /* Read/Write */));
	std::auto_ptr<BackupStoreInfo> pOldInfo(
		BackupStoreInfo::Load(mAccountID, mStoreRoot, mStoreDiscSet,
			true /* Read Only */));

	// If the account has a name, change the logging tag to include it
	if(!(pInfo->GetAccountName().empty()))
	{
		std::ostringstream tag;
		tag << "hk=" << BOX_FORMAT_ACCOUNT(mAccountID) << "/" <<
			pInfo->GetAccountName();
		mTagWithClientID.Change(tag.str());
	}

	// Calculate how much should be deleted
	mDeletionSizeTarget = pInfo->GetBlocksUsed() - pInfo->GetBlocksSoftLimit();
	if(mDeletionSizeTarget < 0)
	{
		mDeletionSizeTarget = 0;
	}

	BackupStoreAccountDatabase::Entry account(mAccountID, mStoreDiscSet);
	mapNewRefs = BackupStoreRefCountDatabase::Create(account);

	// Scan the directory for potential things to delete
	// This will also remove eligible items marked with RemoveASAP
	bool continueHousekeeping = ScanDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID,
		*pInfo);

	if(!continueHousekeeping)
	{
		// The scan was incomplete, so the new block counts are
		// incorrect, we can't rely on them. It's better to discard
		// the new info and adjust the old one instead.
		pInfo = pOldInfo;

		// We're about to reset counters and exit, so report what
		// happened now.
		BOX_INFO("Housekeeping on account " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << " removed " <<
			(0 - mBlocksUsedDelta) << " blocks (" <<
			mFilesDeleted << " files, " <<
			mEmptyDirectoriesDeleted << " dirs) and the directory "
			"scan was interrupted");
	}

	// If housekeeping made any changes, such as deleting RemoveASAP files,
	// the differences in block counts will be recorded in the deltas.
	pInfo->ChangeBlocksUsed(mBlocksUsedDelta);
	pInfo->ChangeBlocksInOldFiles(mBlocksInOldFilesDelta);
	pInfo->ChangeBlocksInDeletedFiles(mBlocksInDeletedFilesDelta);

	// Reset the delta counts for files, as they will include
	// RemoveASAP flagged files deleted during the initial scan.
	// keep removeASAPBlocksUsedDelta for reporting
	int64_t removeASAPBlocksUsedDelta = mBlocksUsedDelta;
	mBlocksUsedDelta = 0;
	mBlocksInOldFilesDelta = 0;
	mBlocksInDeletedFilesDelta = 0;

	// If scan directory stopped for some reason, probably parent
	// instructed to terminate, stop now.
	//
	// We can only update the refcount database if we successfully
	// finished our scan of all directories, otherwise we don't actually
	// know which of the new counts are valid and which aren't
	// (we might not have seen second references to some objects, etc.).

	if(!continueHousekeeping)
	{
		mapNewRefs->Discard();
		pInfo->Save();
		return false;
	}

	// Report any UNexpected changes, and consider them to be errors.
	// Do this before applying the expected changes below.
	mErrorCount += pInfo->ReportChangesTo(*pOldInfo);
	pInfo->Save();

	// Try to load the old reference count database and check whether
	// any counts have changed. We want to compare the mapNewRefs to
	// apOldRefs before we delete any files, because that will also change
	// the reference count in a way that's not an error.

	try
	{
		std::auto_ptr<BackupStoreRefCountDatabase> apOldRefs =
			BackupStoreRefCountDatabase::Load(account, false);
		mErrorCount += mapNewRefs->ReportChangesTo(*apOldRefs);
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
		BOX_INFO("Housekeeping on account " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << " "
			"removed " <<
			(0 - (mBlocksUsedDelta + removeASAPBlocksUsedDelta)) <<
			" blocks (" << mFilesDeleted << " files, " <<
			mEmptyDirectoriesDeleted << " dirs)" <<
			(deleteInterrupted?" and was interrupted":""));
	}

	// Make sure the delta's won't cause problems if the counts are
	// really wrong, and it wasn't fixed because the store was
	// updated during the scan.
	if(mBlocksUsedDelta < (0 - pInfo->GetBlocksUsed()))
	{
		mBlocksUsedDelta = (0 - pInfo->GetBlocksUsed());
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
	pInfo->ChangeBlocksInOldFiles(mBlocksInOldFilesDelta);
	pInfo->ChangeBlocksInDeletedFiles(mBlocksInDeletedFilesDelta);
	pInfo->ChangeBlocksInDirectories(mBlocksInDirectoriesDelta);

	// Save the store info back
	pInfo->Save();

	// force file to be saved and closed before releasing the lock below
	mapNewRefs->Commit();
	mapNewRefs.reset();

	// Explicity release the lock (would happen automatically on
	// going out of scope, included for code clarity)
	writeLock.ReleaseLock();

	BOX_TRACE("Finished housekeeping on account " <<
		BOX_FORMAT_ACCOUNT(mAccountID));
	return true;
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
		if(mpHousekeepingCallback && mpHousekeepingCallback->CheckForInterProcessMsg(mAccountID))
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
	std::auto_ptr<RaidFileRead> dirStream(RaidFileRead::Open(mStoreDiscSet,
		objectFilename));

	// Add the size of the directory on disc to the size being calculated
	int64_t originalDirSizeInBlocks = dirStream->GetDiscUsageInBlocks();
	mBlocksInDirectories += originalDirSizeInBlocks;
	mBlocksUsed += originalDirSizeInBlocks;

	// Read the directory in
	BackupStoreDirectory dir;
	BufferedStream buf(*dirStream);
	dir.ReadFromStream(buf, IOStream::TimeOutInfinite);
	dir.SetUserInfo1_SizeInBlocks(originalDirSizeInBlocks);
	dirStream->Close();

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
			mapNewRefs->AddReference(en->GetObjectID());
		}
	}

	// BLOCK
	{
		// Remove any files which are marked for removal as soon
		// as they become old or deleted.
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
					&& (en->IsDeleted() || en->IsOld()))
				{
					// Delete this immediately.
					DeleteFile(ObjectID, en->GetObjectID(), dir,
						objectFilename, rBackupStoreInfo);

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
		typedef std::pair<std::string, int32_t> version_t;
		std::map<version_t, int32_t> markVersionAges;
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
			if(mpHousekeepingCallback && mpHousekeepingCallback->CheckForInterProcessMsg(mAccountID))	// include account ID here as the specified account is now locked
			{
				// Need to abort now. Return true to signal that we were interrupted.
				return true;
			}
		}
#endif

		// Load up the directory it's in
		// Get the filename
		std::string dirFilename;
		BackupStoreDirectory dir;
		{
			MakeObjectFilename(i->mInDirectory, dirFilename);
			std::auto_ptr<RaidFileRead> dirStream(RaidFileRead::Open(mStoreDiscSet, dirFilename));
			dir.ReadFromStream(*dirStream, IOStream::TimeOutInfinite);
			dir.SetUserInfo1_SizeInBlocks(dirStream->GetDiscUsageInBlocks());
		}

		// Delete the file
		BackupStoreRefCountDatabase::refcount_t refs =
			DeleteFile(i->mInDirectory, i->mObjectID, dir,
				dirFilename, rBackupStoreInfo);
		if(refs == 0)
		{
			BOX_INFO("Housekeeping removed " <<
				(i->mIsFlagDeleted ? "deleted" : "old") <<
				" file " << BOX_FORMAT_OBJECTID(i->mObjectID) <<
				" from dir " << BOX_FORMAT_OBJECTID(i->mInDirectory));
		}
		else
		{
			BOX_TRACE("Housekeeping preserved " <<
				(i->mIsFlagDeleted ? "deleted" : "old") <<
				" file " << BOX_FORMAT_OBJECTID(i->mObjectID) <<
				" in dir " << BOX_FORMAT_OBJECTID(i->mInDirectory) <<
				" with " << refs << " references");
		}

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
	const std::string &rDirectoryFilename,
	BackupStoreInfo& rBackupStoreInfo)
{
	// Find the entry inside the directory
	bool wasDeleted = false;
	bool wasOldVersion = false;
	int64_t deletedFileSizeInBlocks = 0;
	// A pointer to an object which requires committing if the directory save goes OK
	std::auto_ptr<RaidFileWrite> padjustedEntry;
	// BLOCK
	{
		BackupStoreRefCountDatabase::refcount_t refs =
			mapNewRefs->GetRefCount(ObjectID);

		BackupStoreDirectory::Entry *pentry = rDirectory.FindEntryByID(ObjectID);
		if(pentry == 0)
		{
			BOX_ERROR("Housekeeping on account " <<
				BOX_FORMAT_ACCOUNT(mAccountID) << " "
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

			mapNewRefs->RemoveReference(ObjectID);
			return refs - 1;
		}

		// If the entry is involved in a chain of patches, it needs to be handled a bit
		// more carefully.

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
				// There exists an older version which depends on this
				// one. Need to combine the two over that one.

				// Adjust the other entry in the directory
				if(polder == 0 || polder->GetDependsNewer() != ObjectID)
				{
					THROW_EXCEPTION(BackupStoreException,
						PatchChainInfoBadInDirectory);
				}
				// Change the info in the older entry so that this no
				// longer points to this entry.
				polder->SetDependsNewer(0);
			}
			else
			{
				// This entry is in the middle of a chain, and two
				// patches need combining.

				// First, adjust the directory entries
				BackupStoreDirectory::Entry *pnewer = rDirectory.FindEntryByID(pentry->GetDependsNewer());
				if(pnewer == 0 || pnewer->GetDependsOlder() != ObjectID
					|| polder == 0 || polder->GetDependsNewer() != ObjectID)
				{
					THROW_EXCEPTION(BackupStoreException,
						PatchChainInfoBadInDirectory);
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
			padjustedEntry.reset(new RaidFileWrite(mStoreDiscSet,
				objFilenameOlder, mapNewRefs->GetRefCount(ObjectID)));
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
			if(polder->IsDeleted())
			{
				mBlocksInDeletedFilesDelta += sizeDelta;
			}
			if(polder->IsOld())
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
	{
		RaidFileWrite writeDir(mStoreDiscSet, rDirectoryFilename,
			mapNewRefs->GetRefCount(InDirectory));
		writeDir.Open(true /* allow overwriting */);
		rDirectory.WriteToStream(writeDir);

		// Get the disc usage (must do this before commiting it)
		int64_t new_size = writeDir.GetDiscUsageInBlocks();

		// Commit directory
		writeDir.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);

		// Adjust block counts if the directory itself changed in size
		int64_t original_size = rDirectory.GetUserInfo1_SizeInBlocks();
		int64_t adjust = new_size - original_size;
		mBlocksUsedDelta += adjust;
		mBlocksInDirectoriesDelta += adjust;

		UpdateDirectorySize(rDirectory, new_size);
	}

	// Commit any new adjusted entry
	if(padjustedEntry.get() != 0)
	{
		padjustedEntry->Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
		padjustedEntry.reset(); // delete it now
	}

	// Drop reference count by one. Must now be zero, to delete the file.
	bool remaining_refs = mapNewRefs->RemoveReference(ObjectID);
	ASSERT(!remaining_refs);

	// Delete from disc
	BOX_TRACE("Removing unreferenced object " << BOX_FORMAT_OBJECTID(ObjectID));
	std::string objFilename;
	MakeObjectFilename(ObjectID, objFilename);
	RaidFileWrite del(mStoreDiscSet, objFilename, mapNewRefs->GetRefCount(ObjectID));
	del.Delete();

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
//			 IOStream::pos_type new_size_in_blocks)
//		Purpose: Update the directory size, modifying the parent
//			 directory's entry for this directory if necessary.
//		Created: 05/03/14
//
// --------------------------------------------------------------------------

void HousekeepStoreAccount::UpdateDirectorySize(
	BackupStoreDirectory& rDirectory,
	IOStream::pos_type new_size_in_blocks)
{
#ifndef BOX_RELEASE_BUILD
	{
		std::string dirFilename;
		MakeObjectFilename(rDirectory.GetObjectID(), dirFilename);
		std::auto_ptr<RaidFileRead> dirStream(
			RaidFileRead::Open(mStoreDiscSet, dirFilename));
		ASSERT(new_size_in_blocks == dirStream->GetDiscUsageInBlocks());
	}
#endif

	IOStream::pos_type old_size_in_blocks =
		rDirectory.GetUserInfo1_SizeInBlocks();

	if(new_size_in_blocks == old_size_in_blocks)
	{
		// The root directory has no parent, so no entry for it that might need
		// updating.
		return;
	}

	rDirectory.SetUserInfo1_SizeInBlocks(new_size_in_blocks);

	if(rDirectory.GetObjectID() == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return;
	}

	std::string parentFilename;
	MakeObjectFilename(rDirectory.GetContainerID(), parentFilename);
	std::auto_ptr<RaidFileRead> parentStream(
		RaidFileRead::Open(mStoreDiscSet, parentFilename));
	BackupStoreDirectory parent(*parentStream);
	parentStream.reset();

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

	RaidFileWrite writeDir(mStoreDiscSet, parentFilename,
		mapNewRefs->GetRefCount(rDirectory.GetContainerID()));
	writeDir.Open(true /* allow overwriting */);
	parent.WriteToStream(writeDir);
	writeDir.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
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
				// Check for having to stop
				if(mpHousekeepingCallback &&
					// include account ID here as the specified account is now locked
					mpHousekeepingCallback->CheckForInterProcessMsg(mAccountID))
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
	int64_t dirSizeInBlocks = 0;

	// BLOCK
	{
		MakeObjectFilename(dirId, dirFilename);
		// Check it actually exists (just in case it gets added twice to the list)
		if(!RaidFileRead::FileExists(mStoreDiscSet, dirFilename))
		{
			// doesn't exist, next!
			return;
		}
		// load
		std::auto_ptr<RaidFileRead> dirStream(
			RaidFileRead::Open(mStoreDiscSet, dirFilename));
		dirSizeInBlocks = dirStream->GetDiscUsageInBlocks();
		dir.ReadFromStream(*dirStream, IOStream::TimeOutInfinite);
	}

	// Make sure this directory is actually empty
	if(dir.GetNumberOfEntries() != 0)
	{
		// Not actually empty, try next one
		return;
	}

	// Candidate for deletion... open containing directory
	std::string containingDirFilename;
	BackupStoreDirectory containingDir;
	int64_t containingDirSizeInBlocksOrig = 0;
	{
		MakeObjectFilename(dir.GetContainerID(), containingDirFilename);
		std::auto_ptr<RaidFileRead> containingDirStream(
			RaidFileRead::Open(mStoreDiscSet,
				containingDirFilename));
		containingDirSizeInBlocksOrig =
			containingDirStream->GetDiscUsageInBlocks();
		containingDir.ReadFromStream(*containingDirStream,
			IOStream::TimeOutInfinite);
		containingDir.SetUserInfo1_SizeInBlocks(containingDirSizeInBlocksOrig);
	}

	// Find the entry
	BackupStoreDirectory::Entry *pdirentry =
		containingDir.FindEntryByID(dir.GetObjectID());
	// TODO FIXME invert test and reduce indentation
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
		RaidFileWrite writeDir(mStoreDiscSet, containingDirFilename,
			mapNewRefs->GetRefCount(containingDir.GetObjectID()));
		writeDir.Open(true /* allow overwriting */);
		containingDir.WriteToStream(writeDir);

		// get the disc usage (must do this before commiting it)
		int64_t dirSize = writeDir.GetDiscUsageInBlocks();

		// Commit directory
		writeDir.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
		UpdateDirectorySize(containingDir, dirSize);

		// adjust usage counts for this directory
		if(dirSize > 0)
		{
			int64_t adjust = dirSize - containingDirSizeInBlocksOrig;
			mBlocksUsedDelta += adjust;
			mBlocksInDirectoriesDelta += adjust;
		}

		if (mapNewRefs->RemoveReference(dir.GetObjectID()))
		{
			// Still referenced
			BOX_TRACE("Housekeeping spared empty deleted dir " <<
				BOX_FORMAT_OBJECTID(dirId) << " due to " <<
				mapNewRefs->GetRefCount(dir.GetObjectID()) <<
				" remaining references");
			return;
		}

		// Delete the directory itself
		BOX_INFO("Housekeeping removing empty deleted dir " <<
			BOX_FORMAT_OBJECTID(dirId));
		RaidFileWrite del(mStoreDiscSet, dirFilename,
			mapNewRefs->GetRefCount(dir.GetObjectID()));
		del.Delete();

		// And adjust usage counts for the directory that's
		// just been deleted
		mBlocksUsedDelta -= dirSizeInBlocks;
		mBlocksInDirectoriesDelta -= dirSizeInBlocks;

		// Update count
		++mEmptyDirectoriesDeleted;
		rBackupStoreInfo.AdjustNumDirectories(-1);
	}
}

