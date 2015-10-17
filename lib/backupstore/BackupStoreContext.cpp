// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreContext.cpp
//		Purpose: Context for backup store server
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "BackupConstants.h"
#include "BackupStoreContext.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreInfo.h"
#include "BackupStoreObjectMagic.h"
#include "BufferedStream.h"
#include "BufferedWriteStream.h"
#include "FileStream.h"

#include "MemLeakFindOn.h"


// Maximum number of directories to keep in the cache When the cache is bigger
// than this, everything gets deleted. In tests, we set the cache size to zero
// to ensure that it's always flushed, which is very inefficient but helps to
// catch programming errors (use of freed data).
#ifdef BOX_RELEASE_BUILD
	#define	MAX_CACHE_SIZE	32
#else
	#define	MAX_CACHE_SIZE	0
#endif

// Allow the housekeeping process 4 seconds to release an account
#define MAX_WAIT_FOR_HOUSEKEEPING_TO_RELEASE_ACCOUNT	4

// Maximum amount of store info updates before it's actually saved to disc.
#define STORE_INFO_SAVE_DELAY	96

#define CHECK_FILESYSTEM_INITIALISED() \
if(!mapFileSystem.get()) { \
	THROW_EXCEPTION(BackupStoreException, FileSystemNotInitialised); \
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::BackupStoreContext()
//		Purpose: Constructor
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
BackupStoreContext::BackupStoreContext(int32_t ClientID,
	HousekeepingInterface* pHousekeeping, const std::string& rConnectionDetails)
: mConnectionDetails(rConnectionDetails),
  mClientID(ClientID),
  mpHousekeeping(pHousekeeping),
  mProtocolPhase(Phase_START),
  mClientHasAccount(false),
  mReadOnly(true),
  mSaveStoreInfoDelay(STORE_INFO_SAVE_DELAY),
  mpTestHook(NULL)
// If you change the initialisers, be sure to update
// BackupStoreContext::ReceivedFinishCommand as well!
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::~BackupStoreContext()
//		Purpose: Destructor
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
BackupStoreContext::~BackupStoreContext()
{
	ReleaseWriteLock();
	ClearDirectoryCache();
}


void BackupStoreContext::ClearDirectoryCache()
{
	// Delete the objects in the cache
	for(std::map<int64_t, BackupStoreDirectory*>::iterator i(mDirectoryCache.begin());
		i != mDirectoryCache.end(); ++i)
	{
		delete (i->second);
	}
	mDirectoryCache.clear();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::CleanUp()
//		Purpose: Clean up after a connection
//		Created: 16/12/03
//
// --------------------------------------------------------------------------
void BackupStoreContext::CleanUp()
{
	// Make sure the store info is saved, if it has been loaded, isn't read only and has been modified
	if(mapStoreInfo.get() && !(mapStoreInfo->IsReadOnly()) &&
		mapStoreInfo->IsModified())
	{
		mapStoreInfo->Save();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::ReceivedFinishCommand()
//		Purpose: Called when the finish command is received by the protocol
//		Created: 16/12/03
//
// --------------------------------------------------------------------------
void BackupStoreContext::ReceivedFinishCommand()
{
	if(!mReadOnly && mapStoreInfo.get())
	{
		// Save the store info, not delayed
		SaveStoreInfo(false);
	}

	ReleaseWriteLock();

	// Just in case someone wants to reuse a local protocol object,
	// put the context back to its initial state.
	mProtocolPhase = BackupStoreContext::Phase_Version;

	// Avoid the need to check version again, by not resetting mClientHasAccount.
	mReadOnly = true;
	mSaveStoreInfoDelay = STORE_INFO_SAVE_DELAY;
	mpTestHook = NULL;
	mapStoreInfo.reset();
	mapRefCount.reset();
	ClearDirectoryCache();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::AttemptToGetWriteLock()
//		Purpose: Attempt to get a write lock for the store, and if so, unset the read only flags
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
bool BackupStoreContext::AttemptToGetWriteLock()
{
	CHECK_FILESYSTEM_INITIALISED();

	// Request the lock
	bool gotLock = mapFileSystem->TryGetLock();

	if(!gotLock && mpHousekeeping)
	{
		// The housekeeping process might have the thing open -- ask it to stop
		char msg[256];
		int msgLen = snprintf(msg, sizeof(msg), "r%x\n", mClientID);
		// Send message
		mpHousekeeping->SendMessageToHousekeepingProcess(msg, msgLen);

		// Then try again a few times
		int tries = MAX_WAIT_FOR_HOUSEKEEPING_TO_RELEASE_ACCOUNT;
		do
		{
			::sleep(1 /* second */);
			--tries;
			gotLock = mapFileSystem->TryGetLock();

		} while(!gotLock && tries > 0);
	}

	if(gotLock)
	{
		// Got the lock, mark as not read only
		mReadOnly = false;
	}

	return gotLock;
}


void BackupStoreContext::SetClientHasAccount(const std::string &rStoreRoot,
	int StoreDiscSet)
{
	mClientHasAccount = true;
	mapFileSystem.reset(new RaidBackupFileSystem(rStoreRoot, StoreDiscSet));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::LoadStoreInfo()
//		Purpose: Load the store info from disc
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
void BackupStoreContext::LoadStoreInfo()
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() != 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoAlreadyLoaded)
	}

	// Load it up!
	std::auto_ptr<BackupStoreInfo> i = mapFileSystem->GetBackupStoreInfo(mClientID,
		mReadOnly);

	// Check it
	if(i->GetAccountID() != mClientID)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoForWrongAccount)
	}

	// Keep the pointer to it
	mapStoreInfo = i;

	// Try to load the reference count database
	try
	{
		mapRefCount = mapFileSystem->GetRefCountDatabase(mClientID, mReadOnly);
	}
	catch(BoxException &e)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException,
			CorruptReferenceCountDatabase, "Reference count "
			"database is missing or corrupted, cannot safely open "
			"account. Housekeeping will fix this automatically "
			"when it next runs.");
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::SaveStoreInfo(bool)
//		Purpose: Potentially delayed saving of the store info
//		Created: 16/12/03
//
// --------------------------------------------------------------------------
void BackupStoreContext::SaveStoreInfo(bool AllowDelay)
{
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	// Can delay saving it a little while?
	if(AllowDelay)
	{
		--mSaveStoreInfoDelay;
		if(mSaveStoreInfoDelay > 0)
		{
			return;
		}
	}

	// Want to save now
	mapFileSystem->PutBackupStoreInfo(*mapStoreInfo);

	// Reset counter for next delayed save.
	mSaveStoreInfoDelay = STORE_INFO_SAVE_DELAY;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::GetDirectoryInternal(int64_t,
//			 bool)
//		Purpose: Return a reference to a directory, valid only until
//			 the next time a function which may flush the
//			 directory cache is called: mainly this function
//			 (with AllowFlushCache == true) or creation of files.
//			 This is a private function which returns non-const
//			 references to directories in the cache. If the cache
//			 is flushed, it will invalidate all directory
//			 references that you may be holding, so beware.
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
BackupStoreDirectory &BackupStoreContext::GetDirectoryInternal(int64_t ObjectID,
	bool AllowFlushCache)
{
	CHECK_FILESYSTEM_INITIALISED();

	// Get the filename
	int64_t oldRevID = 0, newRevID = 0;
	bool gotRevID = false;

	// Already in cache?
	std::map<int64_t, BackupStoreDirectory*>::iterator item(mDirectoryCache.find(ObjectID));
	if(item != mDirectoryCache.end()) {
#ifndef BOX_RELEASE_BUILD // it might be in the cache, but invalidated
		// in which case, delete it instead of returning it.
		if(!item->second->IsInvalidated())
#else
		if(true)
#endif
		{
			oldRevID = item->second->GetRevisionID();

			// Check the revision ID of the file -- does it need refreshing?
			if(!mapFileSystem->ObjectExists(ObjectID, &newRevID))
			{
				THROW_EXCEPTION(BackupStoreException, DirectoryHasBeenDeleted)
			}

			gotRevID = true;

			if(newRevID == oldRevID)
			{
				// Looks good... return the cached object
				BOX_TRACE("Returning object " <<
					BOX_FORMAT_OBJECTID(ObjectID) <<
					" from cache, modtime = " << newRevID)
				return *(item->second);
			}
		}

		// Delete this cached object
		delete item->second;
		mDirectoryCache.erase(item);
	}

	// Need to load it up

	// First check to see if the cache is too big
	if(mDirectoryCache.size() > MAX_CACHE_SIZE && AllowFlushCache)
	{
		// Very simple. Just delete everything! But in debug builds,
		// leave the entries in the cache and invalidate them instead,
		// so that any attempt to access them will cause an assertion
		// failure that helps to track down the error.
#ifdef BOX_RELEASE_BUILD
		ClearDirectoryCache();
#else
		for(std::map<int64_t, BackupStoreDirectory*>::iterator
			i = mDirectoryCache.begin();
			i != mDirectoryCache.end(); i++)
		{
			i->second->Invalidate();
		}
#endif
	}

	if(!gotRevID)
	{
		if(!mapFileSystem->ObjectExists(ObjectID, &newRevID))
		{
			THROW_EXCEPTION(BackupStoreException, ObjectDoesNotExist);
		}
	}

	// Get an IOStream to read it in
	ASSERT(newRevID != 0);

	if (oldRevID == 0)
	{
		BOX_TRACE("Loading object " << BOX_FORMAT_OBJECTID(ObjectID) <<
			" with modtime " << newRevID);
	}
	else
	{
		BOX_TRACE("Refreshing object " << BOX_FORMAT_OBJECTID(ObjectID) <<
			" in cache, modtime changed from " << oldRevID <<
			" to " << newRevID);
	}

	// Read it from the stream, then set it's revision ID
	std::auto_ptr<BackupStoreDirectory> apDir(new BackupStoreDirectory());
	mapFileSystem->GetDirectory(ObjectID, *apDir);

	// Store in cache
	mDirectoryCache[ObjectID] = apDir.release();

	// Return it
	return *mDirectoryCache[ObjectID];
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::AllocateObjectID()
//		Purpose: Allocate a new object ID, tolerant of failures to save store info
//		Created: 16/12/03
//
// --------------------------------------------------------------------------
int64_t BackupStoreContext::AllocateObjectID()
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	// Given that the store info may not be saved for STORE_INFO_SAVE_DELAY
	// times after it has been updated, this is a reasonable number of times
	// to try for finding an unused ID.
	// (Sizes used in the store info are fixed by the housekeeping process)
	int retryLimit = (STORE_INFO_SAVE_DELAY * 2);

	while(retryLimit > 0)
	{
		// Attempt to allocate an ID from the store
		int64_t id = mapStoreInfo->AllocateObjectID();

		// Check it doesn't exist
		if(!mapFileSystem->ObjectExists(id))
		{
			// Success!
			return id;
		}

		// Decrement retry count, and try again
		--retryLimit;

		// Mark that the store info should be saved as soon as possible
		mSaveStoreInfoDelay = 0;

		BOX_WARNING("When allocating object ID, found that " <<
			BOX_FORMAT_OBJECTID(id) << " is already in use");
	}

	THROW_EXCEPTION(BackupStoreException, CouldNotFindUnusedIDDuringAllocation)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::AddFile(IOStream &, int64_t,
//			 int64_t, int64_t, const BackupStoreFilename &, bool)
//		Purpose: Add a file to the store, from a given stream, into
//			 a specified directory. Returns object ID of the new
//			 file.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
int64_t BackupStoreContext::AddFile(IOStream &rFile, int64_t InDirectory,
	int64_t ModificationTime, int64_t AttributesHash,
	int64_t DiffFromFileID, const BackupStoreFilename &rFilename,
	bool MarkFileWithSameNameAsOldVersions)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	// This is going to be a bit complex to make sure it copes OK
	// with things going wrong.
	// The only thing which isn't safe is incrementing the object ID
	// and keeping the blocks used entirely accurate -- but these
	// aren't big problems if they go horribly wrong. The sizes will
	// be corrected the next time the account has a housekeeping run,
	// and the object ID allocation code is tolerant of missed IDs.
	// (the info is written lazily, so these are necessary)

	// Get the directory we want to modify
	BackupStoreDirectory &dir(GetDirectoryInternal(InDirectory));

	// Allocate the next ID
	int64_t id = AllocateObjectID();

	// Stream the file to disc
	BackupStoreInfo::Adjustment adjustment = {};
	std::auto_ptr<BackupFileSystem::Transaction> apTransaction;

	// Diff or full file?
	if(DiffFromFileID == 0)
	{
		apTransaction = mapFileSystem->PutFileComplete(id, rFile);
	}
	else
	{
		// Check that the diffed from ID actually exists in the directory
		if(dir.FindEntryByID(DiffFromFileID) == 0)
		{
			THROW_EXCEPTION(BackupStoreException,
				DiffFromIDNotFoundInDirectory)
		}

		apTransaction = mapFileSystem->PutFilePatch(id, DiffFromFileID,
			rFile);
	}

	// Get the blocks used
	int64_t changeInBlocksUsed = apTransaction->GetNumBlocks() +
		apTransaction->GetChangeInBlocksUsedByOldFile();
	adjustment.mBlocksUsed += changeInBlocksUsed;
	adjustment.mBlocksInCurrentFiles += changeInBlocksUsed;
	adjustment.mNumCurrentFiles++;

	// Exceeds the hard limit?
	int64_t newTotalBlocksUsed = mapStoreInfo->GetBlocksUsed() +
		changeInBlocksUsed;
	if(newTotalBlocksUsed > mapStoreInfo->GetBlocksHardLimit())
	{
		// This will cancel the Transaction and delete the RaidFile(s).
		THROW_EXCEPTION(BackupStoreException, AddedFileExceedsStorageLimit)
	}

	// Can only get this before we commit the RaidFiles.
	int64_t numBlocksInNewFile = apTransaction->GetNumBlocks();
	int64_t changeInBlocksUsedByOldFile =
		apTransaction->GetChangeInBlocksUsedByOldFile();

	// Modify the directory -- first make all files with the same name
	// marked as an old version
	try
	{
		// Adjust the entry for the object that we replaced with a
		// patch, above.
		BackupStoreDirectory::Entry *poldEntry = NULL;

		if(DiffFromFileID != 0)
		{
			// Get old version entry
			poldEntry = dir.FindEntryByID(DiffFromFileID);
			ASSERT(poldEntry != 0);

			// Adjust size of old entry
			int64_t oldSize = poldEntry->GetSizeInBlocks();
			poldEntry->SetSizeInBlocks(oldSize + changeInBlocksUsedByOldFile);
		}

		if(MarkFileWithSameNameAsOldVersions)
		{
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *e = 0;
			while((e = i.Next()) != 0)
			{
				// First, check it's not an old version (cheaper comparison)
				if(! e->IsOld())
				{
					// Compare name
					if(e->GetName() == rFilename)
					{
						// Check that it's definately not an old version
						ASSERT((e->GetFlags() & BackupStoreDirectory::Entry::Flags_OldVersion) == 0);
						// Set old version flag
						e->AddFlags(BackupStoreDirectory::Entry::Flags_OldVersion);
						// Can safely do this, because we know we won't be here if it's already 
						// an old version
						adjustment.mBlocksInOldFiles += e->GetSizeInBlocks();
						adjustment.mBlocksInCurrentFiles -= e->GetSizeInBlocks();
						adjustment.mNumOldFiles++;
						adjustment.mNumCurrentFiles--;
					}
				}
			}
		}

		// Then the new entry
		BackupStoreDirectory::Entry *pnewEntry = dir.AddEntry(rFilename,
			ModificationTime, id, numBlocksInNewFile,
			BackupStoreDirectory::Entry::Flags_File, AttributesHash);

		// Adjust dependency info of file?
		if(DiffFromFileID && poldEntry && !apTransaction->IsNewFileIndependent())
		{
			poldEntry->SetDependsNewer(id);
			pnewEntry->SetDependsOlder(DiffFromFileID);
		}

		// Write the directory back to disc
		SaveDirectory(dir);

		// It is now safe to commit the old version's new patched version, now
		// that the directory safely reflects the state of the files on disc.
	}
	catch(...)
	{
		// Remove this entry from the cache
		RemoveDirectoryFromCache(InDirectory);

		// Leaving this function without committing the Transaction will cancel
		// it, deleting the new file and not modifying the old one. Don't worry
		// about the incremented numbers in the store info, they won't cause
		// any real problem and bbstoreaccounts check can fix them.
		throw;
	}

	// Commit the new file, and replace the old file (if any) with a patch.
	apTransaction->Commit();

	// Modify the store info
	mapStoreInfo->AdjustNumCurrentFiles(adjustment.mNumCurrentFiles);
	mapStoreInfo->AdjustNumOldFiles(adjustment.mNumOldFiles);
	mapStoreInfo->AdjustNumDeletedFiles(adjustment.mNumDeletedFiles);
	mapStoreInfo->AdjustNumDirectories(adjustment.mNumDirectories);
	mapStoreInfo->ChangeBlocksUsed(adjustment.mBlocksUsed);
	mapStoreInfo->ChangeBlocksInCurrentFiles(adjustment.mBlocksInCurrentFiles);
	mapStoreInfo->ChangeBlocksInOldFiles(adjustment.mBlocksInOldFiles);
	mapStoreInfo->ChangeBlocksInDeletedFiles(adjustment.mBlocksInDeletedFiles);
	mapStoreInfo->ChangeBlocksInDirectories(adjustment.mBlocksInDirectories);

	// Increment reference count on the new directory to one
	mapRefCount->AddReference(id);

	// Save the store info -- can cope if this exceptions because infomation
	// will be rebuilt by housekeeping, and ID allocation can recover.
	SaveStoreInfo(false);

	// Return the ID to the caller
	return id;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::DeleteFile(
//			 const BackupStoreFilename &, int64_t, int64_t &)
//		Purpose: Deletes a file by name, returning true if the file
//			 existed. Object ID returned too, set to zero if not
//			 found.
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
bool BackupStoreContext::DeleteFile(const BackupStoreFilename &rFilename, int64_t InDirectory, int64_t &rObjectIDOut)
{
	CHECK_FILESYSTEM_INITIALISED();

	// Essential checks!
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	// Find the directory the file is in (will exception if it fails)
	BackupStoreDirectory &dir(GetDirectoryInternal(InDirectory));

	// Setup flags
	bool fileExisted = false;
	bool madeChanges = false;
	rObjectIDOut = 0;		// not found

	try
	{
		// Iterate through directory, only looking at files which haven't been deleted
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *e = 0;
		while((e = i.Next(BackupStoreDirectory::Entry::Flags_File,
			BackupStoreDirectory::Entry::Flags_Deleted)) != 0)
		{
			// Compare name
			if(e->GetName() == rFilename)
			{
				// Check that it's definately not already deleted
				ASSERT(!e->IsDeleted());
				// Set deleted flag
				e->AddFlags(BackupStoreDirectory::Entry::Flags_Deleted);
				// Mark as made a change
				madeChanges = true;

				int64_t blocks = e->GetSizeInBlocks();
				mapStoreInfo->AdjustNumDeletedFiles(1);
				mapStoreInfo->ChangeBlocksInDeletedFiles(blocks);

				// We're marking all old versions as deleted.
				// This is how a file can be old and deleted
				// at the same time. So we don't subtract from
				// number or size of old files. But if it was
				// a current file, then it's not any more, so
				// we do need to adjust the current counts.
				if(!e->IsOld())
				{
					mapStoreInfo->AdjustNumCurrentFiles(-1);
					mapStoreInfo->ChangeBlocksInCurrentFiles(-blocks);
				}

				// Is this the last version?
				if((e->GetFlags() & BackupStoreDirectory::Entry::Flags_OldVersion) == 0)
				{
					// Yes. It's been found.
					rObjectIDOut = e->GetObjectID();
					fileExisted = true;
				}
			}
		}

		// Save changes?
		if(madeChanges)
		{
			// Save the directory back
			SaveDirectory(dir);
			SaveStoreInfo(false);
		}
	}
	catch(...)
	{
		RemoveDirectoryFromCache(InDirectory);
		throw;
	}

	return fileExisted;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::UndeleteFile(int64_t, int64_t)
//		Purpose: Undeletes a file, if it exists, returning true if
//			 the file existed.
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
bool BackupStoreContext::UndeleteFile(int64_t ObjectID, int64_t InDirectory)
{
	CHECK_FILESYSTEM_INITIALISED();

	// Essential checks!
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	// Find the directory the file is in (will exception if it fails)
	BackupStoreDirectory &dir(GetDirectoryInternal(InDirectory));

	// Setup flags
	bool fileExisted = false;
	bool madeChanges = false;

	// Count of deleted blocks
	int64_t blocksDel = 0;

	try
	{
		// Iterate through directory, only looking at files which have been deleted
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *e = 0;
		while((e = i.Next(BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_Deleted, 0)) != 0)
		{
			// Compare name
			if(e->GetObjectID() == ObjectID)
			{
				// Check that it's definitely already deleted
				ASSERT((e->GetFlags() & BackupStoreDirectory::Entry::Flags_Deleted) != 0);
				// Clear deleted flag
				e->RemoveFlags(BackupStoreDirectory::Entry::Flags_Deleted);
				// Mark as made a change
				madeChanges = true;
				blocksDel -= e->GetSizeInBlocks();

				// Is this the last version?
				if((e->GetFlags() & BackupStoreDirectory::Entry::Flags_OldVersion) == 0)
				{
					// Yes. It's been found.
					fileExisted = true;
				}
			}
		}

		// Save changes?
		if(madeChanges)
		{
			// Save the directory back
			SaveDirectory(dir);

			// Modify the store info, and write
			mapStoreInfo->ChangeBlocksInDeletedFiles(blocksDel);

			// Maybe postponed save of store info
			SaveStoreInfo();
		}
	}
	catch(...)
	{
		RemoveDirectoryFromCache(InDirectory);
		throw;
	}

	return fileExisted;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::RemoveDirectoryFromCache(int64_t)
//		Purpose: Remove directory from cache
//		Created: 2003/09/04
//
// --------------------------------------------------------------------------
void BackupStoreContext::RemoveDirectoryFromCache(int64_t ObjectID)
{
	std::map<int64_t, BackupStoreDirectory*>::iterator item(mDirectoryCache.find(ObjectID));
	if(item != mDirectoryCache.end())
	{
		// Delete this cached object
		delete item->second;
		// Erase the entry form the map
		mDirectoryCache.erase(item);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::SaveDirectory(BackupStoreDirectory &)
//		Purpose: Save directory back to disc, update time in cache
//		Created: 2003/09/04
//
// --------------------------------------------------------------------------
void BackupStoreContext::SaveDirectory(BackupStoreDirectory &rDir)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	int64_t ObjectID = rDir.GetObjectID();

	try
	{
		// Write to disc, adjust size in store info
		int64_t old_dir_size = rDir.GetUserInfo1_SizeInBlocks();

		mapFileSystem->PutDirectory(rDir);
		int64_t new_dir_size = rDir.GetUserInfo1_SizeInBlocks();

		{
			int64_t sizeAdjustment = new_dir_size - old_dir_size;
			mapStoreInfo->ChangeBlocksUsed(sizeAdjustment);
			mapStoreInfo->ChangeBlocksInDirectories(sizeAdjustment);
		}

		// Update the directory entry in the grandparent, to ensure
		// that it reflects the current size of the parent directory.
		if(new_dir_size != old_dir_size &&
			ObjectID != BACKUPSTORE_ROOT_DIRECTORY_ID)
		{
			int64_t ContainerID = rDir.GetContainerID();
			BackupStoreDirectory& parent(
				GetDirectoryInternal(ContainerID));
			// rDir is now invalid
			BackupStoreDirectory::Entry* en =
				parent.FindEntryByID(ObjectID);
			if(!en)
			{
				BOX_ERROR("Missing entry for directory " <<
					BOX_FORMAT_OBJECTID(ObjectID) <<
					" in directory " <<
					BOX_FORMAT_OBJECTID(ContainerID) <<
					" while trying to update dir size in parent");
			}
			else
			{
				ASSERT(en->GetSizeInBlocks() == old_dir_size);
				en->SetSizeInBlocks(new_dir_size);
				SaveDirectory(parent);
			}
		}
	}
	catch(...)
	{
		// Remove it from the cache if anything went wrong
		RemoveDirectoryFromCache(ObjectID);
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::AddDirectory(int64_t,
//			 const BackupStoreFilename &, bool &)
//		Purpose: Creates a directory (or just returns the ID of an
//			 existing one). rAlreadyExists set appropraitely.
//		Created: 2003/09/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreContext::AddDirectory(int64_t InDirectory,
	const BackupStoreFilename &rFilename,
	const StreamableMemBlock &Attributes,
	int64_t AttributesModTime,
	int64_t ModificationTime,
	bool &rAlreadyExists)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	// Flags as not already existing
	rAlreadyExists = false;

	// Get the directory we want to modify
	BackupStoreDirectory &dir(GetDirectoryInternal(InDirectory));

	// Scan the directory for the name (only looking for directories which already exist)
	{
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next(BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING,
			BackupStoreDirectory::Entry::Flags_Deleted | BackupStoreDirectory::Entry::Flags_OldVersion)) != 0)	// Ignore deleted and old directories
		{
			if(en->GetName() == rFilename)
			{
				// Already exists
				rAlreadyExists = true;
				return en->GetObjectID();
			}
		}
	}

	// Allocate the next ID
	int64_t id = AllocateObjectID();

	// Create an empty directory with the given attributes on disc
	int64_t dirSize;

	{
		BackupStoreDirectory emptyDir(id, InDirectory);
		// add the atttribues
		emptyDir.SetAttributes(Attributes, AttributesModTime);

		// Write...
		mapFileSystem->PutDirectory(emptyDir);
		dirSize = emptyDir.GetUserInfo1_SizeInBlocks();

		// Exceeds the hard limit?
		int64_t newTotalBlocksUsed = mapStoreInfo->GetBlocksUsed() +
			dirSize;
		if(newTotalBlocksUsed > mapStoreInfo->GetBlocksHardLimit())
		{
			mapFileSystem->DeleteDirectory(id);
			THROW_EXCEPTION(BackupStoreException, AddedFileExceedsStorageLimit)
		}

		// Make sure the size of the directory is added to the usage counts in the info
		ASSERT(dirSize > 0);
		mapStoreInfo->ChangeBlocksUsed(dirSize);
		mapStoreInfo->ChangeBlocksInDirectories(dirSize);
		// Not added to cache, so don't set the size in the directory
	}

	// Then add it into the parent directory
	try
	{
		dir.AddEntry(rFilename, ModificationTime, id, dirSize,
			BackupStoreDirectory::Entry::Flags_Dir,
			0 /* attributes hash */);
		SaveDirectory(dir);

		// Increment reference count on the new directory to one
		mapRefCount->AddReference(id);
	}
	catch(...)
	{
		// Back out on adding that directory
		mapFileSystem->DeleteDirectory(id);

		// Remove this entry from the cache
		RemoveDirectoryFromCache(InDirectory);

		// Don't worry about the incremented number in the store info
		throw;
	}

	// Save the store info (may not be postponed)
	mapStoreInfo->AdjustNumDirectories(1);
	SaveStoreInfo(false);

	// tell caller what the ID was
	return id;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::DeleteFile(const BackupStoreFilename &, int64_t, int64_t &, bool)
//		Purpose: Recusively deletes a directory (or undeletes if Undelete = true)
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
void BackupStoreContext::DeleteDirectory(int64_t ObjectID, bool Undelete)
{
	CHECK_FILESYSTEM_INITIALISED();

	// Essential checks!
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	// Containing directory
	int64_t InDirectory = 0;

	try
	{
		// Get the directory that's to be deleted
		{
			// In block, because dir may not be valid after the delete directory call
			BackupStoreDirectory &dir(GetDirectoryInternal(ObjectID));

			// Store the directory it's in for later
			InDirectory = dir.GetContainerID();
		
			// Depth first delete of contents
			DeleteDirectoryRecurse(ObjectID, Undelete);
		}

		// Remove the entry from the directory it's in
		ASSERT(InDirectory != 0);
		BackupStoreDirectory &parentDir(GetDirectoryInternal(InDirectory));

		BackupStoreDirectory::Iterator i(parentDir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next(Undelete?(BackupStoreDirectory::Entry::Flags_Deleted):(BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING),
			Undelete?(0):(BackupStoreDirectory::Entry::Flags_Deleted))) != 0)	// Ignore deleted directories (or not deleted if Undelete)
		{
			if(en->GetObjectID() == ObjectID)
			{
				// This is the one to delete
				if(Undelete)
				{
					en->RemoveFlags(BackupStoreDirectory::Entry::Flags_Deleted);
				}
				else
				{
					en->AddFlags(BackupStoreDirectory::Entry::Flags_Deleted);
				}

				// Save it
				SaveDirectory(parentDir);

				// Done
				break;
			}
		}

		// Update blocks deleted count
		SaveStoreInfo(false);
	}
	catch(...)
	{
		RemoveDirectoryFromCache(InDirectory);
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::DeleteDirectoryRecurse(BackupStoreDirectory &, int64_t)
//		Purpose: Private. Deletes a directory depth-first recusively.
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
void BackupStoreContext::DeleteDirectoryRecurse(int64_t ObjectID, bool Undelete)
{
	CHECK_FILESYSTEM_INITIALISED();

	try
	{
		// Does things carefully to avoid using a directory in the cache after recursive call
		// because it may have been deleted.

		// Do sub directories
		{
			// Get the directory...
			BackupStoreDirectory &dir(GetDirectoryInternal(ObjectID));

			// Then scan it for directories
			std::vector<int64_t> subDirs;
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;
			if(Undelete)
			{
				while((en = i.Next(BackupStoreDirectory::Entry::Flags_Dir | BackupStoreDirectory::Entry::Flags_Deleted,	// deleted dirs
					BackupStoreDirectory::Entry::Flags_EXCLUDE_NOTHING)) != 0)
				{
					// Store the directory ID.
					subDirs.push_back(en->GetObjectID());
				}
			}
			else
			{
				while((en = i.Next(BackupStoreDirectory::Entry::Flags_Dir,	// dirs only
					BackupStoreDirectory::Entry::Flags_Deleted)) != 0)		// but not deleted ones
				{
					// Store the directory ID.
					subDirs.push_back(en->GetObjectID());
				}
			}

			// Done with the directory for now. Recurse to sub directories
			for(std::vector<int64_t>::const_iterator i = subDirs.begin(); i != subDirs.end(); ++i)
			{
				DeleteDirectoryRecurse(*i, Undelete);
			}
		}

		// Then, delete the files. Will need to load the directory again because it might have
		// been removed from the cache.
		{
			// Get the directory...
			BackupStoreDirectory &dir(GetDirectoryInternal(ObjectID));

			// Changes made?
			bool changesMade = false;

			// Run through files
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;

			while((en = i.Next(Undelete?(BackupStoreDirectory::Entry::Flags_Deleted):(BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING),
				Undelete?(0):(BackupStoreDirectory::Entry::Flags_Deleted))) != 0)	// Ignore deleted directories (or not deleted if Undelete)
			{
				// Keep count of the deleted blocks
				if(en->IsFile())
				{
					int64_t size = en->GetSizeInBlocks();
					ASSERT(en->IsDeleted() == Undelete); 
					// Don't adjust counters for old files,
					// because it can be both old and deleted.
					if(!en->IsOld())
					{
						mapStoreInfo->ChangeBlocksInCurrentFiles(Undelete ? size : -size);
						mapStoreInfo->AdjustNumCurrentFiles(Undelete ? 1 : -1);
					}
					mapStoreInfo->ChangeBlocksInDeletedFiles(Undelete ? -size : size);
					mapStoreInfo->AdjustNumDeletedFiles(Undelete ? -1 : 1);
				}

				// Add/remove the deleted flags
				if(Undelete)
				{
					en->RemoveFlags(BackupStoreDirectory::Entry::Flags_Deleted);
				}
				else
				{
					en->AddFlags(BackupStoreDirectory::Entry::Flags_Deleted);
				}

				// Did something
				changesMade = true;
			}

			// Save the directory
			if(changesMade)
			{
				SaveDirectory(dir);
			}
		}
	}
	catch(...)
	{
		RemoveDirectoryFromCache(ObjectID);
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::ChangeDirAttributes(int64_t, const StreamableMemBlock &, int64_t)
//		Purpose: Change the attributes of a directory
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
void BackupStoreContext::ChangeDirAttributes(int64_t Directory, const StreamableMemBlock &Attributes, int64_t AttributesModTime)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	try
	{
		// Get the directory we want to modify
		BackupStoreDirectory &dir(GetDirectoryInternal(Directory));

		// Set attributes
		dir.SetAttributes(Attributes, AttributesModTime);

		// Save back
		SaveDirectory(dir);
	}
	catch(...)
	{
		RemoveDirectoryFromCache(Directory);
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::ChangeFileAttributes(int64_t, int64_t, const StreamableMemBlock &, int64_t)
//		Purpose: Sets the attributes on a directory entry. Returns true if the object existed, false if it didn't.
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
bool BackupStoreContext::ChangeFileAttributes(const BackupStoreFilename &rFilename, int64_t InDirectory, const StreamableMemBlock &Attributes, int64_t AttributesHash, int64_t &rObjectIDOut)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}
	
	try
	{
		// Get the directory we want to modify
		BackupStoreDirectory &dir(GetDirectoryInternal(InDirectory));

		// Find the file entry
		BackupStoreDirectory::Entry *en = 0;
		// Iterate through current versions of files, only
		BackupStoreDirectory::Iterator i(dir);
		while((en = i.Next(
			BackupStoreDirectory::Entry::Flags_File,
			BackupStoreDirectory::Entry::Flags_Deleted | BackupStoreDirectory::Entry::Flags_OldVersion)
			) != 0)
		{
			if(en->GetName() == rFilename)
			{
				// Set attributes
				en->SetAttributes(Attributes, AttributesHash);

				// Tell caller the object ID
				rObjectIDOut = en->GetObjectID();

				// Done
				break;
			}
		}
		if(en == 0)
		{
			// Didn't find it
			return false;
		}

		// Save back
		SaveDirectory(dir);
	}
	catch(...)
	{
		RemoveDirectoryFromCache(InDirectory);
		throw;
	}

	// Changed, everything OK
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::ObjectExists(int64_t)
//		Purpose: Test to see if an object of this ID exists in the store
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
bool BackupStoreContext::ObjectExists(int64_t ObjectID, int MustBe)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	// Note that we need to allow object IDs a little bit greater than the last one in the store info,
	// because the store info may not have got saved in an error condition. Max greater ID is
	// STORE_INFO_SAVE_DELAY in this case, *2 to be safe.
	if(ObjectID <= 0 || ObjectID > (mapStoreInfo->GetLastObjectIDUsed() + (STORE_INFO_SAVE_DELAY * 2)))
	{
		// Obviously bad object ID
		return false;
	}

	// We don't try to check this any more.
	if (!mapFileSystem->ObjectExists(ObjectID))
	{
		return false;
	}

	// Do we need to be more specific?
	if(MustBe != ObjectExists_Anything)
	{
		// Open the file. TODO FIXME: don't download the entire file from S3
		// to read the first four bytes.
		std::auto_ptr<IOStream> objectFile =
			mapFileSystem->GetFile(ObjectID);

		// Read the first integer
		u_int32_t magic;
		if(!objectFile->ReadFullBuffer(&magic, sizeof(magic), 0 /* not interested in how many read if failure */))
		{
			// Failed to get any bytes, must have failed
			return false;
		}

#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		if(MustBe == ObjectExists_File && ntohl(magic) == OBJECTMAGIC_FILE_MAGIC_VALUE_V0)
		{
			// Old version detected
			return true;
		}
#endif

		// Right one?
		u_int32_t requiredMagic = (MustBe == ObjectExists_File)?OBJECTMAGIC_FILE_MAGIC_VALUE_V1:OBJECTMAGIC_DIR_MAGIC_VALUE;

		// Check
		if(ntohl(magic) != requiredMagic)
		{
			return false;
		}

		// File is implicitly closed
	}

	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::OpenObject(int64_t)
//		Purpose: Opens an object
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupStoreContext::OpenObject(int64_t ObjectID)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	// Attempt to open the file
	try
	{
		return mapFileSystem->GetFile(ObjectID);
	}
	catch(BackupStoreException &e)
	{
		if(e.GetSubType() == BackupStoreException::ObjectDoesNotExist)
		{
			// Try again as a directory (unlikely).
			BackupStoreDirectory dir;
			mapFileSystem->GetDirectory(ObjectID, dir);
			CollectInBufferStream* pStream = new CollectInBufferStream();
			std::auto_ptr<IOStream> apStream(pStream);
			dir.WriteToStream(*pStream);
			pStream->SetForReading();
			return apStream;
		}
		else
		{
			throw;
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::GetFile()
//		Purpose: Retrieve a file from the store
//		Created: 2015/08/10
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupStoreContext::GetFile(int64_t ObjectID, int64_t InDirectory)
{
	CHECK_FILESYSTEM_INITIALISED();

	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	// Get the directory it's in
	const BackupStoreDirectory &rdir(GetDirectory(InDirectory));

	// Find the object within the directory
	BackupStoreDirectory::Entry *pfileEntry = rdir.FindEntryByID(ObjectID);
	if(pfileEntry == 0)
	{
		THROW_EXCEPTION(BackupStoreException, CouldNotFindEntryInDirectory);
	}

	// The result
	std::auto_ptr<IOStream> stream;

	// Does this depend on anything?
	if(pfileEntry->GetDependsNewer() != 0)
	{
		// File exists, but is a patch from a new version. Generate the older version.
		std::vector<int64_t> patchChain;
		int64_t id = ObjectID;
		BackupStoreDirectory::Entry *en = 0;
		do
		{
			patchChain.push_back(id);
			en = rdir.FindEntryByID(id);
			if(en == 0)
			{
				THROW_EXCEPTION_MESSAGE(BackupStoreException,
					PatchChainInfoBadInDirectory,
					"Object " <<
					BOX_FORMAT_OBJECTID(ObjectID) <<
					" in dir " <<
					BOX_FORMAT_OBJECTID(InDirectory) <<
					" for account " <<
					BOX_FORMAT_ACCOUNT(mClientID) <<
					" references object " <<
					BOX_FORMAT_OBJECTID(id) <<
					" which does not exist in dir");
			}
			id = en->GetDependsNewer();
		}
		while(en != 0 && id != 0);

		stream = mapFileSystem->GetFilePatch(ObjectID, patchChain);
	}
	else
	{
		// Simple case: file already exists on disc ready to go

		// Open the object
		std::auto_ptr<IOStream> object(OpenObject(ObjectID));
		BufferedStream buf(*object);

		// Verify it
		if(!BackupStoreFile::VerifyEncodedFileFormat(buf))
		{
			THROW_EXCEPTION(BackupStoreException, AddedFileDoesNotVerify);
		}

		// Reset stream -- seek to beginning
		object->Seek(0, IOStream::SeekType_Absolute);

		// Reorder the stream/file into stream order
		{
			// Write nastily to allow this to work with gcc 2.x
			std::auto_ptr<IOStream> t(BackupStoreFile::ReorderFileToStreamOrder(object.get(), true /* take ownership */));
			stream = t;
		}

		// Object will be deleted when the stream is deleted,
		// so can release the object auto_ptr here to avoid
		// premature deletion
		object.release();
	}

	return stream;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::GetClientStoreMarker()
//		Purpose: Retrieve the client store marker
//		Created: 2003/10/29
//
// --------------------------------------------------------------------------
int64_t BackupStoreContext::GetClientStoreMarker()
{
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	return mapStoreInfo->GetClientStoreMarker();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::GetStoreDiscUsageInfo(int64_t &, int64_t &, int64_t &)
//		Purpose: Get disc usage info from store info
//		Created: 1/1/04
//
// --------------------------------------------------------------------------
void BackupStoreContext::GetStoreDiscUsageInfo(int64_t &rBlocksUsed, int64_t &rBlocksSoftLimit, int64_t &rBlocksHardLimit)
{
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	rBlocksUsed = mapStoreInfo->GetBlocksUsed();
	rBlocksSoftLimit = mapStoreInfo->GetBlocksSoftLimit();
	rBlocksHardLimit = mapStoreInfo->GetBlocksHardLimit();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::HardLimitExceeded()
//		Purpose: Returns true if the hard limit has been exceeded
//		Created: 1/1/04
//
// --------------------------------------------------------------------------
bool BackupStoreContext::HardLimitExceeded()
{
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	return mapStoreInfo->GetBlocksUsed() > mapStoreInfo->GetBlocksHardLimit();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::SetClientStoreMarker(int64_t)
//		Purpose: Sets the client store marker, and commits it to disc
//		Created: 2003/10/29
//
// --------------------------------------------------------------------------
void BackupStoreContext::SetClientStoreMarker(int64_t ClientStoreMarker)
{
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	mapStoreInfo->SetClientStoreMarker(ClientStoreMarker);
	SaveStoreInfo(false /* don't delay saving this */);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::MoveObject(int64_t, int64_t, int64_t, const BackupStoreFilename &, bool)
//		Purpose: Move an object (and all objects with the same name) from one directory to another
//		Created: 12/11/03
//
// --------------------------------------------------------------------------
void BackupStoreContext::MoveObject(int64_t ObjectID, int64_t MoveFromDirectory,
	int64_t MoveToDirectory, const BackupStoreFilename &rNewFilename,
	bool MoveAllWithSameName, bool AllowMoveOverDeletedObject)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, ContextIsReadOnly)
	}

	// Should deleted files be excluded when checking for the existance of objects with the target name?
	int64_t targetSearchExcludeFlags = (AllowMoveOverDeletedObject)
		?(BackupStoreDirectory::Entry::Flags_Deleted)
		:(BackupStoreDirectory::Entry::Flags_EXCLUDE_NOTHING);

	// Special case if the directories are the same...
	if(MoveFromDirectory == MoveToDirectory)
	{
		try
		{
			// Get the first directory
			BackupStoreDirectory &dir(GetDirectoryInternal(MoveFromDirectory));

			// Find the file entry
			BackupStoreDirectory::Entry *en = dir.FindEntryByID(ObjectID);

			// Error if not found
			if(en == 0)
			{
				THROW_EXCEPTION(BackupStoreException, CouldNotFindEntryInDirectory)
			}

			// Check the new name doens't already exist (optionally ignoring deleted files)
			{
				BackupStoreDirectory::Iterator i(dir);
				BackupStoreDirectory::Entry *c = 0;
				while((c = i.Next(BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING, targetSearchExcludeFlags)) != 0)
				{
					if(c->GetName() == rNewFilename)
					{
						THROW_EXCEPTION(BackupStoreException, NameAlreadyExistsInDirectory)
					}
				}
			}

			// Need to get all the entries with the same name?
			if(MoveAllWithSameName)
			{
				// Iterate through the directory, copying all with matching names
				BackupStoreDirectory::Iterator i(dir);
				BackupStoreDirectory::Entry *c = 0;
				while((c = i.Next()) != 0)
				{
					if(c->GetName() == en->GetName())
					{
						// Rename this one
						c->SetName(rNewFilename);
					}
				}
			}
			else
			{
				// Just copy this one
				en->SetName(rNewFilename);
			}

			// Save the directory back
			SaveDirectory(dir);
		}
		catch(...)
		{
			RemoveDirectoryFromCache(MoveToDirectory); // either will do, as they're the same
			throw;
		}

		return;
	}

	// Got to be careful how this is written, as we can't guarantee that
	// if we have two directories open, the first won't be deleted as the
	// second is opened. (cache)

	// List of entries to move
	std::vector<BackupStoreDirectory::Entry *> moving;

	// list of directory IDs which need to have containing dir id changed
	std::vector<int64_t> dirsToChangeContainingID;

	try
	{
		// First of all, get copies of the entries to move to the to directory.

		{
			// Get the first directory
			BackupStoreDirectory &from(GetDirectoryInternal(MoveFromDirectory));

			// Find the file entry
			BackupStoreDirectory::Entry *en = from.FindEntryByID(ObjectID);

			// Error if not found
			if(en == 0)
			{
				THROW_EXCEPTION(BackupStoreException, CouldNotFindEntryInDirectory)
			}

			// Need to get all the entries with the same name?
			if(MoveAllWithSameName)
			{
				// Iterate through the directory, copying all with matching names
				BackupStoreDirectory::Iterator i(from);
				BackupStoreDirectory::Entry *c = 0;
				while((c = i.Next()) != 0)
				{
					if(c->GetName() == en->GetName())
					{
						// Copy
						moving.push_back(new BackupStoreDirectory::Entry(*c));

						// Check for containing directory correction
						if(c->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir) dirsToChangeContainingID.push_back(c->GetObjectID());
					}
				}
				ASSERT(!moving.empty());
			}
			else
			{
				// Just copy this one
				moving.push_back(new BackupStoreDirectory::Entry(*en));

				// Check for containing directory correction
				if(en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir) dirsToChangeContainingID.push_back(en->GetObjectID());
			}
		}

		// Secondly, insert them into the to directory, and save it

		{
			// To directory
			BackupStoreDirectory &to(GetDirectoryInternal(MoveToDirectory));

			// Check the new name doens't already exist
			{
				BackupStoreDirectory::Iterator i(to);
				BackupStoreDirectory::Entry *c = 0;
				while((c = i.Next(BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING, targetSearchExcludeFlags)) != 0)
				{
					if(c->GetName() == rNewFilename)
					{
						THROW_EXCEPTION(BackupStoreException, NameAlreadyExistsInDirectory)
					}
				}
			}

			// Copy the entries into it, changing the name as we go
			for(std::vector<BackupStoreDirectory::Entry *>::iterator i(moving.begin()); i != moving.end(); ++i)
			{
				BackupStoreDirectory::Entry *en = (*i);
				en->SetName(rNewFilename);
				to.AddEntry(*en);	// adds copy
			}

			// Save back
			SaveDirectory(to);
		}

		// Thirdly... remove them from the first directory -- but if it fails, attempt to delete them from the to directory
		try
		{
			// Get directory
			BackupStoreDirectory &from(GetDirectoryInternal(MoveFromDirectory));

			// Delete each one
			for(std::vector<BackupStoreDirectory::Entry *>::iterator i(moving.begin()); i != moving.end(); ++i)
			{
				from.DeleteEntry((*i)->GetObjectID());
			}

			// Save back
			SaveDirectory(from);
		}
		catch(...)
		{
			// UNDO modification to To directory

			// Get directory
			BackupStoreDirectory &to(GetDirectoryInternal(MoveToDirectory));

			// Delete each one
			for(std::vector<BackupStoreDirectory::Entry *>::iterator i(moving.begin()); i != moving.end(); ++i)
			{
				to.DeleteEntry((*i)->GetObjectID());
			}

			// Save back
			SaveDirectory(to);

			// Throw the error
			throw;
		}

		// Finally... for all the directories we moved, modify their containing directory ID
		for(std::vector<int64_t>::iterator i(dirsToChangeContainingID.begin()); i != dirsToChangeContainingID.end(); ++i)
		{
			// Load the directory
			BackupStoreDirectory &change(GetDirectoryInternal(*i));

			// Modify containing dir ID
			change.SetContainerID(MoveToDirectory);

			// Save it back
			SaveDirectory(change);
		}
	}
	catch(...)
	{
		// Make sure directories aren't in the cache, as they may have been modified
		RemoveDirectoryFromCache(MoveToDirectory);
		RemoveDirectoryFromCache(MoveFromDirectory);
		for(std::vector<int64_t>::iterator i(dirsToChangeContainingID.begin()); i != dirsToChangeContainingID.end(); ++i)
		{
			RemoveDirectoryFromCache(*i);
		}

		while(!moving.empty())
		{
			delete moving.back();
			moving.pop_back();
		}
		throw;
	}

	// Clean up
	while(!moving.empty())
	{
		delete moving.back();
		moving.pop_back();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreContext::GetBackupStoreInfo()
//		Purpose: Return the backup store info object, exception if it isn't loaded
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
const BackupStoreInfo &BackupStoreContext::GetBackupStoreInfo() const
{
	if(mapStoreInfo.get() == 0)
	{
		THROW_EXCEPTION(BackupStoreException, StoreInfoNotLoaded)
	}

	return *(mapStoreInfo.get());
}

