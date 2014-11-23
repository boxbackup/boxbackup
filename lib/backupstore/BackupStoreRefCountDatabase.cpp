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

// TODO FIXME replace all references to REFCOUNT with STOREOBJECTMETABASE
#define REFCOUNT_MAGIC_VALUE_1 0x52656643 // RefC
#define REFCOUNT_MAGIC_VALUE_2 0x534f4d31 // SOM1 (StoreObjectMetabase1)
#define REFCOUNT_FILENAME      "StoreObjectMetaBase"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::BackupStoreRefCountDatabase()
//		Purpose: Default constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabase::BackupStoreRefCountDatabase(const
	BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly,
	bool Temporary, std::auto_ptr<FileStream> apDatabaseFile)
: mAccount(rAccount),
  mFilename(GetFilename(rAccount, Temporary)),
  mReadOnly(ReadOnly),
  mIsModified(false),
  mIsTemporaryFile(Temporary),
  mapDatabaseFile(apDatabaseFile)
{
	ASSERT(!(ReadOnly && Temporary)); // being both doesn't make sense
	ASSERT(mapDatabaseFile.get());
}

void BackupStoreRefCountDatabase::Commit()
{
	if (!mIsTemporaryFile)
	{
		// TODO FIXME replace all references to "refcount" with 
		// StoreObjectMetaBase
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Cannot commit a permanent refcount database");
	}

	if (!mapDatabaseFile.get())
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Refcount database is already closed");
	}

	mapDatabaseFile->Close();
	mapDatabaseFile.reset();
	mIsTemporaryFile = false;

	std::string Final_Filename = GetFilename(mAccount, false);

	#ifdef WIN32
	if(FileExists(Final_Filename) && unlink(Final_Filename.c_str()) != 0)
	{
		THROW_SYS_FILE_ERROR("Failed to delete old permanent refcount "
			"database file", mFilename, CommonException, OSFileError);
	}
	#endif

	if(rename(mFilename.c_str(), Final_Filename.c_str()) != 0)
	{
		THROW_SYS_ERROR("Failed to rename temporary refcount database "
			"file from " << mFilename << " to " <<
			Final_Filename, CommonException, OSFileError);
	}

	mFilename = Final_Filename;
	mIsModified = false;
}

void BackupStoreRefCountDatabase::Discard()
{
	if (!mIsTemporaryFile)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Cannot discard a permanent refcount database");
	}

	if (!mapDatabaseFile.get())
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Refcount database is already closed");
	}

	mapDatabaseFile->Close();
	mapDatabaseFile.reset();

	if(unlink(mFilename.c_str()) != 0)
	{
		THROW_SYS_FILE_ERROR("Failed to delete temporary refcount database "
			"file ", mFilename, CommonException, OSFileError);
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
	BackupStoreAccountDatabase::Entry& rAccount, bool Temporary)
{
	std::string RootDir = BackupStoreAccounts::GetAccountRoot(rAccount);
	ASSERT(RootDir[RootDir.size() - 1] == '/' ||
		RootDir[RootDir.size() - 1] == DIRECTORY_SEPARATOR_ASCHAR);

	std::string fn(RootDir + REFCOUNT_FILENAME ".rdb");
	if(Temporary)
	{
		fn += "X";
	}
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
	BackupStoreRefCountDatabase::Create
	(const BackupStoreAccountDatabase::Entry& rAccount)
{
	// Initial header
	refcount_StreamFormat hdr;
	hdr.mMagicValue = htonl(REFCOUNT_MAGIC_VALUE_2);
	hdr.mAccountID = htonl(rAccount.GetID());

	std::string Filename = GetFilename(rAccount, true); // temporary

	// Open the file for writing
	if (FileExists(Filename))
	{
		BOX_WARNING(BOX_FILE_MESSAGE(Filename, "Overwriting existing "
			"temporary reference count database"));
		if (unlink(Filename.c_str()) != 0)
		{
			THROW_SYS_FILE_ERROR("Failed to delete old temporary "
				"reference count database file", Filename,
				CommonException, OSFileError);
		}
	}

	int flags = O_CREAT | O_BINARY | O_RDWR | O_EXCL;
	std::auto_ptr<FileStream> DatabaseFile(new FileStream(Filename, flags));

	// Write header
	DatabaseFile->Write(&hdr, sizeof(hdr));

	// Make new object
	std::auto_ptr<BackupStoreRefCountDatabase> refcount(
		new BackupStoreRefCountDatabase(rAccount, false, true,
			DatabaseFile));

	// The root directory must always have one reference for a database
	// to be valid, so set that now on the new database. This will leave
	// mIsModified set to true.
	refcount_t root_refs = refcount->AddReference(BACKUPSTORE_ROOT_DIRECTORY_ID);
	ASSERT(root_refs == 1);

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
	// Generate the filename. Cannot open a temporary database, so it must
	// be a permanent one.
	std::string Filename = GetFilename(rAccount, false);
	int flags = ReadOnly ? O_RDONLY : O_RDWR;

	// Open the file for read/write
	std::auto_ptr<FileStream> dbfile(new FileStream(Filename,
		flags | O_BINARY));

	// Read in a header
	refcount_StreamFormat hdr;
	if(!dbfile->ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */))
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"short read", Filename, BackupStoreException,
			CouldNotLoadStoreInfo);
	}

	// Check it
	if(ntohl(hdr.mMagicValue) == REFCOUNT_MAGIC_VALUE_1)
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"old magic number", Filename, BackupStoreException,
			BadStoreInfoOnLoad);
	}

	if(ntohl(hdr.mMagicValue) != REFCOUNT_MAGIC_VALUE_2)
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"bad magic number: " << ntohl(hdr.mMagicValue),
			Filename, BackupStoreException, BadStoreInfoOnLoad);
	}

	if((int32_t)ntohl(hdr.mAccountID) != rAccount.GetID())
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"wrong account number: " <<
			BOX_FORMAT_ACCOUNT(ntohl(hdr.mAccountID)),
			Filename, BackupStoreException, BadStoreInfoOnLoad);
	}

	// Make new object
	std::auto_ptr<BackupStoreRefCountDatabase> refcount(
		new BackupStoreRefCountDatabase(rAccount, ReadOnly, false,
			dbfile));

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
const BackupStoreRefCountDatabase::EntryData
BackupStoreRefCountDatabase::GetEntryData(int64_t ObjectID)
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

	EntryData data;
	if (mapDatabaseFile->Read(&data, sizeof(data)) != sizeof(data))
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"short read at offset " << offset, mFilename,
			BackupStoreException, CouldNotLoadStoreInfo);
	}

	data.mFlags = ntohs(data.mFlags);
	data.mRefCount = ntohl(data.mRefCount);
	data.mSizeInBlocks = box_ntoh64(data.mSizeInBlocks);
	data.mDependsNewer = box_ntoh64(data.mDependsNewer);
	data.mDependsOlder = box_ntoh64(data.mDependsOlder);

	return data;
}

int64_t BackupStoreRefCountDatabase::GetLastObjectIDUsed() const
{
	return (GetSize() - sizeof(refcount_StreamFormat)) /
		sizeof(refcount_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::AddReference(int64_t
//			 ObjectID)
//		Purpose: Increments the reference count of the specified
//			 object, and returns the new reference count.
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------

BackupStoreRefCountDatabase::refcount_t
BackupStoreRefCountDatabase::AddReference(int64_t ObjectID)
{
	std::auto_ptr<Entry> ape;

	if (ObjectID > GetLastObjectIDUsed())
	{
		// new object, assume no previous references
		ape.reset(new Entry(ObjectID));
	}
	else
	{
		// read previous value from database
		const Entry& re = GetEntry(ObjectID);
		ape.reset(new Entry(ObjectID, re));
	}

	refcount_t refcount = ape->AddReference();
	PutEntry(*ape);
	return refcount;
}

void BackupStoreRefCountDatabase::PutEntryData(int64_t ObjectID,
	const EntryData& data)
{
	EntryData dataOut;
	dataOut.mFlags = htons(data.mFlags);
	dataOut.mRefCount = htonl(data.mRefCount);
	dataOut.mSizeInBlocks = box_hton64(data.mSizeInBlocks);
	dataOut.mDependsNewer = box_hton64(data.mDependsNewer);
	dataOut.mDependsOlder = box_hton64(data.mDependsOlder);

	IOStream::pos_type offset = GetOffset(ObjectID);
	mapDatabaseFile->Seek(offset, SEEK_SET);
	mapDatabaseFile->Write(&dataOut, sizeof(dataOut));
	mIsModified = true;
}

BackupStoreRefCountDatabase::refcount_t
BackupStoreRefCountDatabase::RemoveReference(int64_t ObjectID)
{
	// Read previous value from database. Must exist in database.
	Entry e = GetEntry(ObjectID);
	refcount_t refcount = e.RemoveReference();
	ASSERT(refcount >= 0);
	PutEntry(e);
	return refcount;
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
