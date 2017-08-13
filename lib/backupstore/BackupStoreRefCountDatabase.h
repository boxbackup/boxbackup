// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreRefCountDatabase.h
//		Purpose: Main backup store information storage
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREREFCOUNTDATABASE__H
#define BACKUPSTOREREFCOUNTDATABASE__H

#include <memory>
#include <string>
#include <vector>

#include "BackupStoreAccountDatabase.h"
#include "BackupStoreConstants.h"
#include "FileStream.h"

class BackupStoreCheck;
class BackupStoreContext;

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
//		Name:    BackupStoreRefCountDatabase
//		Purpose: Backup store reference count database storage
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------
class BackupStoreRefCountDatabase
{
	friend class BackupStoreCheck;
	friend class BackupStoreContext;
	friend class HousekeepStoreAccount;

public:
	~BackupStoreRefCountDatabase();
private:
	// Creation through static functions only
	BackupStoreRefCountDatabase(const std::string& Filename, int64_t AccountID,
		bool ReadOnly, bool PotentialDB, bool TemporaryDB,
		std::auto_ptr<FileStream> apDatabaseFile);
	// No copying allowed
	BackupStoreRefCountDatabase(const BackupStoreRefCountDatabase &);

public:
	// Create a blank database, using a temporary file that you must
	// Discard() or Commit() to make permanent.
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const BackupStoreAccountDatabase::Entry& rAccount);
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const std::string& Filename, int64_t AccountID, bool reuse_existing_file = false);
	void Commit();
	void Discard();

	// Load it from the store
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(const
		BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly);

	typedef uint32_t refcount_t;

	// Data access functions
	refcount_t GetRefCount(int64_t ObjectID) const;
	int64_t GetLastObjectIDUsed() const;

	// Data modification functions
	void AddReference(int64_t ObjectID);
	// RemoveReference returns false if refcount drops to zero
	bool RemoveReference(int64_t ObjectID);
	int ReportChangesTo(BackupStoreRefCountDatabase& rOldRefs);

private:
	static std::string GetFilename(const BackupStoreAccountDatabase::Entry& rAccount);

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
	void SetRefCount(int64_t ObjectID, refcount_t NewRefCount);
	
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

#endif // BACKUPSTOREREFCOUNTDATABASE__H
