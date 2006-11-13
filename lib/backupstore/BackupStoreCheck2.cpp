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

#include "BackupStoreCheck.h"
#include "StoreStructure.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "autogen_BackupStoreException.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreConstants.h"
#include "BackupStoreInfo.h"

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
		::printf("Root directory doesn't exist\n");
		
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
	
	// Serialise to disc
	std::string filename;
	StoreStructure::MakeObjectFilename(DirectoryID, mStoreRoot, mDiscSetNumber, filename, true /* make sure the dir exists */);
	RaidFileWrite obj(mDiscSetNumber, filename);
	obj.Open(false /* don't allow overwriting */);
	dir.WriteToStream(obj);
	int64_t size = obj.GetDiscUsageInBlocks();
	obj.Commit(true /* convert to raid now */);
	
	// Record the fact we've done this
	mDirsAdded.insert(DirectoryID);
	
	// Add to sizes
	mBlocksUsed += size;
	mBlocksInDirectories += size;
}


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
				::printf("Object %llx is unattached.\n", pblock->mID[e]);
				++mNumberErrorsFound;

				// What's to be done?
				int64_t putIntoDirectoryID = 0;

				if((flags & Flags_IsDir) == Flags_IsDir)
				{
					// Directory. Just put into lost and found.
					putIntoDirectoryID = GetLostAndFoundDirID();
				}
				else
				{
					// File. Only attempt to attach it somewhere if it isn't a patch
					{
						int64_t diffFromObjectID = 0;
						std::string filename;
						StoreStructure::MakeObjectFilename(pblock->mID[e], mStoreRoot, mDiscSetNumber, filename, false /* don't attempt to make sure the dir exists */);
						// The easiest way to do this is to verify it again. Not such a bad penalty, because
						// this really shouldn't be done very often.
						{
							std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
							BackupStoreFile::VerifyEncodedFileFormat(*file, &diffFromObjectID);
						}

						// If not zero, then it depends on another file, which may or may not be available.
						// Just delete it to be safe.
						if(diffFromObjectID != 0)
						{
							::printf("Object %llx is unattached, and is a patch. Deleting, cannot reliably recover.\n", pblock->mID[e]);
						
							// Delete this object instead
							if(mFixErrors)
							{
								RaidFileWrite del(mDiscSetNumber, filename);
								del.Delete();
							}
							
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

				// Add it to the directory
				InsertObjectIntoDirectory(pblock->mID[e], putIntoDirectoryID,
					((flags & Flags_IsDir) == Flags_IsDir));
			}
		}
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
		::printf("Missing directory %llx could be recreated\n", MissingDirectoryID);
		mDirsAdded.insert(MissingDirectoryID);
		return true;
	}
	::printf("Recreating missing directory %llx\n", MissingDirectoryID);
	
	// Create a blank directory
	BackupStoreDirectory dir(MissingDirectoryID, missing->second /* containing dir ID */);
	// Note that this directory already contains a directory entry pointing to
	// this dir, so it doesn't have to be added.
	
	// Serialise to disc
	std::string filename;
	StoreStructure::MakeObjectFilename(MissingDirectoryID, mStoreRoot, mDiscSetNumber, filename, true /* make sure the dir exists */);
	RaidFileWrite root(mDiscSetNumber, filename);
	root.Open(false /* don't allow overwriting */);
	dir.WriteToStream(root);
	root.Commit(true /* convert to raid now */);
	
	// Record the fact we've done this
	mDirsAdded.insert(MissingDirectoryID);
	
	// Remove the entry from the map, so this doesn't happen again
	mDirsWhichContainLostDirs.erase(missing);

	return true;
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
	std::string filename;
	StoreStructure::MakeObjectFilename(BACKUPSTORE_ROOT_DIRECTORY_ID, mStoreRoot, mDiscSetNumber, filename, false /* don't make sure the dir exists */);
	{
		std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
		dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
	}

	// Find a suitable name
	BackupStoreFilename lostAndFound;
	int n = 0;
	while(true)
	{
		char name[32];
		::sprintf(name, "lost+found%d", n++);
		lostAndFound.SetAsClearFilename(name);
		if(!dir.NameInUse(lostAndFound))
		{
			// Found a name which can be used
			::printf("Lost and found dir has name %s\n", name);
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
	RaidFileWrite root(mDiscSetNumber, filename);
	root.Open(true /* allow overwriting */);
	dir.WriteToStream(root);
	root.Commit(true /* convert to raid now */);
	
	// Store
	mLostAndFoundDirectoryID = id;

	// Tell caller
	return mLostAndFoundDirectoryID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::InsertObjectIntoDirectory(int64_t, int64_t, bool)
//		Purpose: 
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::InsertObjectIntoDirectory(int64_t ObjectID, int64_t DirectoryID, bool IsDirectory)
{
	if(!mFixErrors)
	{
		// Don't do anything if we're not supposed to fix errors
		return;
	}

	// Data for the object
	BackupStoreFilename objectStoreFilename;
	int64_t modTime = 100;	// something which isn't zero or a special time
	int32_t sizeInBlocks = 0; // suitable for directories

	if(IsDirectory)
	{
		// Directory -- simply generate a name for it.
		char name[32];
		::sprintf(name, "dir%08x", mLostDirNameSerial++);
		objectStoreFilename.SetAsClearFilename(name);
	}
	else
	{
		// Files require a little more work...
		// Open file
		std::string fileFilename;
		StoreStructure::MakeObjectFilename(ObjectID, mStoreRoot, mDiscSetNumber, fileFilename, false /* don't make sure the dir exists */);
		std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, fileFilename));
		// Fill in size information
		sizeInBlocks = file->GetDiscUsageInBlocks();
		// Read in header
		file_StreamFormat hdr;
		if(file->Read(&hdr, sizeof(hdr)) != sizeof(hdr) || (ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
			&& ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V0
#endif		
			))
		{
			// This should never happen, everything has been checked before.
			THROW_EXCEPTION(BackupStoreException, Internal)
		}
		// This tells us nice things
		modTime = box_ntoh64(hdr.mModificationTime);
		// And the filename comes next
		objectStoreFilename.ReadFromStream(*file, IOStream::TimeOutInfinite);
	}

	// Directory object
	BackupStoreDirectory dir;

	// Generate filename
	std::string filename;
	StoreStructure::MakeObjectFilename(DirectoryID, mStoreRoot, mDiscSetNumber, filename, false /* don't make sure the dir exists */);
	
	// Read it in
	{
		std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
		dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
	}
	
	// Add a new entry in an appropraite place
	dir.AddUnattactedObject(objectStoreFilename, modTime, ObjectID, sizeInBlocks,
		IsDirectory?(BackupStoreDirectory::Entry::Flags_Dir):(BackupStoreDirectory::Entry::Flags_File));

	// Fix any flags which have been broken, which there's a good change of going
	dir.CheckAndFix();
	
	// Write it out
	if(mFixErrors)
	{
		RaidFileWrite root(mDiscSetNumber, filename);
		root.Open(true /* allow overwriting */);
		dir.WriteToStream(root);
		root.Commit(true /* convert to raid now */);
	}
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
		std::string filename;
		StoreStructure::MakeObjectFilename(*i, mStoreRoot, mDiscSetNumber, filename, false /* don't make sure the dir exists */);
		{
			std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
			dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
		}

		// Adjust container ID
		dir.SetContainerID(pblock->mContainer[index]);
		
		// Write it out
		RaidFileWrite root(mDiscSetNumber, filename);
		root.Open(true /* allow overwriting */);
		dir.WriteToStream(root);
		root.Commit(true /* convert to raid now */);
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
		std::string filename;
		StoreStructure::MakeObjectFilename(i->second, mStoreRoot, mDiscSetNumber, filename, false /* don't make sure the dir exists */);
		{
			std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
			dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
		}

		// Delete the dodgy entry
		dir.DeleteEntry(i->first);
		
		// Fix it up
		dir.CheckAndFix();
		
		// Write it out
		RaidFileWrite root(mDiscSetNumber, filename);
		root.Open(true /* allow overwriting */);
		dir.WriteToStream(root);
		root.Commit(true /* convert to raid now */);
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
	std::auto_ptr<BackupStoreInfo> poldInfo;
	try
	{
		poldInfo.reset(BackupStoreInfo::Load(mAccountID, mStoreRoot, mDiscSetNumber, true /* read only */).release());
	}
	catch(...)
	{
		::printf("Load of existing store info failed, regenerating.\n");
		++mNumberErrorsFound;
	}

	// Minimum soft and hard limits
	int64_t minSoft = ((mBlocksUsed * 11) / 10) + 1024;
	int64_t minHard = ((minSoft * 11) / 10) + 1024;

	// Need to do anything?
	if(poldInfo.get() != 0 && mNumberErrorsFound == 0 && poldInfo->GetAccountID() == mAccountID)
	{
		// Leave the store info as it is, no need to alter it because nothing really changed,
		// and the only essential thing was that the account ID was correct, which is was.
		return;
	}
	
	// NOTE: We will always build a new store info, so the client store marker gets changed.

	// Work out the new limits
	int64_t softLimit = minSoft;
	int64_t hardLimit = minHard;
	if(poldInfo.get() != 0 && poldInfo->GetBlocksSoftLimit() > minSoft)
	{
		softLimit = poldInfo->GetBlocksSoftLimit();
	}
	else
	{
		::printf("NOTE: Soft limit for account changed to ensure housekeeping doesn't delete files on next run\n");
	}
	if(poldInfo.get() != 0 && poldInfo->GetBlocksHardLimit() > minHard)
	{
		hardLimit = poldInfo->GetBlocksHardLimit();
	}
	else
	{
		::printf("NOTE: Hard limit for account changed to ensure housekeeping doesn't delete files on next run\n");
	}
	
	// Object ID
	int64_t lastObjID = mLastIDInInfo;
	if(mLostAndFoundDirectoryID != 0)
	{
		mLastIDInInfo++;
	}

	// Build a new store info
	std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::CreateForRegeneration(
		mAccountID,
		mStoreRoot,
		mDiscSetNumber,
		lastObjID,
		mBlocksUsed,
		mBlocksInOldFiles,
		mBlocksInDeletedFiles,
		mBlocksInDirectories,
		softLimit,
		hardLimit));

	// Save to disc?
	if(mFixErrors)
	{
		info->Save();
		::printf("New store info file written successfully.\n");
	}
}


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
	{
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
					TRACE2("Entry id %llx removed because depends on newer version %llx which doesn't exist\n", (*i)->GetObjectID(), dependsNewer);
					
					// Remove
					delete *i;
					mEntries.erase(i);
					
					// Start again at the beginning of the vector, the iterator is now invalid
					i = mEntries.begin();
					
					// Mark as changed
					changed = true;
				}
				else
				{
					// Check that newerEn has it marked
					if(newerEn->GetDependsOlder() != (*i)->GetObjectID())
					{
						// Wrong entry
						TRACE3("Entry id %llx, correcting DependsOlder to %llx, was %llx\n", dependsNewer, (*i)->GetObjectID(), newerEn->GetDependsOlder());
						newerEn->SetDependsOlder((*i)->GetObjectID());
						// Mark as changed
						changed = true;
					}
				}
			}
		}
	}
	
	// Check that if a file has a dependency marked, it exists, and remove it if it doesn't
	{
		std::vector<Entry*>::iterator i(mEntries.begin());
		for(; i != mEntries.end(); ++i)
		{
			int64_t dependsOlder = (*i)->GetDependsOlder();
			if(dependsOlder != 0 && FindEntryByID(dependsOlder) == 0)
			{
				// Has an older version marked, but this doesn't exist. Remove this mark
				TRACE2("Entry id %llx was marked that %llx depended on it, which doesn't exist, dependency info cleared\n", (*i)->GetObjectID(), dependsOlder);

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
		std::set<BackupStoreFilename> filenamesEncountered;

		do
		{
			// Look at previous
			--i;

			bool removeEntry = false;
			if((*i) == 0)
			{
				TRACE0("Remove because null pointer found\n");
				removeEntry = true;
			}
			else
			{
				bool isDir = (((*i)->GetFlags() & Entry::Flags_Dir) == Entry::Flags_Dir);
				
				// Check mutually exclusive flags
				if(isDir && (((*i)->GetFlags() & Entry::Flags_File) == Entry::Flags_File))
				{
					// Bad! Unset the file flag
					TRACE1("Entry %llx: File flag set when dir flag set\n", (*i)->GetObjectID());
					(*i)->RemoveFlags(Entry::Flags_File);
					changed = true;
				}
			
				// Check...
				if(idsEncountered.find((*i)->GetObjectID()) != idsEncountered.end())
				{
					// ID already seen, or type doesn't match
					TRACE1("Entry %llx: Remove because ID already seen\n", (*i)->GetObjectID());
					removeEntry = true;
				}
				else
				{
					// Haven't already seen this ID, remember it
					idsEncountered.insert((*i)->GetObjectID());
					
					// Check to see if the name has already been encountered -- if not, then it
					// needs to have the old version flag set
					if(filenamesEncountered.find((*i)->GetName()) != filenamesEncountered.end())
					{
						// Seen before -- check old version flag set
						if(((*i)->GetFlags() & Entry::Flags_OldVersion) != Entry::Flags_OldVersion
							&& ((*i)->GetFlags() & Entry::Flags_Deleted) == 0)
						{
							// Not set, set it
							TRACE1("Entry %llx: Set old flag\n", (*i)->GetObjectID());
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
							TRACE1("Entry %llx: Old flag unset\n", (*i)->GetObjectID());
							(*i)->RemoveFlags(Entry::Flags_OldVersion);
							changed = true;
						}
						
						// Remember filename
						filenamesEncountered.insert((*i)->GetName());
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
//		Name:    BackupStoreDirectory::AddUnattactedObject(...)
//		Purpose: Adds an object which is currently unattached. Assume that CheckAndFix() will be called afterwards.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::AddUnattactedObject(const BackupStoreFilename &rName,
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


