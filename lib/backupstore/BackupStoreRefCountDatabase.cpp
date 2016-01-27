// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreRefCountDatabase.cpp
//		Purpose: Backup store object reference count database storage
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include <algorithm>

#include "BackupStoreRefCountDatabase.h"
#include "BackupStoreException.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "RaidFileController.h"
#include "RaidFileUtil.h"
#include "RaidFileException.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#define REFCOUNT_MAGIC_VALUE	0x52656643 // RefC
#define REFCOUNT_FILENAME	"refcount"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::BackupStoreRefCountDatabase()
//		Purpose: Default constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabase::BackupStoreRefCountDatabase(const std::string& Filename,
	int64_t mAccountID, bool ReadOnly, bool Temporary,
	std::auto_ptr<FileStream> apDatabaseFile)
: mFilename(Filename + (Temporary ? "X" : "")),
  mFinalFilename(Filename),
  mReadOnly(ReadOnly),
  mIsModified(false),
  mIsTemporaryFile(Temporary),
  mapDatabaseFile(apDatabaseFile)
{
	ASSERT(!(ReadOnly && Temporary)); // being both doesn't make sense
}

void BackupStoreRefCountDatabase::Commit()
{
	if (!mIsTemporaryFile)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Cannot commit a permanent reference count database");
	}

	if (!mapDatabaseFile.get())
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Reference count database is already closed");
	}

	mapDatabaseFile->Close();
	mapDatabaseFile.reset();

	#ifdef WIN32
	if(FileExists(mFinalFilename) && unlink(mFinalFilename.c_str()) != 0)
	{
		THROW_EMU_FILE_ERROR("Failed to delete old permanent refcount "
			"database file", mFinalFilename, CommonException,
			OSFileError);
	}
	#endif

	if(rename(mFilename.c_str(), mFinalFilename.c_str()) != 0)
	{
		THROW_EMU_ERROR("Failed to rename temporary refcount database "
			"file from " << mFilename << " to " <<
			mFinalFilename, CommonException, OSFileError);
	}

	mFilename = mFinalFilename;
	mIsModified = false;
	mIsTemporaryFile = false;
}

void BackupStoreRefCountDatabase::Discard()
{
	if (!mIsTemporaryFile)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Cannot discard a permanent reference count database");
	}

	// Under normal conditions, we should know whether the file is still
	// open or not, and not Discard it unless it's open. However if the
	// final rename() fails during Commit(), the file will already be
	// closed, and we don't want to blow up here in that case.
	if (mapDatabaseFile.get())
	{
		mapDatabaseFile->Close();
		mapDatabaseFile.reset();
	}

	if(unlink(mFilename.c_str()) != 0)
	{
		THROW_EMU_FILE_ERROR("Failed to delete temporary refcount "
			"database file", mFilename, CommonException,
			OSFileError);
	}

	mIsModified = false;
	mIsTemporaryFile = false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::~BackupStoreRefCountDatabase
//		Purpose: Destructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabase::~BackupStoreRefCountDatabase()
{
	if (mIsTemporaryFile)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"BackupStoreRefCountDatabase destroyed without "
			"explicit commit or discard");
		Discard();
	}
}

std::string BackupStoreRefCountDatabase::GetFilename(const
	BackupStoreAccountDatabase::Entry& rAccount)
{
	std::string RootDir = BackupStoreAccounts::GetAccountRoot(rAccount);
	ASSERT(RootDir[RootDir.size() - 1] == '/' ||
		RootDir[RootDir.size() - 1] == DIRECTORY_SEPARATOR_ASCHAR);

	std::string fn(RootDir + REFCOUNT_FILENAME ".rdb");
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(rAccount.GetDiscSet()));
	return RaidFileUtil::MakeWriteFileName(rdiscSet, fn);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::Create(int32_t,
//			 const std::string &, int, bool)
//		Purpose: Create a blank database, using a temporary file that
//			 you must Discard() or Commit() to make permanent.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreRefCountDatabase>
BackupStoreRefCountDatabase::Create(const BackupStoreAccountDatabase::Entry& rAccount)
{
	std::string Filename = GetFilename(rAccount);
	return Create(Filename, rAccount.GetID());
}

std::auto_ptr<BackupStoreRefCountDatabase>
BackupStoreRefCountDatabase::Create(const std::string& Filename, int64_t AccountID)
{
	// Open the file for writing
	std::string temp_filename = Filename + "X";
	if(FileExists(temp_filename))
	{
		BOX_WARNING(BOX_FILE_MESSAGE(Filename, "Overwriting existing "
			"temporary reference count database"));
		if (unlink(temp_filename.c_str()) != 0)
		{
			THROW_SYS_FILE_ERROR("Failed to delete old temporary "
				"reference count database file", temp_filename,
				CommonException, OSFileError);
		}
	}

	int flags = O_CREAT | O_BINARY | O_RDWR | O_EXCL;
	std::auto_ptr<FileStream> DatabaseFile(new FileStream(temp_filename, flags));

	// Write header
	refcount_StreamFormat hdr;
	hdr.mMagicValue = htonl(REFCOUNT_MAGIC_VALUE);
	hdr.mAccountID = htonl(AccountID);
	DatabaseFile->Write(&hdr, sizeof(hdr));

	// Make new object
	std::auto_ptr<BackupStoreRefCountDatabase> refcount(
		new BackupStoreRefCountDatabase(Filename, AccountID,
		false, // ReadOnly
		true, // Temporary
		DatabaseFile));

	// The root directory must always have one reference for a database
	// to be valid, so set that now on the new database. This will leave
	// mIsModified set to true.
	refcount->SetRefCount(BACKUPSTORE_ROOT_DIRECTORY_ID, 1);

	// return it to caller
	return refcount;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::Load(int32_t AccountID,
//			 BackupStoreAccountDatabase& rAccountDatabase,
//			 bool ReadOnly);
//		Purpose: Loads the info from disc, given the root
//			 information. Can be marked as read only.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreRefCountDatabase> BackupStoreRefCountDatabase::Load(
	const BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly)
{
	return BackupStoreRefCountDatabase::Load(GetFilename(rAccount),
		rAccount.GetID(), ReadOnly);
}

std::auto_ptr<BackupStoreRefCountDatabase>
BackupStoreRefCountDatabase::Load(const std::string& Filename, int64_t AccountID,
	bool ReadOnly)
{
	// You cannot reopen a temporary database, so it must be the permanent filename,
	// so no need to append an X to it.
	ASSERT(Filename.size() > 0 && Filename[Filename.size() - 1] != 'X');

	int flags = ReadOnly ? O_RDONLY : O_RDWR;
	std::auto_ptr<FileStream> database_file(new FileStream(Filename, flags | O_BINARY));

	// Read in a header
	refcount_StreamFormat hdr;
	if(!database_file->ReadFullBuffer(&hdr, sizeof(hdr),
		0 /* not interested in bytes read if this fails */))
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"short read", Filename, BackupStoreException,
			CouldNotLoadStoreInfo);
	}

	// Check it
	if(ntohl(hdr.mMagicValue) != REFCOUNT_MAGIC_VALUE ||
		(int32_t)ntohl(hdr.mAccountID) != AccountID)
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"bad magic number", Filename, BackupStoreException,
			BadStoreInfoOnLoad);
	}

	// Make new object
	std::auto_ptr<BackupStoreRefCountDatabase> refcount(
		new BackupStoreRefCountDatabase(Filename, AccountID, ReadOnly,
			false, // Temporary
			database_file));

	// return it to caller
	return refcount;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::GetRefCount(int64_t
//			 ObjectID)
//		Purpose: Get the number of references to the specified object
//			 out of the database
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabase::refcount_t
BackupStoreRefCountDatabase::GetRefCount(int64_t ObjectID) const
{
	IOStream::pos_type offset = GetOffset(ObjectID);

	if (GetSize() < offset + GetEntrySize())
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"attempted read of unknown refcount for object " <<
			BOX_FORMAT_OBJECTID(ObjectID), mFilename,
			BackupStoreException, UnknownObjectRefCountRequested);
	}

	mapDatabaseFile->Seek(offset, SEEK_SET);

	refcount_t refcount;
	if (mapDatabaseFile->Read(&refcount, sizeof(refcount)) !=
		sizeof(refcount))
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"short read at offset " << offset, mFilename,
			BackupStoreException, CouldNotLoadStoreInfo);
	}

	return ntohl(refcount);
}

int64_t BackupStoreRefCountDatabase::GetLastObjectIDUsed() const
{
	return (GetSize() - sizeof(refcount_StreamFormat)) /
		sizeof(refcount_t);
}

void BackupStoreRefCountDatabase::AddReference(int64_t ObjectID)
{
	refcount_t refcount;

	if (ObjectID > GetLastObjectIDUsed())
	{
		// new object, assume no previous references
		refcount = 0;
	}
	else
	{
		// read previous value from database
		refcount = GetRefCount(ObjectID);
	}

	refcount++;

	SetRefCount(ObjectID, refcount);
}

void BackupStoreRefCountDatabase::SetRefCount(int64_t ObjectID,
	refcount_t NewRefCount)
{
	IOStream::pos_type offset = GetOffset(ObjectID);
	mapDatabaseFile->Seek(offset, SEEK_SET);
	refcount_t RefCountNetOrder = htonl(NewRefCount);
	mapDatabaseFile->Write(&RefCountNetOrder, sizeof(RefCountNetOrder));
	mIsModified = true;
}

bool BackupStoreRefCountDatabase::RemoveReference(int64_t ObjectID)
{
	refcount_t refcount = GetRefCount(ObjectID); // must exist in database
	ASSERT(refcount > 0);
	refcount--;
	SetRefCount(ObjectID, refcount);
	return (refcount > 0);
}

int BackupStoreRefCountDatabase::ReportChangesTo(BackupStoreRefCountDatabase& rOldRefs)
{
	int ErrorCount = 0;
	int64_t MaxOldObjectId = rOldRefs.GetLastObjectIDUsed();
	int64_t MaxNewObjectId = GetLastObjectIDUsed();

	for (int64_t ObjectID = BACKUPSTORE_ROOT_DIRECTORY_ID;
		ObjectID < std::max(MaxOldObjectId, MaxNewObjectId);
		ObjectID++)
	{
		typedef BackupStoreRefCountDatabase::refcount_t refcount_t;
		refcount_t OldRefs = (ObjectID <= MaxOldObjectId) ?
			rOldRefs.GetRefCount(ObjectID) : 0;
		refcount_t NewRefs = (ObjectID <= MaxNewObjectId) ?
			this->GetRefCount(ObjectID) : 0;

		if (OldRefs != NewRefs)
		{
			BOX_WARNING("Reference count of object " <<
				BOX_FORMAT_OBJECTID(ObjectID) <<
				" changed from " << OldRefs <<
				" to " << NewRefs);
			ErrorCount++;
		}
	}

	return ErrorCount;
}
