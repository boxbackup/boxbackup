// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreCheck2.cpp
//		Purpose: More backup store checking
//		Created: 22/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <string.h>

#include "autogen_BackupStoreException.h"
#include "BackupFileSystem.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreInfo.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreRefCountDatabase.h"
#include "MemBlockStream.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckRoot()
//		Purpose: Check the root directory exists.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckRoot()
{
	int32_t index = 0;
	IDBlock *pblock = LookupID(BACKUPSTORE_ROOT_DIRECTORY_ID, index);

	if(pblock != 0)
	{
		// Found it. Which is lucky. Mark it as contained.
		SetFlags(pblock, index, Flags_IsContained);
	}
	else
	{
		BOX_ERROR("Root directory doesn't exist");
		++mNumberErrorsFound;

		if(mFixErrors)
		{
			// Create a new root directory
			CreateBlankDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, BACKUPSTORE_ROOT_DIRECTORY_ID);
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CreateBlankDirectory(int64_t, int64_t)
//		Purpose: Creates a blank directory
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CreateBlankDirectory(int64_t DirectoryID, int64_t ContainingDirID)
{
	if(!mFixErrors)
	{
		// Don't do anything if we're not supposed to fix errors
		return;
	}

	BackupStoreDirectory dir(DirectoryID, ContainingDirID);
	mrFileSystem.PutDirectory(dir);

	// Record the fact we've done this
	mDirsAdded.insert(DirectoryID);

	// Add to sizes
	mBlocksUsed += dir.GetUserInfo1_SizeInBlocks();
	mBlocksInDirectories += dir.GetUserInfo1_SizeInBlocks();
}

class BackupStoreDirectoryFixer
{
	private:
	BackupStoreDirectory mDirectory;
	BackupFileSystem& mrFileSystem;

	public:
	BackupStoreDirectoryFixer(BackupFileSystem& rFileSystem, int64_t ID);
	void InsertObject(int64_t ObjectID, bool IsDirectory, int32_t lostDirNameSerial);
	~BackupStoreDirectoryFixer();
};

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckUnattachedObjects()
//		Purpose: Check for objects which aren't attached to anything
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckUnattachedObjects()
{
	typedef std::map<int64_t, BackupStoreDirectoryFixer*> fixers_t;
	typedef std::pair<int64_t, BackupStoreDirectoryFixer*> fixer_pair_t;
	fixers_t fixers;

	// Scan all objects, finding ones which have no container
	for(Info_t::const_iterator i(mInfo.begin()); i != mInfo.end(); ++i)
	{
		IDBlock *pblock = i->second;
		int32_t bentries = (pblock == mpInfoLastBlock)?mInfoLastBlockEntries:BACKUPSTORECHECK_BLOCK_SIZE;

		for(int e = 0; e < bentries; ++e)
		{
			uint8_t flags = GetFlags(pblock, e);
			if((flags & Flags_IsContained) == 0)
			{
				// Unattached object...
				int64_t ObjectID = pblock->mID[e];
				BOX_ERROR("Object " <<
					BOX_FORMAT_OBJECTID(ObjectID) <<
					" is unattached.");
				++mNumberErrorsFound;

				// What's to be done?
				int64_t putIntoDirectoryID = 0;

				if((flags & Flags_IsDir) == Flags_IsDir)
				{
					// Directory. Just put into lost and found.
					// (It doesn't contain its filename, so we
					// can't recreate the entry in the parent)
					putIntoDirectoryID = GetLostAndFoundDirID();
				}
				else
				{
					// File. Only attempt to attach it somewhere if it isn't a patch
					{
						int64_t diffFromObjectID = 0;

						// The easiest way to do this is to verify it again. Not such a bad penalty, because
						// this really shouldn't be done very often.
						{
							std::auto_ptr<IOStream> file = mrFileSystem.GetFile(ObjectID);
							BackupStoreFile::VerifyEncodedFileFormat(*file, &diffFromObjectID);
						}

						// If not zero, then it depends on another file, which may or may not be available.
						// Just delete it to be safe.
						if(diffFromObjectID != 0)
						{
							BOX_WARNING("Object " << BOX_FORMAT_OBJECTID(ObjectID) << " is unattached, and is a patch. Deleting, cannot reliably recover.");

							// Delete this object instead
							if(mFixErrors)
							{
								mrFileSystem.DeleteFile(ObjectID);
							}

							mBlocksUsed -= pblock->mObjectSizeInBlocks[e];

							// Move on to next item
							continue;
						}
					}

					// Files contain their original filename, so perhaps the orginal directory still exists,
					// or we can infer the existance of a directory?
					// Look for a matching entry in the mDirsWhichContainLostDirs map.
					// Can't do this with a directory, because the name just wouldn't be known, which is
					// pretty useless as bbackupd would just delete it. So better to put it in lost+found
					// where the admin can do something about it.
					int32_t dirindex;
					IDBlock *pdirblock = LookupID(pblock->mContainer[e], dirindex);
					if(pdirblock != 0)
					{
						// Something with that ID has been found. Is it a directory?
						if(GetFlags(pdirblock, dirindex) & Flags_IsDir)
						{
							// Directory exists, add to that one
							putIntoDirectoryID = pblock->mContainer[e];
						}
						else
						{
							// Not a directory. Use lost and found dir
							putIntoDirectoryID = GetLostAndFoundDirID();
						}
					}
					else if(mDirsAdded.find(pblock->mContainer[e]) != mDirsAdded.end()
						|| TryToRecreateDirectory(pblock->mContainer[e]))
					{
						// The directory reappeared, or was created somehow elsewhere
						putIntoDirectoryID = pblock->mContainer[e];
					}
					else
					{
						putIntoDirectoryID = GetLostAndFoundDirID();
					}
				}
				ASSERT(putIntoDirectoryID != 0);

				if (!mFixErrors)
				{
					continue;
				}

				BackupStoreDirectoryFixer* pFixer;
				fixers_t::iterator fi = 
					fixers.find(putIntoDirectoryID);
				if (fi == fixers.end())
				{
					// no match, create a new one
					pFixer = new BackupStoreDirectoryFixer(
						mrFileSystem, putIntoDirectoryID);
					fixers.insert(fixer_pair_t(
						putIntoDirectoryID, pFixer));
				}
				else
				{
					pFixer = fi->second;
				}

				int32_t lostDirNameSerial = 0;

				if(flags & Flags_IsDir)
				{
					lostDirNameSerial = mLostDirNameSerial++;
				}

				// Add it to the directory
				pFixer->InsertObject(ObjectID,
					((flags & Flags_IsDir) == Flags_IsDir),
					lostDirNameSerial);
				mpNewRefs->AddReference(ObjectID);
			}
		}
	}

	// clean up all the fixers. Deleting them commits them automatically.
	for (fixers_t::iterator i = fixers.begin(); i != fixers.end(); i++)
	{
		BackupStoreDirectoryFixer* pFixer = i->second;
		delete pFixer;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::TryToRecreateDirectory(int64_t)
//		Purpose: Recreate a missing directory
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
bool BackupStoreCheck::TryToRecreateDirectory(int64_t MissingDirectoryID)
{
	// During the directory checking phase, a map of "missing directory" to
	// containing directory was built. If we can find it here, then it's
	// something which can be recreated!
	std::map<BackupStoreCheck_ID_t, BackupStoreCheck_ID_t>::iterator missing(
		mDirsWhichContainLostDirs.find(MissingDirectoryID));
	if(missing == mDirsWhichContainLostDirs.end())
	{
		// Not a missing directory, can't recreate.
		return false;
	}

	// Can recreate this! Wooo!
	if(!mFixErrors)
	{
		BOX_WARNING("Missing directory " << 
			BOX_FORMAT_OBJECTID(MissingDirectoryID) <<
			" could be recreated.");
		mDirsAdded.insert(MissingDirectoryID);
		return true;
	}

	BOX_WARNING("Recreating missing directory " << 
		BOX_FORMAT_OBJECTID(MissingDirectoryID));

	// Create a blank directory
	BackupStoreDirectory dir(MissingDirectoryID, missing->second /* containing dir ID */);

	// Note that this directory already contains a directory entry pointing to
	// this dir, so it doesn't have to be added.
	mrFileSystem.PutDirectory(dir);

	// Record the fact we've done this
	mDirsAdded.insert(MissingDirectoryID);

	// Remove the entry from the map, so this doesn't happen again
	mDirsWhichContainLostDirs.erase(missing);

	return true;
}

BackupStoreDirectoryFixer::BackupStoreDirectoryFixer(BackupFileSystem& rFileSystem,
	int64_t ID)
: mrFileSystem(rFileSystem)
{
	mrFileSystem.GetDirectory(ID, mDirectory);
}

void BackupStoreDirectoryFixer::InsertObject(int64_t ObjectID, bool IsDirectory,
	int32_t lostDirNameSerial)
{
	// Data for the object
	BackupStoreFilename objectStoreFilename;
	int64_t modTime = 100;	// something which isn't zero or a special time
	int32_t sizeInBlocks = 0; // suitable for directories

	if(IsDirectory)
	{
		// Directory -- simply generate a name for it.
		char name[32];
		::snprintf(name, sizeof(name), "dir%08x", lostDirNameSerial);
		objectStoreFilename.SetAsClearFilename(name);
	}
	else
	{
		// Files require a little more work... Fill in size information.
		sizeInBlocks = mrFileSystem.GetFileSizeInBlocks(ObjectID);

		// Read in header
		std::auto_ptr<IOStream> file = mrFileSystem.GetFile(ObjectID);
		file_StreamFormat hdr;
		if(file->Read(&hdr, sizeof(hdr)) != sizeof(hdr) ||
			(ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
			&& ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V0
#endif
			))
		{
			// This should never happen, everything has been
			// checked before.
			THROW_EXCEPTION(BackupStoreException, Internal)
		}
		// This tells us nice things
		modTime = box_ntoh64(hdr.mModificationTime);
		// And the filename comes next
		objectStoreFilename.ReadFromStream(*file, IOStream::TimeOutInfinite);
	}

	// Add a new entry in an appropriate place
	mDirectory.AddUnattachedObject(objectStoreFilename, modTime,
		ObjectID, sizeInBlocks,
		IsDirectory?(BackupStoreDirectory::Entry::Flags_Dir):(BackupStoreDirectory::Entry::Flags_File));
}

BackupStoreDirectoryFixer::~BackupStoreDirectoryFixer()
{
	// Fix any flags which have been broken, which there's a good chance of doing
	mDirectory.CheckAndFix();

	// Write it out
	mrFileSystem.PutDirectory(mDirectory);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::GetLostAndFoundDirID()
//		Purpose: Returns the ID of the lost and found directory, creating it if necessary
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::GetLostAndFoundDirID()
{
	// Already allocated it?
	if(mLostAndFoundDirectoryID != 0)
	{
		return mLostAndFoundDirectoryID;
	}

	if(!mFixErrors)
	{
		// The result will never be used anyway if errors aren't being fixed
		return 1;
	}

	// Load up the root directory
	BackupStoreDirectory dir;
	mrFileSystem.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, dir);

	// Find a suitable name
	BackupStoreFilename lostAndFound;
	int n = 0;
	while(true)
	{
		char name[32];
		::snprintf(name, sizeof(name), "lost+found%d", n++);
		lostAndFound.SetAsClearFilename(name);
		if(!dir.NameInUse(lostAndFound))
		{
			// Found a name which can be used
			BOX_WARNING("Lost and found dir has name " << name);
			break;
		}
	}

	// Allocate an ID
	int64_t id = mLastIDInInfo + 1;

	// Create a blank directory
	CreateBlankDirectory(id, BACKUPSTORE_ROOT_DIRECTORY_ID);

	// Add an entry for it
	dir.AddEntry(lostAndFound, 0, id, 0, BackupStoreDirectory::Entry::Flags_Dir, 0);

	// Write out root dir
	mrFileSystem.PutDirectory(dir);

	// Store
	mLostAndFoundDirectoryID = id;

	// Tell caller
	return mLostAndFoundDirectoryID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::FixDirsWithWrongContainerID()
//		Purpose: Rewrites container IDs where required
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::FixDirsWithWrongContainerID()
{
	if(!mFixErrors)
	{
		// Don't do anything if we're not supposed to fix errors
		return;
	}

	// Run through things which need fixing
	for(std::vector<BackupStoreCheck_ID_t>::iterator i(mDirsWithWrongContainerID.begin());
			i != mDirsWithWrongContainerID.end(); ++i)
	{
		int32_t index = 0;
		IDBlock *pblock = LookupID(*i, index);
		if(pblock == 0) continue;

		// Load in
		BackupStoreDirectory dir;
		mrFileSystem.GetDirectory(*i, dir);

		// Adjust container ID
		dir.SetContainerID(pblock->mContainer[index]);

		// Write it out
		mrFileSystem.PutDirectory(dir);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::FixDirsWithLostDirs()
//		Purpose: Fix directories
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::FixDirsWithLostDirs()
{
	if(!mFixErrors)
	{
		// Don't do anything if we're not supposed to fix errors
		return;
	}

	// Run through things which need fixing
	for(std::map<BackupStoreCheck_ID_t, BackupStoreCheck_ID_t>::iterator i(mDirsWhichContainLostDirs.begin());
			i != mDirsWhichContainLostDirs.end(); ++i)
	{
		int32_t index = 0;
		IDBlock *pblock = LookupID(i->second, index);
		if(pblock == 0) continue;

		// Load in
		BackupStoreDirectory dir;
		mrFileSystem.GetDirectory(i->second, dir);

		// Delete the dodgy entry
		dir.DeleteEntry(i->first);

		// Fix it up
		dir.CheckAndFix();

		// Write it out
		mrFileSystem.PutDirectory(dir);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::WriteNewStoreInfo()
//		Purpose: Regenerate store info
//		Created: 23/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::WriteNewStoreInfo()
{
	// Attempt to load the existing store info file
	std::auto_ptr<BackupStoreInfo> apOldInfo;
	try
	{
		apOldInfo = mrFileSystem.GetBackupStoreInfoUncached();
		mAccountName = apOldInfo->GetAccountName();
	}
	catch(...)
	{
		BOX_ERROR("Load of existing store info failed, regenerating.");
		++mNumberErrorsFound;
	}

	BOX_INFO("Current files: " << mNumCurrentFiles << ", "
		"old files: " << mNumOldFiles << ", "
		"deleted files: " << mNumDeletedFiles << ", "
		"directories: " << mNumDirectories);

	// Minimum soft and hard limits to ensure that nothing gets deleted
	// by housekeeping.
	int64_t minSoft = ((mBlocksUsed * 11) / 10) + 1024;
	int64_t minHard = ((minSoft * 11) / 10) + 1024;

	int64_t softLimit = apOldInfo.get() ? apOldInfo->GetBlocksSoftLimit() : minSoft;
	int64_t hardLimit = apOldInfo.get() ? apOldInfo->GetBlocksHardLimit() : minHard;

	if(mNumberErrorsFound && apOldInfo.get())
	{
		if(apOldInfo->GetBlocksSoftLimit() > minSoft)
		{
			softLimit = apOldInfo->GetBlocksSoftLimit();
		}
		else
		{
			BOX_WARNING("Soft limit for account changed to ensure "
				"housekeeping doesn't delete files on next run.");
		}

		if(apOldInfo->GetBlocksHardLimit() > minHard)
		{
			hardLimit = apOldInfo->GetBlocksHardLimit();
		}
		else
		{
			BOX_WARNING("Hard limit for account changed to ensure "
				"housekeeping doesn't delete files on next run.");
		}
	}

	// Object ID
	int64_t lastObjID = mLastIDInInfo;
	if(mLostAndFoundDirectoryID != 0)
	{
		mLastIDInInfo++;
	}

	// Build a new store info
	std::auto_ptr<MemBlockStream> extra_data;
	if(apOldInfo.get())
	{
		extra_data.reset(new MemBlockStream(apOldInfo->GetExtraData()));
	}
	else
	{
		extra_data.reset(new MemBlockStream(/* empty */));
	}
	std::auto_ptr<BackupStoreInfo> apNewInfo(BackupStoreInfo::CreateForRegeneration(
		mAccountID,
		mAccountName,
		lastObjID,
		mBlocksUsed,
		mBlocksInCurrentFiles,
		mBlocksInOldFiles,
		mBlocksInDeletedFiles,
		mBlocksInDirectories,
		softLimit,
		hardLimit,
		(apOldInfo.get() ? apOldInfo->IsAccountEnabled() : true),
		*extra_data));
	apNewInfo->AdjustNumCurrentFiles(mNumCurrentFiles);
	apNewInfo->AdjustNumOldFiles(mNumOldFiles);
	apNewInfo->AdjustNumDeletedFiles(mNumDeletedFiles);
	apNewInfo->AdjustNumDirectories(mNumDirectories);

	// If there are any errors (apart from wrong block counts), then we
	// should reset the ClientStoreMarker to zero, which
	// CreateForRegeneration does. But if there are no major errors, then
	// we should maintain the old ClientStoreMarker, to avoid invalidating
	// the client's directory cache.
	if(apOldInfo.get() && !mNumberErrorsFound)
	{
		BOX_INFO("No major errors found, preserving old "
			"ClientStoreMarker: " <<
			apOldInfo->GetClientStoreMarker());
		apNewInfo->SetClientStoreMarker(apOldInfo->GetClientStoreMarker());
	}

	if(apOldInfo.get())
	{
		mNumberErrorsFound += apNewInfo->ReportChangesTo(*apOldInfo);
	}

	// Save to disc?
	if(mFixErrors)
	{
		mrFileSystem.PutBackupStoreInfo(*apNewInfo);
		BOX_INFO("New store info file written successfully.");
	}
}

#define FMT_OID(x) BOX_FORMAT_OBJECTID(x)
#define FMT_i      BOX_FORMAT_OBJECTID((*i)->GetObjectID())

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::CheckAndFix()
//		Purpose: Check the directory for obvious logical problems, and fix them.
//				 Return true if the directory was changed.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
bool BackupStoreDirectory::CheckAndFix()
{
	bool changed = false;

	// Check that if a file depends on a new version, that version is in this directory
	bool restart;

	do
	{
		restart = false;

		std::vector<Entry*>::iterator i(mEntries.begin());
		for(; i != mEntries.end(); ++i)
		{
			int64_t dependsNewer = (*i)->GetDependsNewer();
			if(dependsNewer != 0)
			{
				BackupStoreDirectory::Entry *newerEn = FindEntryByID(dependsNewer);
				if(newerEn == 0)
				{
					// Depends on something, but it isn't there.
					BOX_WARNING("Entry id " << FMT_i <<
						" removed because depends "
						"on newer version " <<
						FMT_OID(dependsNewer) <<
						" which doesn't exist");

					// Remove
					delete *i;
					mEntries.erase(i);

					// Mark as changed
					changed = true;

					// Start again at the beginning of the vector, the iterator is now invalid
					restart = true;
					break;
				}
				else
				{
					// Check that newerEn has it marked
					if(newerEn->GetDependsOlder() != (*i)->GetObjectID())
					{
						// Wrong entry
						BOX_TRACE("Entry id " <<
							FMT_OID(dependsNewer) <<
							", correcting DependsOlder to " <<
							FMT_i <<
							", was " <<
							FMT_OID(newerEn->GetDependsOlder()));
						newerEn->SetDependsOlder((*i)->GetObjectID());
						// Mark as changed
						changed = true;
					}
				}
			}
		}
	}
	while(restart);

	// Check that if a file has a dependency marked, it exists, and remove it if it doesn't
	{
		std::vector<Entry*>::iterator i(mEntries.begin());
		for(; i != mEntries.end(); ++i)
		{
			int64_t dependsOlder = (*i)->GetDependsOlder();
			if(dependsOlder != 0 && FindEntryByID(dependsOlder) == 0)
			{
				// Has an older version marked, but this doesn't exist. Remove this mark
				BOX_TRACE("Entry id " << FMT_i <<
					" was marked as depended on by " <<
					FMT_OID(dependsOlder) << ", "
					"which doesn't exist, dependency "
					"info cleared");

				(*i)->SetDependsOlder(0);

				// Mark as changed
				changed = true;
			}
		}
	}

	bool ch = false;
	do
	{
		// Reset change marker
		ch = false;

		// Search backwards -- so see newer versions first
		std::vector<Entry*>::iterator i(mEntries.end());
		if(i == mEntries.begin())
		{
			// Directory is empty, stop now
			return changed; // changed flag
		}

		// Records of things seen
		std::set<int64_t> idsEncountered;
		std::set<std::string> filenamesEncountered;

		do
		{
			// Look at previous
			--i;

			bool removeEntry = false;
			if((*i) == 0)
			{
				BOX_TRACE("Remove because null pointer found");
				removeEntry = true;
			}
			else
			{
				// Check mutually exclusive flags
				if((*i)->IsDir() && (*i)->IsFile())
				{
					// Bad! Unset the file flag
					BOX_TRACE("Entry " << FMT_i <<
						": File flag and dir flag both set");
					(*i)->RemoveFlags(Entry::Flags_File);
					changed = true;
				}
			
				// Check...
				if(idsEncountered.find((*i)->GetObjectID()) != idsEncountered.end())
				{
					// ID already seen, or type doesn't match
					BOX_TRACE("Entry " << FMT_i <<
						": Remove because ID already seen");
					removeEntry = true;
				}
				else
				{
					// Haven't already seen this ID, remember it
					idsEncountered.insert((*i)->GetObjectID());
					
					// Check to see if the name has already been encountered -- if not, then it
					// needs to have the old version flag set
					if(filenamesEncountered.find((*i)->GetName().GetEncodedFilename()) != filenamesEncountered.end())
					{
						// Seen before -- check old version flag set
						if(((*i)->GetFlags() & Entry::Flags_OldVersion) != Entry::Flags_OldVersion
							&& ((*i)->GetFlags() & Entry::Flags_Deleted) == 0)
						{
							// Not set, set it
							BOX_TRACE("Entry " << FMT_i <<
								": Set old flag");
							(*i)->AddFlags(Entry::Flags_OldVersion);
							changed = true;
						}
					}
					else
					{
						// Check old version flag NOT set
						if(((*i)->GetFlags() & Entry::Flags_OldVersion) == Entry::Flags_OldVersion)
						{
							// Set, unset it
							BOX_TRACE("Entry " << FMT_i <<
								": Old flag unset");
							(*i)->RemoveFlags(Entry::Flags_OldVersion);
							changed = true;
						}
						
						// Remember filename
						filenamesEncountered.insert((*i)->GetName().GetEncodedFilename());
					}
				}
			}

			if(removeEntry)
			{
				// Mark something as changed, in loop
				ch = true;

				// Mark something as globally changed
				changed = true;

				// erase the thing from the list
				Entry *pentry = (*i);
				mEntries.erase(i);

				// And delete the entry object
				delete pentry;

				// Stop going around this loop, as the iterator is now invalid
				break;
			}
		} while(i != mEntries.begin());

	} while(ch != false);

	return changed;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::AddUnattachedObject(...)
//		Purpose: Adds an object which is currently unattached. Assume that CheckAndFix() will be called afterwards.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::AddUnattachedObject(const BackupStoreFilename &rName,
	box_time_t ModificationTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags)
{
	Entry *pnew = new Entry(rName, ModificationTime, ObjectID, SizeInBlocks, Flags,
			ModificationTime /* use as attr mod time too */);
	try
	{
		// Want to order this just before the first object which has a higher ID,
		// which is the place it's most likely to be correct.
		std::vector<Entry*>::iterator i(mEntries.begin());
		for(; i != mEntries.end(); ++i)
		{
			if((*i)->GetObjectID() > ObjectID)
			{
				// Found a good place to insert it
				break;
			}
		}
		if(i == mEntries.end())
		{
			mEntries.push_back(pnew);
		}
		else
		{
			mEntries.insert(i, 1 /* just the one copy */, pnew);
		}
	}
	catch(...)
	{
		delete pnew;
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::NameInUse(const BackupStoreFilename &)
//		Purpose: Returns true if the name is currently in use in the directory
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
bool BackupStoreDirectory::NameInUse(const BackupStoreFilename &rName)
{
	for(std::vector<Entry*>::iterator i(mEntries.begin()); i != mEntries.end(); ++i)
	{
		if((*i)->GetName() == rName)
		{
			return true;
		}
	}

	return false;
}


