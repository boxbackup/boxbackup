// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreCheck.cpp
//		Purpose: Check a store for consistency
//		Created: 21/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#ifdef WIN32
#	include <io.h> // for _mktemp_s
#endif

#include "autogen_BackupStoreException.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreRefCountDatabase.h"
#include "RaidFileController.h"
#include "RaidFileException.h"
#include "RaidFileRead.h"
#include "RaidFileUtil.h"
#include "RaidFileWrite.h"
#include "StoreStructure.h"
#include "Utils.h" // for ObjectExists_* (object_exists_t)

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::BackupStoreCheck(const std::string &,
//		         int, int32_t, bool, bool)
//		Purpose: Constructor
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
BackupStoreCheck::BackupStoreCheck(BackupFileSystem& FileSystem, bool FixErrors, bool Quiet)
: mAccountID(FileSystem.GetAccountID()), // will be 0 for S3BackupFileSystem
  mFixErrors(FixErrors),
  mQuiet(Quiet),
  mNumberErrorsFound(0),
  mLastIDInInfo(0),
  mpInfoLastBlock(0),
  mInfoLastBlockEntries(0),
  mrFileSystem(FileSystem),
  mLostDirNameSerial(0),
  mLostAndFoundDirectoryID(0),
  mBlocksUsed(0),
  mBlocksInCurrentFiles(0),
  mBlocksInOldFiles(0),
  mBlocksInDeletedFiles(0),
  mBlocksInDirectories(0),
  mNumCurrentFiles(0),
  mNumOldFiles(0),
  mNumDeletedFiles(0),
  mNumDirectories(0),
  mTimeout(600) // default timeout is 10 minutes
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::~BackupStoreCheck()
//		Purpose: Destructor
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
BackupStoreCheck::~BackupStoreCheck()
{
	// Clean up
	FreeInfo();

	// Avoid an exception if we forget to discard mapNewRefs
	if (mpNewRefs)
	{
		// Discard() can throw exception, but destructors aren't supposed to do that, so
		// just catch and log them.
		try
		{
			mpNewRefs->Discard();
		}
		catch(BoxException &e)
		{
			BOX_ERROR("Error while destroying BackupStoreCheck: discarding "
				"the refcount database threw an exception: " << e.what());
		}

		mpNewRefs = NULL;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::Check()
//		Purpose: Perform the check on the given account. You need to
//			 hold a lock on the account before calling this!
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::Check()
{
	if(mFixErrors)
	{
		// Will throw an exception if it doesn't manage to get a lock:
		mrFileSystem.TryGetLock();
	}

	if(!mQuiet && mFixErrors)
	{
		BOX_INFO("Will fix errors encountered during checking.");
	}

	// If we are read-only, then we should not call GetPotentialRefCountDatabase because
	// that does actually change the store: the temporary file would conflict with any other
	// process which wants to do the same thing at the same time (e.g. housekeeping), and if
	// neither process locks the store, they will break each other. We can still create a
	// refcount DB in a temporary directory, and Commit() will not really commit it in that
	// case (it will rename it, but still in the temporary directory).
	if(mFixErrors)
	{
		mpNewRefs = &mrFileSystem.GetPotentialRefCountDatabase();
	}
	else
	{
		std::string temp_file = GetTempDirPath() + "boxbackup_refcount_db_XXXXXX";
		char temp_file_buf[PATH_MAX];
		strncpy(temp_file_buf, temp_file.c_str(), sizeof(temp_file_buf));
#ifdef WIN32
		if(_mktemp_s(temp_file_buf, sizeof(temp_file_buf)) != 0)
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException, FailedToCreateTemporaryFile,
				"Failed to get a temporary file name based on " << temp_file);
		}
#else
		int fd = mkstemp(temp_file_buf);
		if(fd == -1)
		{
			THROW_SYS_FILE_ERROR("Failed to get a temporary file name based on",
				temp_file, BackupStoreException, FailedToCreateTemporaryFile);
		}
		close(fd);
#endif

		BOX_TRACE("Creating temporary refcount DB in a temporary file: " << temp_file_buf);
		mapOwnNewRefs = BackupStoreRefCountDatabase::Create(temp_file_buf,
			mrFileSystem.GetAccountID(), true); // reuse_existing_file
		mpNewRefs = mapOwnNewRefs.get();
	}

	// Phase 1, check objects
	if(!mQuiet)
	{
		BOX_INFO("Checking account " << mrFileSystem.GetAccountIdentifier() <<
			"...");
		BOX_INFO("Phase 1, check objects...");
	}
	CheckObjects();

	// Phase 2, check directories
	if(!mQuiet)
	{
		BOX_INFO("Phase 2, check directories...");
	}
	CheckDirectories();

	// Phase 3, check root
	if(!mQuiet)
	{
		BOX_INFO("Phase 3, check root...");
	}
	CheckRoot();

	// Phase 4, check unattached objects
	if(!mQuiet)
	{
		BOX_INFO("Phase 4, fix unattached objects...");
	}
	CheckUnattachedObjects();

	// Phase 5, fix bad info
	if(!mQuiet)
	{
		BOX_INFO("Phase 5, fix unrecovered inconsistencies...");
	}
	FixDirsWithWrongContainerID();
	FixDirsWithLostDirs();

	// Phase 6, regenerate store info
	if(!mQuiet)
	{
		BOX_INFO("Phase 6, regenerate store info...");
	}
	WriteNewStoreInfo();

	try
	{
		// We should be able to load a reference to the old refcount database
		// (read-only) at the same time that we have a reference to the new one
		// (temporary) open but not yet committed.
		BackupStoreRefCountDatabase& old_refs(
			mrFileSystem.GetPermanentRefCountDatabase(true)); // ReadOnly
		mNumberErrorsFound += mpNewRefs->ReportChangesTo(old_refs);
	}
	catch(BoxException &e)
	{
		BOX_WARNING("Old reference count database was missing or corrupted, "
			"cannot check it for errors.");
		mNumberErrorsFound++;
	}

	// force file to be saved and closed before releasing the lock below
	if(mFixErrors)
	{
		mpNewRefs->Commit();
	}
	else
	{
		mpNewRefs->Discard();
		mapOwnNewRefs.reset();
	}
	mpNewRefs = NULL;

	if(mNumberErrorsFound > 0)
	{
		BOX_WARNING("Finished checking account " <<
			mrFileSystem.GetAccountIdentifier() << ": " <<
			mNumberErrorsFound << " errors found");

		if(!mFixErrors)
		{
			BOX_WARNING("No changes to the store account "
				"have been made.");
		}

		if(!mFixErrors && mNumberErrorsFound > 0)
		{
			BOX_WARNING("Run again with fix option to "
				"fix these errors");
		}

		if(mFixErrors && mNumberErrorsFound > 0)
		{
			BOX_WARNING("You should now use bbackupquery "
				"on the client machine to examine the store.");
			if(mLostAndFoundDirectoryID != 0)
			{
				BOX_WARNING("A lost+found directory was "
					"created in the account root.\n"
					"This contains files and directories "
					"which could not be matched to "
					"existing directories.\n"\
					"bbackupd will delete this directory "
					"in a few days time.");
			}
		}
	}
	else
	{
		BOX_NOTICE("Finished checking account " <<
			mrFileSystem.GetAccountIdentifier() << ": " <<
			"no errors found");
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckObjects()
//		Purpose: Read in the contents of the directory, recurse to other levels,
//				 checking objects for sanity and readability
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckObjects()
{
	// Scan all available files to identify the largest object ID that we know
	// about, worked out by looking at disc contents, not trusting anything.
	BackupFileSystem::CheckObjectsResult check =
		mrFileSystem.CheckObjects(mFixErrors);

	mNumberErrorsFound += check.numErrorsFound;

	// Then try to open each object, for initial consistency checks.
	for(int64_t ObjectID = 1; ObjectID <= check.maxObjectIDFound; ObjectID++)
	{
		if(CheckAndAddObject(ObjectID) == ObjectExists_Unknown)
		{
			// File was bad, delete it
			BOX_ERROR("Object " << BOX_FORMAT_OBJECTID(ObjectID) << " "
				"is invalid" << (mFixErrors?", deleting":""));
			++mNumberErrorsFound;
			if(mFixErrors)
			{
				mrFileSystem.DeleteObjectUnknown(ObjectID);
			}
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckAndAddObject(int64_t,
//			 const std::string &)
//		Purpose: Check a specific object and add it to the list
//			 if it's OK. If there are any errors with the
//			 reading, return false and it'll be deleted.
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
object_exists_t BackupStoreCheck::CheckAndAddObject(int64_t ObjectID)
{
	// Info on object...
	bool isFile = true, isDirectory = false;
	int64_t containerID = -1;

	std::auto_ptr<IOStream> file = mrFileSystem.GetObject(ObjectID, false); // !required
	if(!file.get())
	{
		// This file just doesn't exist. Saves the caller from calling
		// ObjectExists() before calling us, which is expensive on S3.
		return ObjectExists_NoObject;
	}

	try
	{
		// Read in first four bytes -- don't have to worry about
		// retrying if not all bytes read as is RaidFile
		uint32_t signature;
		int bytes_read;
		if(!file->ReadFullBuffer(&signature, sizeof(signature),
			0, // don't care about number of bytes actually read
			mTimeout))
		{
			// Too short, can't read signature from it
			BOX_ERROR("Object " << BOX_FORMAT_OBJECTID(ObjectID) << " is "
				"too small to have a valid header");
			return ObjectExists_Unknown;
		}

		// Seek back to beginning
		file->Seek(0, IOStream::SeekType_Absolute);

		// Then... check depending on the type
		switch(ntohl(signature))
		{
		case OBJECTMAGIC_FILE_MAGIC_VALUE_V1:
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		case OBJECTMAGIC_FILE_MAGIC_VALUE_V0:
#endif
			// Check it as a file.
			containerID = CheckFile(ObjectID, *file);
			break;

		case OBJECTMAGIC_DIR_MAGIC_VALUE:
			// Check it as a directory.
			isFile = false;
			isDirectory = true;
			containerID = CheckDirInitial(ObjectID, *file);
			break;

		default:
			// Unknown signature. Bad file. Very bad file.
			return ObjectExists_Unknown;
			break;
		}
	}
	catch(std::exception &e)
	{
		// Error caught, not a good file then, let it be deleted
		BOX_ERROR("Object " << BOX_FORMAT_OBJECTID(ObjectID) << " failed initial "
			"validation: " << e.what());
		return ObjectExists_Unknown;
	}

	ASSERT((isFile || isDirectory) && !(isFile && isDirectory));

	// Got a container ID? (ie check was successful)
	if(containerID == -1)
	{
		return ObjectExists_Unknown;
	}

	// Add to list of IDs known about
	int64_t size = mrFileSystem.GetFileSizeInBlocks(ObjectID);
	AddID(ObjectID, containerID, size, isFile);

	// Add to usage counts
	mBlocksUsed += size;
	if(!isFile)
	{
		mBlocksInDirectories += size;
	}

	// If it looks like a good object, and it's non-RAID, and
	// this is a RAID set, then convert it to RAID.
	mrFileSystem.EnsureObjectIsPermanent(ObjectID, mFixErrors);

	// Report success
	return isFile ? ObjectExists_File : ObjectExists_Dir;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckFile(int64_t, IOStream &)
//		Purpose: Do check on file, return original container ID
//			 if OK, or -1 on error
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::CheckFile(int64_t ObjectID, IOStream &rStream)
{
	// Check that it's not the root directory ID. Having a file as
	// the root directory would be bad.
	if(ObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		// Get that dodgy thing deleted!
		BOX_ERROR("Have file as root directory. This is bad.");
		return -1;
	}

	// Check the format of the file, and obtain the container ID
	int64_t originalContainerID = -1;
	if(!BackupStoreFile::VerifyEncodedFileFormat(rStream,
		0 /* don't want diffing from ID */,
		&originalContainerID))
	{
		BOX_ERROR("Object " << BOX_FORMAT_OBJECTID(ObjectID) << " does not "
			"verify as a file");
		return -1;
	}

	return originalContainerID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckDirInitial(int64_t, IOStream &)
//		Purpose: Do initial check on directory, return container ID
//			 if OK, or -1 on error
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::CheckDirInitial(int64_t ObjectID, IOStream &rStream)
{
	// Simply attempt to read in the directory
	BackupStoreDirectory dir;
	dir.ReadFromStream(rStream, IOStream::TimeOutInfinite);

	// Check object ID
	if(dir.GetObjectID() != ObjectID)
	{
		BOX_ERROR("Directory " << BOX_FORMAT_OBJECTID(ObjectID) << " has a "
			"different internal object ID than expected: " <<
			BOX_FORMAT_OBJECTID(dir.GetObjectID()));
		return -1;
	}

	// Return container ID
	return dir.GetContainerID();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckDirectories()
//		Purpose: Check the directories
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckDirectories()
{
	// Phase 1 did this:
	//    Checked that all the directories are readable
	//    Built a list of all directories and files which exist on the store
	//
	// This phase will check all the files in the directories, make
	// a note of all directories which are missing, and do initial fixing.

	// The root directory is not contained inside another directory, so
	// it has no directory entry to scan, but we have to count it
	// somewhere, so we'll count it here.
	mNumDirectories++;

	// Scan all objects.
	for(Info_t::const_iterator i(mInfo.begin()); i != mInfo.end(); ++i)
	{
		IDBlock *pblock = i->second;
		int32_t bentries = (pblock == mpInfoLastBlock)?mInfoLastBlockEntries:BACKUPSTORECHECK_BLOCK_SIZE;

		for(int e = 0; e < bentries; ++e)
		{
			uint8_t flags = GetFlags(pblock, e);
			if(flags & Flags_IsDir)
			{
				// Found a directory. Read it in.
				BackupStoreDirectory dir;
				mrFileSystem.GetDirectory(pblock->mID[e], dir);

				// Flag for modifications
				bool isModified = CheckDirectory(dir);

				// Check the directory again, now that entries have been removed
				if(dir.CheckAndFix())
				{
					// Wasn't quite right, and has been modified
					BOX_ERROR("Directory ID " <<
						BOX_FORMAT_OBJECTID(pblock->mID[e]) <<
						" was still bad after all checks");
					++mNumberErrorsFound;
					isModified = true;
				}
				else if(isModified)
				{
					BOX_INFO("Directory ID " <<
						BOX_FORMAT_OBJECTID(pblock->mID[e]) <<
						" was OK after fixing");
				}

				if(isModified && mFixErrors)
				{
					BOX_WARNING("Writing modified directory back to "
						"storage: " <<
						BOX_FORMAT_OBJECTID(pblock->mID[e]));
					mrFileSystem.PutDirectory(dir);
				}

				CountDirectoryEntries(dir);
			}
		}
	}
}

bool BackupStoreCheck::CheckDirectory(BackupStoreDirectory& dir)
{
	bool restart = true;
	bool isModified = false;

	while(restart)
	{
		std::vector<int64_t> toDelete;
		restart = false;

		// Check for validity
		if(dir.CheckAndFix())
		{
			// Wasn't quite right, and has been modified
			BOX_ERROR("Directory ID " <<
				BOX_FORMAT_OBJECTID(dir.GetObjectID()) <<
				" had invalid entries" <<
				(mFixErrors ? ", fixed" : ""));
			++mNumberErrorsFound;
			isModified = true;
		}

		// Go through, and check that every entry exists and is valid
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next()) != 0)
		{
			// Lookup the item
			int32_t iIndex;
			IDBlock *piBlock = LookupID(en->GetObjectID(), iIndex);
			bool badEntry = false;
			if(piBlock != 0)
			{
				badEntry = !CheckDirectoryEntry(*en,
					dir.GetObjectID(), isModified);
			}
			// Item can't be found. Is it a directory?
			else if(en->IsDir())
			{
				// Store the directory for later attention
				mDirsWhichContainLostDirs[en->GetObjectID()] =
					dir.GetObjectID();
			}
			else
			{
				// Just remove the entry
				badEntry = true;
				BOX_ERROR("Directory ID " <<
					BOX_FORMAT_OBJECTID(dir.GetObjectID()) <<
					" references object " <<
					BOX_FORMAT_OBJECTID(en->GetObjectID()) <<
					" which does not exist.");
				++mNumberErrorsFound;
			}

			// Is this entry worth keeping?
			if(badEntry)
			{
				toDelete.push_back(en->GetObjectID());
			}
		}

		if(toDelete.size() > 0)
		{
			// Delete entries from directory
			for(std::vector<int64_t>::const_iterator d(toDelete.begin()); d != toDelete.end(); ++d)
			{
				BOX_ERROR("Removing directory entry " <<
					BOX_FORMAT_OBJECTID(*d) << " from "
					"directory " <<
					BOX_FORMAT_OBJECTID(dir.GetObjectID()));
				++mNumberErrorsFound;
				dir.DeleteEntry(*d);
			}

			// Mark as modified
			restart = true;
			isModified = true;

			// Errors found
		}
	}

	return isModified;
}

// Count valid remaining entries and the number of blocks in them.
void BackupStoreCheck::CountDirectoryEntries(BackupStoreDirectory& dir)
{
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	while((en = i.Next()) != 0)
	{
		int32_t iIndex;
		IDBlock *piBlock = LookupID(en->GetObjectID(), iIndex);
		bool wasAlreadyContained = false;

		ASSERT(piBlock != 0 ||
			mDirsWhichContainLostDirs.find(en->GetObjectID())
			!= mDirsWhichContainLostDirs.end());

		if (piBlock)
		{
			// Normally it would exist and this
			// check would not be necessary, but
			// we might have missing directories
			// that we will recreate later.
			// cf mDirsWhichContainLostDirs.
			uint8_t iflags = GetFlags(piBlock, iIndex);
			wasAlreadyContained = (iflags & Flags_IsContained);
			SetFlags(piBlock, iIndex, iflags | Flags_IsContained);
		}

		if(wasAlreadyContained)
		{
			// don't double-count objects that are
			// contained by another directory as well.
		}
		else if(en->IsDir())
		{
			mNumDirectories++;
		}
		else if(!en->IsFile())
		{
			BOX_TRACE("Not counting object " <<
				BOX_FORMAT_OBJECTID(en->GetObjectID()) <<
				" with flags " << en->GetFlags());
		}
		else // it's a file
		{
			// Add to sizes?
			// If piBlock was zero, then wasAlreadyContained
			// might be uninitialized.
			ASSERT(piBlock != NULL)

			// It can be both old and deleted.
			// If neither, then it's current.
			if(en->IsDeleted())
			{
				mNumDeletedFiles++;
				mBlocksInDeletedFiles += en->GetSizeInBlocks();
			}

			if(en->IsOld())
			{
				mNumOldFiles++;
				mBlocksInOldFiles += en->GetSizeInBlocks();
			}

			if(!en->IsDeleted() && !en->IsOld())
			{
				mNumCurrentFiles++;
				mBlocksInCurrentFiles += en->GetSizeInBlocks();
			}
		}

		mpNewRefs->AddReference(en->GetObjectID());
	}
}

bool BackupStoreCheck::CheckDirectoryEntry(BackupStoreDirectory::Entry& rEntry,
	int64_t DirectoryID, bool& rIsModified)
{
	int32_t IndexInDirBlock;
	IDBlock *piBlock = LookupID(rEntry.GetObjectID(), IndexInDirBlock);
	ASSERT(piBlock != 0);

	uint8_t iflags = GetFlags(piBlock, IndexInDirBlock);

	// Is the type the same?
	if(((iflags & Flags_IsDir) == Flags_IsDir) != rEntry.IsDir())
	{
		// Entry is of wrong type
		BOX_ERROR("Directory ID " <<
			BOX_FORMAT_OBJECTID(DirectoryID) <<
			" references object " <<
			BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
			" which has a different type than expected.");
		++mNumberErrorsFound;
		return false; // remove this entry
	}

	// Check that the entry is not already contained.
	if(iflags & Flags_IsContained)
	{
		BOX_ERROR("Directory ID " <<
			BOX_FORMAT_OBJECTID(DirectoryID) <<
			" references object " <<
			BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
			" which is already contained.");
		++mNumberErrorsFound;
		return false; // remove this entry
	}

	// Not already contained by another directory.
	// Don't set the flag until later, after we finish repairing
	// the directory and removing all bad entries.
	
	// Check that the container ID of the object is correct
	if(piBlock->mContainer[IndexInDirBlock] != DirectoryID)
	{
		// Needs fixing...
		if(iflags & Flags_IsDir)
		{
			// Add to will fix later list
			BOX_ERROR("Directory ID " <<
				BOX_FORMAT_OBJECTID(rEntry.GetObjectID())
				<< " has wrong container ID.");
			mDirsWithWrongContainerID.push_back(rEntry.GetObjectID());
			++mNumberErrorsFound;
		}
		else
		{
			// This is OK for files, they might move
			BOX_INFO("File ID " <<
				BOX_FORMAT_OBJECTID(rEntry.GetObjectID())
				<< " has different container ID, "
				"probably moved");
		}
		
		// Fix entry for now
		piBlock->mContainer[IndexInDirBlock] = DirectoryID;
	}

	// Check the object size
	if(rEntry.GetSizeInBlocks() != piBlock->mObjectSizeInBlocks[IndexInDirBlock])
	{
		// Wrong size, correct it.
		BOX_ERROR("Directory " << BOX_FORMAT_OBJECTID(DirectoryID) <<
			" entry for " << BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
			" has wrong size " << rEntry.GetSizeInBlocks() <<
			", should be " << piBlock->mObjectSizeInBlocks[IndexInDirBlock]);

		rEntry.SetSizeInBlocks(piBlock->mObjectSizeInBlocks[IndexInDirBlock]);

		// Mark as changed
		rIsModified = true;
		++mNumberErrorsFound;
	}

	return true; // don't delete this entry
}
