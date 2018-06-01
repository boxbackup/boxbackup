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

typedef BackupStoreRefCountDatabase::refcount_t refcount_t;

// set packing to one byte
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

typedef struct
{
	uint32_t mMagicValue;	// also the version number
	uint32_t mAccountID;
} refcount_StreamFormat;

// Use default packing
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreRefCountDatabaseImpl
//		Purpose: Implementation of the BackupStoreRefCountDatabase
//			 interface.
//		Created: 2016/04/17
//
// --------------------------------------------------------------------------
class BackupStoreRefCountDatabaseImpl : public BackupStoreRefCountDatabase
{
public:
	~BackupStoreRefCountDatabaseImpl();

	BackupStoreRefCountDatabaseImpl(const std::string& Filename, int64_t AccountID,
		bool ReadOnly, bool PotentialDB, bool TemporaryDB,
		std::auto_ptr<FileStream> apDatabaseFile);

private:
	// No copying allowed
	BackupStoreRefCountDatabaseImpl(const BackupStoreRefCountDatabase &);

public:
	// Create a blank database, using a temporary file that you must
	// Discard() or Commit() to make permanent.
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const BackupStoreAccountDatabase::Entry& rAccount);
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const std::string& Filename, int64_t AccountID);
	void Commit();
	void Discard();
	void Close()
	{
		// If this was a potential database, it should have been
		// Commit()ed or Discard()ed first.
		ASSERT(!mIsPotentialDB);
		// If this is a temporary database, we should Discard() it to
		// delete the file:
		if(mIsTemporaryDB)
		{
			Discard();
		}
		mapDatabaseFile.reset();
	}
	void Reopen();
	bool IsReadOnly() { return mReadOnly; }

	// Load it from the store
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(
		const BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly);
	// Load it from a stream (file or RaidFile)
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(
		const std::string& FileName, int64_t AccountID, bool ReadOnly);

	// Data access functions
	refcount_t GetRefCount(int64_t ObjectID) const;
	int64_t GetLastObjectIDUsed() const;

	// SetRefCount is not private, but this whole implementation is effectively
	// private, and SetRefCount is not in the interface, so it's not callable from
	// anywhere else.
	void SetRefCount(int64_t ObjectID, refcount_t NewRefCount);

	// Data modification functions
	void AddReference(int64_t ObjectID);
	// RemoveReference returns false if refcount drops to zero
	bool RemoveReference(int64_t ObjectID);
	int ReportChangesTo(BackupStoreRefCountDatabase& rOldRefs);

private:
	IOStream::pos_type GetSize() const
	{
		return mapDatabaseFile->GetPosition() +
			mapDatabaseFile->BytesLeftToRead();
	}
	IOStream::pos_type GetEntrySize() const
	{
		return sizeof(refcount_t);
	}
	IOStream::pos_type GetOffset(int64_t ObjectID) const
	{
		return ((ObjectID - 1) * GetEntrySize()) +
			sizeof(refcount_StreamFormat);
	}

	// Location information
	int64_t mAccountID;
	std::string mFilename, mFinalFilename;
	bool mReadOnly;
	bool mIsModified;
	bool mIsPotentialDB;
	bool mIsTemporaryDB;
	std::auto_ptr<FileStream> mapDatabaseFile;

	bool NeedsCommitOrDiscard()
	{
		return mapDatabaseFile.get() && mIsModified && mIsPotentialDB;
	}
};


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabaseImpl::BackupStoreRefCountDatabase()
//		Purpose: Default constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabaseImpl::BackupStoreRefCountDatabaseImpl(
	const std::string& Filename, int64_t AccountID, bool ReadOnly, bool PotentialDB,
	bool TemporaryDB, std::auto_ptr<FileStream> apDatabaseFile)
: mAccountID(AccountID),
  mFilename(Filename + (PotentialDB ? "X" : "")),
  mFinalFilename(TemporaryDB ? "" : Filename),
  mReadOnly(ReadOnly),
  mIsModified(false),
  mIsPotentialDB(PotentialDB),
  mIsTemporaryDB(TemporaryDB),
  mapDatabaseFile(apDatabaseFile)
{
	ASSERT(!(PotentialDB && TemporaryDB)); // being both doesn't make sense
	ASSERT(!(ReadOnly && PotentialDB)); // being both doesn't make sense
}

void BackupStoreRefCountDatabaseImpl::Commit()
{
	ASSERT(!mIsTemporaryDB);

	if (!mIsPotentialDB)
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
	if(FileExists(mFinalFilename) && EMU_UNLINK(mFinalFilename.c_str()) != 0)
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
	mIsPotentialDB = false;
}

void BackupStoreRefCountDatabaseImpl::Discard()
{
	if (!mIsTemporaryDB && !mIsPotentialDB)
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

	if(EMU_UNLINK(mFilename.c_str()) != 0)
	{
		THROW_EMU_FILE_ERROR("Failed to delete temporary/potential refcount "
			"database file", mFilename, CommonException,
			OSFileError);
	}

	mIsModified = false;
	mIsTemporaryDB = false;
	mIsPotentialDB = false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabaseImpl::~BackupStoreRefCountDatabase
//		Purpose: Destructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabaseImpl::~BackupStoreRefCountDatabaseImpl()
{
	if (mIsTemporaryDB || mIsPotentialDB)
	{
		// Don't throw exceptions in a destructor.
		if(mIsPotentialDB)
		{
			BOX_ERROR("Potential new BackupStoreRefCountDatabase destroyed "
				"without explicit commit or discard");
		}

		try
		{
			Discard();
		}
		catch(BoxException &e)
		{
			BOX_LOG_SYS_ERROR("Failed to discard BackupStoreRefCountDatabase "
				"in destructor: " << e.what());
		}
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
//		Name:    BackupStoreRefCountDatabase::Create(
//			 const BackupStoreAccountDatabase::Entry& rAccount)
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

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::Create(
//			 const std::string& Filename, int64_t AccountID,
//		         bool reuse_existing_file)
//		Purpose: Create a blank database, using a temporary file that
//			 you must Discard() or Commit() to make permanent.
//		         Be careful with reuse_existing_file, because it
//		         makes it easy to bypass the restriction of only one
//		         (committable) temporary database at a time, and to
//		         accidentally overwrite the main DB.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreRefCountDatabase>
BackupStoreRefCountDatabase::Create(const std::string& Filename, int64_t AccountID,
	bool reuse_existing_file)
{
	// Open the file for writing
	std::string temp_filename = Filename + (reuse_existing_file ? "" : "X");
	int flags = O_CREAT | O_BINARY | O_RDWR | O_EXCL;

	if(FileExists(temp_filename))
	{
		if(reuse_existing_file)
		{
			// Don't warn, and don't fail because the file already exists. This allows
			// creating a temporary file securely with mkstemp() and then opening it as
			// a refcount database, avoiding a race condition.
			flags &= ~O_EXCL;
		}
		else
		{
			BOX_WARNING(BOX_FILE_MESSAGE(temp_filename, "Overwriting existing "
				"temporary reference count database"));
			if (EMU_UNLINK(temp_filename.c_str()) != 0)
			{
				THROW_SYS_FILE_ERROR("Failed to delete old temporary "
					"reference count database file", temp_filename,
					CommonException, OSFileError);
			}
		}
	}

#ifdef BOX_OPEN_LOCK
	flags |= BOX_OPEN_LOCK;
#endif
	std::auto_ptr<FileStream> database_file(new FileStream(temp_filename, flags));

	// Write header
	refcount_StreamFormat hdr;
	hdr.mMagicValue = htonl(REFCOUNT_MAGIC_VALUE);
	hdr.mAccountID = htonl(AccountID);
	database_file->Write(&hdr, sizeof(hdr));

	// Make new object
	BackupStoreRefCountDatabaseImpl* p_impl = new BackupStoreRefCountDatabaseImpl(
		Filename, AccountID,
		false, // ReadOnly
		!reuse_existing_file, // PotentialDB
		reuse_existing_file, // TemporaryDB
		database_file);
	std::auto_ptr<BackupStoreRefCountDatabase> refcount(p_impl);

	// The root directory must always have one reference for a database
	// to be valid, so set that now on the new database. This will leave
	// mIsModified set to true.
	p_impl->SetRefCount(BACKUPSTORE_ROOT_DIRECTORY_ID, 1);

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

std::auto_ptr<FileStream> OpenDatabaseFile(const std::string& Filename, int64_t AccountID,
	bool ReadOnly)
{
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

	return database_file;
}


std::auto_ptr<BackupStoreRefCountDatabase>
BackupStoreRefCountDatabase::Load(const std::string& Filename, int64_t AccountID,
	bool ReadOnly)
{
	// You cannot reopen a temporary database, so it must be the permanent filename,
	// so no need to append an X to it.
	ASSERT(Filename.size() > 0 && Filename[Filename.size() - 1] != 'X');

	std::auto_ptr<FileStream> database_file = OpenDatabaseFile(Filename, AccountID, ReadOnly);
	std::auto_ptr<BackupStoreRefCountDatabase> refcount(
		new BackupStoreRefCountDatabaseImpl(Filename, AccountID, ReadOnly,
			false, // PotentialDB
			false, // TemporaryDB
			database_file));

	// return it to caller
	return refcount;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabaseImpl::Reopen()
//		Purpose: Reopen a previously-opened and then closed refcount
//			 database.
//		Created: 2016/04/25
//
// --------------------------------------------------------------------------
void BackupStoreRefCountDatabaseImpl::Reopen()
{
	ASSERT(!mapDatabaseFile.get());

	// You cannot reopen a temporary database, so it must be the permanent filename,
	// so no need to append an X to it.
	ASSERT(mFilename.size() > 0 && mFilename[mFilename.size() - 1] != 'X');

	mapDatabaseFile = OpenDatabaseFile(mFilename, mAccountID, mReadOnly);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabaseImpl::GetRefCount(int64_t
//			 ObjectID)
//		Purpose: Get the number of references to the specified object
//			 out of the database
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------
refcount_t BackupStoreRefCountDatabaseImpl::GetRefCount(int64_t ObjectID) const
{
	ASSERT(mapDatabaseFile.get());
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

int64_t BackupStoreRefCountDatabaseImpl::GetLastObjectIDUsed() const
{
	return (GetSize() - sizeof(refcount_StreamFormat)) /
		sizeof(refcount_t);
}

void BackupStoreRefCountDatabaseImpl::AddReference(int64_t ObjectID)
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

void BackupStoreRefCountDatabaseImpl::SetRefCount(int64_t ObjectID,
	refcount_t NewRefCount)
{
	ASSERT(mapDatabaseFile.get());
	IOStream::pos_type offset = GetOffset(ObjectID);
	mapDatabaseFile->Seek(offset, SEEK_SET);
	refcount_t RefCountNetOrder = htonl(NewRefCount);
	mapDatabaseFile->Write(&RefCountNetOrder, sizeof(RefCountNetOrder));
	mIsModified = true;
}

bool BackupStoreRefCountDatabaseImpl::RemoveReference(int64_t ObjectID)
{
	refcount_t refcount = GetRefCount(ObjectID); // must exist in database
	ASSERT(refcount > 0);
	refcount--;
	SetRefCount(ObjectID, refcount);
	return (refcount > 0);
}

int BackupStoreRefCountDatabaseImpl::ReportChangesTo(BackupStoreRefCountDatabase& rOldRefs)
{
	int ErrorCount = 0;
	int64_t MaxOldObjectId = rOldRefs.GetLastObjectIDUsed();
	int64_t MaxNewObjectId = GetLastObjectIDUsed();

	for (int64_t ObjectID = BACKUPSTORE_ROOT_DIRECTORY_ID;
		ObjectID < std::max(MaxOldObjectId, MaxNewObjectId);
		ObjectID++)
	{
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
