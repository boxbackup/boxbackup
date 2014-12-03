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
#include "Utils.h"

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
	  mBlocksInCurrentFiles(0),
	  mBlocksInOldFiles(0),
	  mBlocksInDeletedFiles(0),
	  mBlocksInDirectories(0),
	  mNumCurrentFiles(0),
	  mNumOldFiles(0),
	  mNumDeletedFiles(0),
	  mNumDirectories(0)
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
	if (mapNewRefs.get())
	{
		mapNewRefs->Discard();
		mapNewRefs.reset();
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
		std::string writeLockFilename;
		StoreStructure::MakeWriteLockFilename(mStoreRoot, mDiscSetNumber, writeLockFilename);
		ASSERT(FileExists(writeLockFilename));
	}

	if(!mQuiet && mFixErrors)
	{
		BOX_INFO("Will fix errors encountered during checking.");
	}

	BackupStoreAccountDatabase::Entry account(mAccountID, mDiscSetNumber);

	// Phase 1, check objects
	if(!mQuiet)
	{
		BOX_INFO("Checking store account ID " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << "...");
		BOX_INFO("Phase 1, check objects...");
	}
	CheckObjects();

	int32_t RootIndex;
	IDBlock *piBlock = LookupID(BACKUPSTORE_ROOT_DIRECTORY_ID, RootIndex);
	mapNewRefs = BackupStoreRefCountDatabase::Create(account,
		piBlock->mObjectSizeInBlocks[RootIndex]);

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

	try
	{
		mapOldRefs = BackupStoreRefCountDatabase::Load(account, false);
	}
	catch(BoxException &e)
	{
		BOX_WARNING("Reference count database was missing or "
			"corrupted, cannot check it for errors.");
		mNumberErrorsFound++;
	}

	// Phase 4, check StoreObjectMetaBase
	if(!mQuiet)
	{
		BOX_INFO("Phase 4, check StoreObjectMetaBase...");
	}
	CheckStoreObjectMetaBase();

	// Phase 5, check unattached objects
	if(!mQuiet)
	{
		BOX_INFO("Phase 5, fix unattached objects...");
	}
	CheckUnattachedObjects();

	// Phase 6, fix bad info
	if(!mQuiet)
	{
		BOX_INFO("Phase 6, fix unrecovered inconsistencies...");
	}
	FixDirsWithWrongContainerID();
	FixDirsWithLostDirs();

	// Phase 7, regenerate store info
	if(!mQuiet)
	{
		BOX_INFO("Phase 7, regenerate store info...");
	}
	WriteNewStoreInfo();

	if(mapOldRefs.get())
	{
		mNumberErrorsFound += mapNewRefs->ReportChangesTo(*mapOldRefs);
	}

	// force file to be saved and closed before releasing the lock below
	if(mFixErrors)
	{
		mapNewRefs->Commit();
	}
	else
	{
		mapNewRefs->Discard();
	}
	mapNewRefs.reset();

	if(mNumberErrorsFound > 0)
	{
		BOX_WARNING("Finished checking store account ID " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << ": " <<
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
		BOX_NOTICE("Finished checking store account ID " <<
			BOX_FORMAT_ACCOUNT(mAccountID) << ": "
			"no errors found");
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
		BOX_TRACE("Max dir starting ID is " <<
			BOX_FORMAT_OBJECTID(maxDir));
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
		// If any of the directories is missing, create it.
		RaidFileController &rcontroller(RaidFileController::GetController());
		RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mDiscSetNumber));

		if(!rdiscSet.IsNonRaidSet())
		{
			unsigned int numDiscs = rdiscSet.size();

			for(unsigned int l = 0; l < numDiscs; ++l)
			{
				// build name
				std::string dn(rdiscSet[l] + DIRECTORY_SEPARATOR + rDirName);
				struct stat st;

				if(stat(dn.c_str(), &st) != 0 && errno == ENOENT)
				{
					if(mkdir(dn.c_str(), 0755) != 0)
					{
						THROW_SYS_FILE_ERROR("Failed to "
							"create missing RaidFile "
							"directory", dn,
							RaidFileException, OSError);
					}
				}
			}
		}

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
				BOX_ERROR("Spurious or invalid directory " <<
					rDirName << DIRECTORY_SEPARATOR <<
					(*i) << " found, " <<
					(mFixErrors?"deleting":"delete manually"));
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
//		Purpose: Check all the files within this directory which has
//			 the given starting ID.
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
		BOX_WARNING("RaidFile dir " << dirName << " does not exist");
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
		// No other files should be present in subdirectories
		else if(StartID != 0)
		{
			fileOK = false;
		}
		// info and refcount databases are OK in the root directory
		else if(*i == "info" || *i == "refcount.db" ||
			*i == "refcount.rdb" || *i == "refcount.rdbX" ||
			*i == "StoreObjectMetaBase.rdb" ||
			*i == "StoreObjectMetaBase.rdbX")
		{
			fileOK = true;
		}
		else
		{
			fileOK = false;
		}

		if(!fileOK)
		{
			// Unexpected or bad file, delete it
			BOX_ERROR("Spurious file " << dirName <<
				DIRECTORY_SEPARATOR << (*i) << " found" <<
				(mFixErrors?", deleting":""));
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
			std::string filename = dirName + leaf;
			if(!CheckAndAddObject(StartID | i, filename))
			{
				// File was bad, delete it
				BOX_ERROR("Corrupted file " << filename <<
					" found" << (mFixErrors?", deleting":""));
				++mNumberErrorsFound;
				if(mFixErrors)
				{
					RaidFileWrite del(mDiscSetNumber, filename);
					del.Delete();
				}
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
bool BackupStoreCheck::CheckAndAddObject(int64_t ObjectID,
	const std::string &rFilename)
{
	// Info on object...
	bool isFile = true;
	int64_t containerID = -1;
	int64_t size = -1;

	try
	{
		// Open file
		std::auto_ptr<RaidFileRead> file(
			RaidFileRead::Open(mDiscSetNumber, rFilename));
		size = file->GetDiscUsageInBlocks();

		// Read in first four bytes -- don't have to worry about
		// retrying if not all bytes read as is RaidFile
		uint32_t signature;
		if(file->Read(&signature, sizeof(signature)) != sizeof(signature))
		{
			BOX_ERROR(BOX_FILE_MESSAGE(rFilename,
				"Object file " << BOX_FORMAT_OBJECTID(ObjectID) <<
				" too short for signature"));
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
			mBlocksInDirectories += size;
			break;

		default:
			// Unknown signature. Bad file. Very bad file.
			BOX_ERROR(BOX_FILE_MESSAGE(rFilename,
				"Object file " <<
				BOX_FORMAT_OBJECTID(ObjectID) <<
				" has unknown magic number: " <<
				BOX_FORMAT_HEX32(ntohl(signature))));
			return false;
			break;
		}
	}
	catch(BoxException &e)
	{
		// Error caught, not a good file then, let it be deleted
		BOX_ERROR(BOX_FILE_MESSAGE(rFilename,
			"Error while reading object file: " <<
			BOX_FORMAT_OBJECTID(ObjectID) <<
			": " << e.GetMessage()));
		return false;
	}
	catch(std::exception &e)
	{
		// Error caught, not a good file then, let it be deleted
		BOX_ERROR(BOX_FILE_MESSAGE(rFilename,
			"Error while reading object file: " <<
			BOX_FORMAT_OBJECTID(ObjectID) <<
			": " << e.what()));
		return false;
	}
	catch(...)
	{
		// Error caught, not a good file then, let it be deleted
		BOX_ERROR(BOX_FILE_MESSAGE(rFilename,
			"Unknown error while reading object file: " <<
			BOX_FORMAT_OBJECTID(ObjectID)));
		return false;
	}

	// Got a container ID? (ie check was successful)
	if(containerID == -1)
	{
		BOX_ERROR(BOX_FILE_MESSAGE(rFilename,
			"Failed to determine container ID of object " <<
			BOX_FORMAT_OBJECTID(ObjectID)));
		return false;
	}

	// Add to list of IDs known about
	AddID(ObjectID, containerID, size, isFile);

	// Add to usage counts
	mBlocksUsed += size;

	// If it looks like a good object, and it's non-RAID, and
	// this is a RAID set, then convert it to RAID.

	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mDiscSetNumber));
	if(!rdiscSet.IsNonRaidSet())
	{
		// See if the file exists
		RaidFileUtil::ExistType existance =
			RaidFileUtil::RaidFileExists(rdiscSet, rFilename);
		if(existance == RaidFileUtil::NonRaid)
		{
			BOX_WARNING(BOX_FILE_MESSAGE(rFilename,
				"Found non-RAID write file for object " <<
				BOX_FORMAT_OBJECTID(ObjectID) << " in RAID set" <<
				(mFixErrors?", transforming to RAID: ":"") <<
				(mFixErrors?rFilename:"")));
			if(mFixErrors)
			{
				RaidFileWrite write(mDiscSetNumber, rFilename);
				write.TransformToRaidStorage();
			}
		}
		else if(existance == RaidFileUtil::AsRaidWithMissingReadable)
		{
			BOX_WARNING(BOX_FILE_MESSAGE(rFilename,
				"Found damaged but repairable RAID file "
				"for object " << BOX_FORMAT_OBJECTID(ObjectID) <<
				" in RAID set" <<
				(mFixErrors?", repairing: ":"") <<
				(mFixErrors?rFilename:"")));
			if(mFixErrors)
			{
				std::auto_ptr<RaidFileRead> read(
					RaidFileRead::Open(mDiscSetNumber,
						rFilename));
				RaidFileWrite write(mDiscSetNumber, rFilename);
				write.Open(true /* overwrite */);
				read->CopyStreamTo(write);
				read.reset();
				write.Commit(true /* transform to RAID */);
			}
		}
	}

	// Report success
	return true;
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
		// Didn't verify
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
		// Wrong object ID
		BOX_ERROR("Directory ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
			" thinks it should be " <<
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
				std::string filename;
				StoreStructure::MakeObjectFilename(pblock->mID[e], mStoreRoot, mDiscSetNumber, filename, false /* no dir creation */);
				BackupStoreDirectory dir;
				{
					std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(mDiscSetNumber, filename));
					dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
				}

				bool isModified = false;

				if(dir.GetObjectID() != pblock->mID[e])
				{
					BOX_ERROR("Directory ID " <<
						BOX_FORMAT_OBJECTID(pblock->mID[e]) <<
						" has wrong object ID " <<
						BOX_FORMAT_OBJECTID(dir.GetObjectID()));
					dir.SetObjectID(pblock->mID[e]);
					++mNumberErrorsFound;
					isModified = true;
				}

				// Flag for modifications
				isModified |= CheckDirectory(dir);

				// Check the directory again, now that entries have been removed
				if(isModified && dir.CheckAndFix())
				{
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

				isModified |= CountDirectoryEntries(dir);

				if(isModified && mFixErrors)
				{
					BOX_WARNING("Writing modified directory to disk: " <<
						BOX_FORMAT_OBJECTID(pblock->mID[e]));
					RaidFileWrite fixed(mDiscSetNumber, filename);
					fixed.Open(true /* allow overwriting */);
					dir.WriteToStream(fixed);
					fixed.Commit(true /* convert to raid representation now */);
				}

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
					dir.GetObjectID(), piBlock, iIndex,
					isModified);
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

// Refcount valid remaining entries, and either update them from the SOMB or
// use them to initialise a new SOMB entry.
bool BackupStoreCheck::CountDirectoryEntries(BackupStoreDirectory& dir)
{
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	bool isModified = false;

	while((en = i.Next()) != 0)
	{
		if(!en->IsFile() && !en->IsDir())
		{
			BOX_WARNING("Directory " <<
				BOX_FORMAT_OBJECTID(dir.GetObjectID()) <<
				" contains entry " <<
				BOX_FORMAT_OBJECTID(en->GetObjectID()) <<
				" with unknown flags " << en->GetFlags() << ","
				" not counting");
		}

		BackupStoreRefCountDatabase::Entry soe(en->GetObjectID());
		bool rebuild = false;

		if(en->GetObjectID() <= mapNewRefs->GetLastObjectIDUsed())
		{
			soe = mapNewRefs->GetEntry(en->GetObjectID());
		}
		else if(mapOldRefs.get() && en->GetObjectID() <= mapOldRefs->GetLastObjectIDUsed())
		{
			// Copy data from old SOMB to compare, but set the
			// reference count to 0 because we're recounting.

			try
			{
				soe = mapOldRefs->GetEntry(en->GetObjectID());
				soe.SetRefCount(0);
			}
			catch (BoxException &e)
			{
				BOX_WARNING("Failed to get object metadata "
					"for " << BOX_FORMAT_OBJECTID(en->GetObjectID()) <<
					"from old StoreObjectMetaBase: " <<
					e.what());
				rebuild = true;
			}
		}
		else
		{
			rebuild = true;
		}

		soe.AddReference();

		if(rebuild)
		{
			// Rebuild the SOMB using data from the first directory
			// entry found. Any subsequent entries will be made
			// consistent with the SOMB, but no errors will be
			// reported because it's impossible to keep all directory
			// entries in sync with the SOMB without regular checks
			// like this one.
			soe.SetFlags(en->GetFlags());
			soe.SetSizeInBlocks(en->GetSizeInBlocks());
			soe.SetDependsNewer(en->GetDependsNewer());
			soe.SetDependsOlder(en->GetDependsOlder());
		}
		else
		{
			// The size and dependencies in the StoreObjectMetaBase
			// should already be correct.
			isModified |= en->UpdateFrom(soe, dir.GetObjectID());
		}

		ASSERT(en->GetFlags() == soe.GetFlags());
		ASSERT(en->GetSizeInBlocks() == soe.GetSizeInBlocks());
		ASSERT(en->GetDependsNewer() == soe.GetDependsNewer());
		ASSERT(en->GetDependsOlder() == soe.GetDependsOlder());

		mapNewRefs->PutEntry(soe);
	}

	return isModified;
}

bool BackupStoreCheck::CheckDirectoryEntry(BackupStoreDirectory::Entry& rEntry,
	int64_t DirectoryID, IDBlock *piBlock, int32_t IndexInDirBlock,
	bool& rIsModified, bool* pWasAlreadyContained)
{
	ASSERT(piBlock != 0);

	bool dummy;
	if(!pWasAlreadyContained)
	{
		pWasAlreadyContained = &dummy;
	}

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

	if(iflags & Flags_IsContained)
	{
		// With snapshots it's no longer an error for an object to be
		// contained in (referenced by) more than one directory.
		*pWasAlreadyContained = true;

		// However we need to check that the Old and Deleted flags
		// match, otherwise accounting will become much harder.
		if((bool)(iflags & Flags_IsOld) != rEntry.IsOld() ||
			(bool)(iflags & Flags_IsDeleted) != rEntry.IsDeleted())
		{
			BOX_ERROR("Directory ID " <<
				BOX_FORMAT_OBJECTID(DirectoryID) <<
				" also references object " <<
				BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
				", but with different flags");

			rEntry.AddFlags(
				((iflags & Flags_IsOld) & BackupStoreDirectory::Entry::Flags_OldVersion) |
				((iflags & Flags_IsDeleted) & BackupStoreDirectory::Entry::Flags_Deleted));
			rEntry.RemoveFlags(
				(!(iflags & Flags_IsOld) & BackupStoreDirectory::Entry::Flags_OldVersion) |
				(!(iflags & Flags_IsDeleted) & BackupStoreDirectory::Entry::Flags_Deleted));

			++mNumberErrorsFound;
			// not fatal, so don't remove entry
		}
	}
	else
	{
		SetFlags(piBlock, IndexInDirBlock, iflags |
			(rEntry.IsOld() ? Flags_IsOld : 0) |
			(rEntry.IsDeleted() ? Flags_IsDeleted : 0));
	}

	// Not already contained by another directory. Don't set the
	// IsContained flag until later, after we finish repairing the
	// directory and removing all bad entries.

	// Check that the container ID of the object is correct
	if(piBlock->mContainer[IndexInDirBlock] != DirectoryID)
	{
		// Don't try to get refcount of an object that's not yet in
		// the database, as it will throw an exception.
		if(mapNewRefs->GetLastObjectIDUsed() >= rEntry.GetObjectID() &&
			mapNewRefs->GetRefCount(rEntry.GetObjectID()) > 0)
		{
			// This can't be fixed if the object has multiple
			// references (at least one reference already, apart
			// from this one), because all but one of them will be
			// wrong. But we could check that the directory
			// which this object thinks is its parent actually
			// contains an entry pointing to it. TODO FIXME.
			//
			// We will find and fix an "error" if the container ID
			// is different to the lowest numbered reference to this
			// object. That's because we're still counting
			// references as we go, so the first time we enter this
			// branch, the reference count will be just 1. But this
			// isn't a big problem.
		}
		else if(iflags & Flags_IsDir)
		{
			// Needs fixing...
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

		// Fix entry for now. In the multiple reference case, we make
		// sure that the object's container points to a directory (any
		// directory) which does actually contain it. Why is this
		// needed anyway?
		piBlock->mContainer[IndexInDirBlock] = DirectoryID;
	}

	// Check the object size
	if(rEntry.GetSizeInBlocks() != piBlock->mObjectSizeInBlocks[IndexInDirBlock])
	{
		// Wrong size, correct it.
		//
		// TODO FIXME: uploading a patch changes the size of the object
		// in other directories, and we currently fudge it by updating
		// quietly during bbstoreaccounts check, because we don't have a
		// way to find all references. So don't count wrong size on a
		// patch as an error.
		if(rEntry.GetDependsNewer() != 0)
		{
			BOX_INFO("Directory " << BOX_FORMAT_OBJECTID(DirectoryID) <<
				" entry for " << BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
				" has wrong size " << rEntry.GetSizeInBlocks() <<
				", should be " << piBlock->mObjectSizeInBlocks[IndexInDirBlock]);
		}
		else
		{
			BOX_ERROR("Directory " << BOX_FORMAT_OBJECTID(DirectoryID) <<
				" entry for " << BOX_FORMAT_OBJECTID(rEntry.GetObjectID()) <<
				" has wrong size " << rEntry.GetSizeInBlocks() <<
				", should be " << piBlock->mObjectSizeInBlocks[IndexInDirBlock]);
			++mNumberErrorsFound;
		}

		rEntry.SetSizeInBlocks(piBlock->mObjectSizeInBlocks[IndexInDirBlock]);

		// Mark as changed
		rIsModified = true;
	}

	return true; // don't delete this entry
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::CheckStoreObjectMetaBase()
//		Purpose: Check each entry in the StoreObjectMetaBase against
//			 entries on disk, ensure that it's consistent,
//			 increase reference count of objects that are relied
//			 on by other objects (patches).
//		Created: 25/11/14
//
// --------------------------------------------------------------------------
void BackupStoreCheck::CheckStoreObjectMetaBase()
{
	int64_t max_real_id = mpInfoLastBlock->mID[mInfoLastBlockEntries - 1];
	int64_t max_somb_id = mapNewRefs->GetLastObjectIDUsed();

	// If this is wrong, then we made a mistake while regenerating the
	// StoreObjectMetaBase.
	ASSERT(max_somb_id == max_real_id);

	// We can only add newer entry dependencies once, regardless of how
	// many times the old object (patch) might be referenced in various
	// directories, otherwise we can't keep our accounting straight when
	// an object is converted to a patch.

	int64_t max_id = std::min(max_real_id, max_somb_id);
	for (int64_t id = 1; id < max_id; id++)
	{
		BackupStoreRefCountDatabase::Entry e = mapNewRefs->GetEntry(id);
		if(e.GetDependsNewer())
		{
			mapNewRefs->AddReference(e.GetDependsNewer());
		}

		if(e.IsDir())
		{
			mNumDirectories++;
			mBlocksInDirectories += e.GetSizeInBlocks();
		}
		else if(e.IsFile())
		{
			// It can be both old and deleted.
			// If neither, then it's current.
			if(e.IsDeleted())
			{
				mNumDeletedFiles++;
				mBlocksInDeletedFiles += e.GetSizeInBlocks();
			}

			if(e.IsOld())
			{
				mNumOldFiles++;
				mBlocksInOldFiles += e.GetSizeInBlocks();
			}

			if(!e.IsDeleted() && !e.IsOld())
			{
				mNumCurrentFiles++;
				mBlocksInCurrentFiles += e.GetSizeInBlocks();
			}
		}
		else // Not a file, nor a directory?
		{
			BOX_WARNING("StoreObjectMetaBase contains entry " <<
				BOX_FORMAT_OBJECTID(id) <<
				" with unknown flags " << e.GetFlags() << ","
				" not counting");
			mNumberErrorsFound++;
		}
	}
}
