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
#include <unistd.h>

#include "BackupStoreCheck.h"
#include "StoreStructure.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "autogen_BackupStoreException.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreFile.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreConstants.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::BackupStoreCheck(const std::string &, int, int32_t, bool, bool)
//		Purpose: Constructor
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
BackupStoreCheck::BackupStoreCheck(const std::string &rStoreRoot, int DiscSetNumber, int32_t AccountID, bool FixErrors, bool Quiet)
	: mStoreRoot(rStoreRoot),
	  mDiscSetNumber(DiscSetNumber),
	  mAccountID(AccountID),
	  mFixErrors(FixErrors),
	  mQuiet(Quiet),
	  mNumberErrorsFound(0),
	  mLastIDInInfo(0),
	  mpInfoLastBlock(0),
	  mInfoLastBlockEntries(0),
	  mLostDirNameSerial(0),
	  mLostAndFoundDirectoryID(0),
	  mBlocksUsed(0),
	  mBlocksInOldFiles(0),
	  mBlocksInDeletedFiles(0),
	  mBlocksInDirectories(0)
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
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::Check()
//		Purpose: Perform the check on the given account
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::Check()
{
	// Lock the account
	{
		std::string writeLockFilename;
		StoreStructure::MakeWriteLockFilename(mStoreRoot, mDiscSetNumber, writeLockFilename);

		bool gotLock = false;
		int triesLeft = 8;
		do
		{
			gotLock = mAccountLock.TryAndGetLock(writeLockFilename.c_str(), 0600 /* restrictive file permissions */);
			
			if(!gotLock)
			{
				--triesLeft;
				::sleep(1);
			}
		} while(!gotLock && triesLeft > 0);
	
		if(!gotLock)
		{
			// Couldn't lock the account -- just stop now
			if(!mQuiet)
			{
				::printf("Couldn't lock the account -- did not check.\nTry again later after the client has disconnected.\nAlternatively, forcibly kill the server.\n");
			}
			THROW_EXCEPTION(BackupStoreException, CouldNotLockStoreAccount)
		}
	}

	if(!mQuiet && mFixErrors)
	{
		::printf("NOTE: Will fix errors encountered during checking.\n");
	}

	// Phase 1, check objects
	if(!mQuiet)
	{
		::printf("Check store account ID %08x\nPhase 1, check objects...\n", mAccountID);
	}
	CheckObjects();
	
	// Phase 2, check directories
	if(!mQuiet)
	{
		::printf("Phase 2, check directories...\n");
	}
	CheckDirectories();
	
	// Phase 3, check root
	if(!mQuiet)
	{
		::printf("Phase 3, check root...\n");
	}
	CheckRoot();

	// Phase 4, check unattached objects
	if(!mQuiet)
	{
		::printf("Phase 4, fix unattached objects...\n");
	}
	CheckUnattachedObjects();

	// Phase 5, fix bad info
	if(!mQuiet)
	{
		::printf("Phase 5, fix unrecovered inconsistencies...\n");
	}
	FixDirsWithWrongContainerID();
	FixDirsWithLostDirs();
	
	// Phase 6, regenerate store info
	if(!mQuiet)
	{
		::printf("Phase 6, regenerate store info...\n");
	}
	WriteNewStoreInfo();
	
//	DUMP_OBJECT_INFO
	
	if(mNumberErrorsFound > 0)
	{
		::printf("%lld errors found\n", mNumberErrorsFound);
		if(!mFixErrors)
		{
			::printf("NOTE: No changes to the store account have been made.\n");
		}
		if(!mFixErrors && mNumberErrorsFound > 0)
		{
			::printf("Run again with fix option to fix these errors\n");
		}
		if(mNumberErrorsFound > 0)
		{
			::printf("You should now use bbackupquery on the client machine to examine the store.\n");
			if(mLostAndFoundDirectoryID != 0)
			{
				::printf("A lost+found directory was created in the account root.\n"\
					"This contains files and directories which could not be matched to existing directories.\n"\
					"bbackupd will delete this directory in a few days time.\n");
			}
		}
	}
	else
	{
		::printf("Store account checked, no errors found.\n");
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static TwoDigitHexToInt(const char *, int &)
//		Purpose: Convert a two digit hex string to an int, returning whether it's valid or not
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
static inline bool TwoDigitHexToInt(const char *String, int &rNumberOut)
{
	int n = 0;
	// Char 0
	if(String[0] >= '0' && String[0] <= '9')
	{
		n = (String[0] - '0') << 4;
	}
	else if(String[0] >= 'a' && String[0] <= 'f')
	{
		n = ((String[0] - 'a') + 0xa) << 4;
	}
	else
	{
		return false;
	}
	// Char 1
	if(String[1] >= '0' && String[1] <= '9')
	{
		n |= String[1] - '0';
	}
	else if(String[1] >= 'a' && String[1] <= 'f')
	{
		n |= (String[1] - 'a') + 0xa;
	}
	else
	{
		return false;
	}

	// Return a valid number
	rNumberOut = n;
	return true;
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
	// Maximum start ID of directories -- worked out by looking at disc contents, not trusting anything
	int64_t maxDir = 0;

	// Find the maximum directory starting ID
	{
		// Make sure the starting root dir doesn't end with '/'.
		std::string start(mStoreRoot);
		if(start.size() > 0 && start[start.size() - 1] == '/')
		{
			start.resize(start.size() - 1);
		}
	
		maxDir = CheckObjectsScanDir(0, 1, mStoreRoot);
		TRACE1("Max dir starting ID is %llx\n", maxDir);
	}
	
	// Then go through and scan all the objects within those directories
	for(int64_t d = 0; d <= maxDir; d += (1<<STORE_ID_SEGMENT_LENGTH))
	{
		CheckObjectsDir(d);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckObjectsScanDir(int64_t, int, int, const std::string &)
//		Purpose: Read in the contents of the directory, recurse to other levels,
//				 return the maximum starting ID of any directory found.
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::CheckObjectsScanDir(int64_t StartID, int Level, const std::string &rDirName)
{
	//TRACE2("Scan directory for max dir starting ID %s, StartID %lld\n", rDirName.c_str(), StartID);

	int64_t maxID = StartID;

	// Read in all the directories, and recurse downwards
	{
		std::vector<std::string> dirs;
		RaidFileRead::ReadDirectoryContents(mDiscSetNumber, rDirName,
			RaidFileRead::DirReadType_DirsOnly, dirs);

		for(std::vector<std::string>::const_iterator i(dirs.begin()); i != dirs.end(); ++i)
		{
			// Check to see if it's the right name
			int n = 0;
			if((*i).size() == 2 && TwoDigitHexToInt((*i).c_str(), n)
				&& n < (1<<STORE_ID_SEGMENT_LENGTH))
			{
				// Next level down
				int64_t mi = CheckObjectsScanDir(StartID | (n << (Level * STORE_ID_SEGMENT_LENGTH)), Level + 1,
					rDirName + DIRECTORY_SEPARATOR + *i);
				// Found a greater starting ID?
				if(mi > maxID)
				{
					maxID = mi;
				}
			}
			else
			{
				::printf("Spurious or invalid directory %s/%s found%s -- delete manually\n", rDirName.c_str(), (*i).c_str(), mFixErrors?", deleting":"");
				++mNumberErrorsFound;
			}
		}
	}

	return maxID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckObjectsDir(int64_t)
//		Purpose: Check all the files within this directory which has the given starting ID.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckObjectsDir(int64_t StartID)
{
	// Make directory name -- first generate the filename of an entry in it
	std::string dirName;
	StoreStructure::MakeObjectFilename(StartID, mStoreRoot, mDiscSetNumber, dirName, false /* don't make sure the dir exists */);
	// Check expectations
	ASSERT(dirName.size() > 4 && 
		dirName[dirName.size() - 4] == DIRECTORY_SEPARATOR_ASCHAR);
	// Remove the filename from it
	dirName.resize(dirName.size() - 4); // four chars for "/o00"
	
	// Check directory exists
	if(!RaidFileRead::DirectoryExists(mDiscSetNumber, dirName))
	{
		TRACE1("RaidFile dir %s does not exist\n", dirName.c_str());
		return;
	}

	// Read directory contents
	std::vector<std::string> files;
	RaidFileRead::ReadDirectoryContents(mDiscSetNumber, dirName,
		RaidFileRead::DirReadType_FilesOnly, files);
	
	// Array of things present
	bool idsPresent[(1<<STORE_ID_SEGMENT_LENGTH)];
	for(int l = 0; l < (1<<STORE_ID_SEGMENT_LENGTH); ++l)
	{
		idsPresent[l] = false;
	}
	
	// Parse each entry, building up a list of object IDs which are present in the dir.
	// This is done so that whatever order is retured from the directory, objects are scanned
	// in order.
	// Filename must begin with a 'o' and be three characters long, otherwise it gets deleted.
	for(std::vector<std::string>::const_iterator i(files.begin()); i != files.end(); ++i)
	{
		bool fileOK = true;
		int n = 0;
		if((*i).size() == 3 && (*i)[0] == 'o' && TwoDigitHexToInt((*i).c_str() + 1, n)
			&& n < (1<<STORE_ID_SEGMENT_LENGTH))
		{
			// Filename is valid, mark as existing
			idsPresent[n] = true;
		}
		else
		{
			// info file in root dir is OK!
			if(StartID != 0 || ::strcmp("info", (*i).c_str()) != 0)
			{
				fileOK = false;
			}
		}
		
		if(!fileOK)
		{
			// Unexpected or bad file, delete it
			::printf("Spurious file %s" DIRECTORY_SEPARATOR "%s "
				"found%s\n", dirName.c_str(), (*i).c_str(), 
				mFixErrors?", deleting":"");
			++mNumberErrorsFound;
			if(mFixErrors)
			{
				RaidFileWrite del(mDiscSetNumber, dirName + DIRECTORY_SEPARATOR + *i);
				del.Delete();
			}
		}
	}
	
	// Check all the objects found in this directory
	for(int i = 0; i < (1<<STORE_ID_SEGMENT_LENGTH); ++i)
	{
		if(idsPresent[i])
		{
			// Check the object is OK, and add entry
			char leaf[8];
			::sprintf(leaf, DIRECTORY_SEPARATOR "o%02x", i);
			if(!CheckAndAddObject(StartID | i, dirName + leaf))
			{
				// File was bad, delete it
				::printf("Corrupted file %s%s found%s\n", dirName.c_str(), leaf, mFixErrors?", deleting":"");
				++mNumberErrorsFound;
				if(mFixErrors)
				{
					RaidFileWrite del(mDiscSetNumber, dirName + leaf);
					del.Delete();
				}
			}
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckAndAddObject(int64_t, const std::string &)
//		Purpose: Check a specific object and add it to the list if it's OK -- if
//				 there are any errors with the reading, return false and it'll be deleted.
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
bool BackupStoreCheck::CheckAndAddObject(int64_t ObjectID, const std::string &rFilename)
{
	// Info on object...
	bool isFile = true;
	int64_t containerID = -1;
	int64_t size = -1;

	try
	{
		// Open file
		std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, rFilename));
		size = file->GetDiscUsageInBlocks();
		
		// Read in first four bytes -- don't have to worry about retrying if not all bytes read as is RaidFile
		uint32_t signature;
		if(file->Read(&signature, sizeof(signature)) != sizeof(signature))
		{
			// Too short, can't read signature from it
			return false;
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
			// File... check
			containerID = CheckFile(ObjectID, *file);
			break;

		case OBJECTMAGIC_DIR_MAGIC_VALUE:
			isFile = false;
			containerID = CheckDirInitial(ObjectID, *file);
			break;

		default:
			// Unknown signature. Bad file. Very bad file.
			return false;
			break;
		}
		
		// Add to usage counts
		mBlocksUsed += size;
		if(!isFile)
		{
			mBlocksInDirectories += size;
		}
	}
	catch(...)
	{
		// Error caught, not a good file then, let it be deleted
		return false;
	}
	
	// Got a container ID? (ie check was successful)
	if(containerID == -1)
	{
		return false;
	}
	
	// Add to list of IDs known about
	AddID(ObjectID, containerID, size, isFile);

	// Report success
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckFile(int64_t, IOStream &)
//		Purpose: Do check on file, return original container ID if OK, or -1 on error
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
int64_t BackupStoreCheck::CheckFile(int64_t ObjectID, IOStream &rStream)
{
	// Check that it's not the root directory ID. Having a file as the root directory would be bad.
	if(ObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		// Get that dodgy thing deleted!
		::printf("Have file as root directory. This is bad.\n");
		return -1;
	}

	// Check the format of the file, and obtain the container ID
	int64_t originalContainerID = -1;
	if(!BackupStoreFile::VerifyEncodedFileFormat(rStream, 0 /* don't want diffing from ID */,
		&originalContainerID))
	{
		// Didn't verify
		return -1;
	}

	return originalContainerID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckDirInitial(int64_t, IOStream &)
//		Purpose: Do initial check on directory, return container ID if OK, or -1 on error
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
		// Wrong object ID
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

	// Scan all objects	
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
				std::string filename;
				StoreStructure::MakeObjectFilename(pblock->mID[e], mStoreRoot, mDiscSetNumber, filename, false /* no dir creation */);
				BackupStoreDirectory dir;
				{
					std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
					dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
				}
				
				// Flag for modifications
				bool isModified = false;
				
				// Check for validity
				if(dir.CheckAndFix())
				{
					// Wasn't quite right, and has been modified
					::printf("Directory ID %llx has bad structure\n", pblock->mID[e]);
					++mNumberErrorsFound;
					isModified = true;
				}
				
				// Go through, and check that everything in that directory exists and is valid
				std::vector<int64_t> toDelete;
				
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
						// Found. Get flags
						uint8_t iflags = GetFlags(piBlock, iIndex);
						
						// Is the type the same?
						if(((iflags & Flags_IsDir) == Flags_IsDir)
							!= ((en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir) == BackupStoreDirectory::Entry::Flags_Dir))
						{
							// Entry is of wrong type
							::printf("Directory ID %llx references object %llx which has a different type than expected.\n", pblock->mID[e], en->GetObjectID());
							badEntry = true;
						}
						else
						{
							// Check that the entry is not already contained.
							if(iflags & Flags_IsContained)
							{
								badEntry = true;
								::printf("Directory ID %llx references object %llx which is already contained.\n", pblock->mID[e], en->GetObjectID());
							}
							else
							{
								// Not already contained -- mark as contained
								SetFlags(piBlock, iIndex, iflags | Flags_IsContained);
								
								// Check that the container ID of the object is correct
								if(piBlock->mContainer[iIndex] != pblock->mID[e])
								{
									// Needs fixing...
									if(iflags & Flags_IsDir)
									{
										// Add to will fix later list
										::printf("Directory ID %llx has wrong container ID.\n", en->GetObjectID());
										mDirsWithWrongContainerID.push_back(en->GetObjectID());
									}
									else
									{
										// This is OK for files, they might move
										::printf("File ID %llx has different container ID, probably moved\n", en->GetObjectID());
									}
									
									// Fix entry for now
									piBlock->mContainer[iIndex] = pblock->mID[e];
								}
							}
						}
						
						// Check the object size, if it's OK and a file
						if(!badEntry && !((iflags & Flags_IsDir) == Flags_IsDir))
						{
							if(en->GetSizeInBlocks() != piBlock->mObjectSizeInBlocks[iIndex])
							{
								// Correct
								en->SetSizeInBlocks(piBlock->mObjectSizeInBlocks[iIndex]);
								// Mark as changed
								isModified = true;
								// Tell user
								::printf("Directory ID %llx has wrong size for object %llx\n", pblock->mID[e], en->GetObjectID());
							}
						}
					}
					else
					{
						// Item can't be found. Is it a directory?
						if(en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir)
						{
							// Store the directory for later attention
							mDirsWhichContainLostDirs[en->GetObjectID()] = pblock->mID[e];
						}
						else
						{
							// Just remove the entry
							badEntry = true;
							::printf("Directory ID %llx references object %llx which does not exist.\n", pblock->mID[e], en->GetObjectID());
						}
					}
					
					// Is this entry worth keeping?
					if(badEntry)
					{
						toDelete.push_back(en->GetObjectID());
					}
					else
					{
						// Add to sizes?
						if(en->GetFlags() & BackupStoreDirectory::Entry::Flags_OldVersion)
						{
							mBlocksInOldFiles += en->GetSizeInBlocks();
						}
						if(en->GetFlags() & BackupStoreDirectory::Entry::Flags_Deleted)
						{
							mBlocksInDeletedFiles += en->GetSizeInBlocks();
						}
					}
				}
				
				if(toDelete.size() > 0)
				{
					// Delete entries from directory
					for(std::vector<int64_t>::const_iterator d(toDelete.begin()); d != toDelete.end(); ++d)
					{
						dir.DeleteEntry(*d);
					}
					
					// Mark as modified
					isModified = true;
					
					// Check the directory again, now that entries have been removed
					dir.CheckAndFix();
					
					// Errors found
					++mNumberErrorsFound;
				}
				
				if(isModified && mFixErrors)
				{	
					::printf("Fixing directory ID %llx\n", pblock->mID[e]);

					// Save back to disc
					RaidFileWrite fixed(mDiscSetNumber, filename);
					fixed.Open(true /* allow overwriting */);
					dir.WriteToStream(fixed);
					// Commit it
					fixed.Commit(true /* convert to raid representation now */);
				}
			}
		}
	}

}


