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
public:
	~BackupStoreRefCountDatabase();
private:
	// Creation through static functions only
	BackupStoreRefCountDatabase(const BackupStoreAccountDatabase::Entry&
		rAccount);
	// No copying allowed
	BackupStoreRefCountDatabase(const BackupStoreRefCountDatabase &);
	
public:
	// Create a new database for a new account. This method will refuse
	// to overwrite any existing file.
	static void CreateNew(const BackupStoreAccountDatabase::Entry& rAccount)
	{
		Create(rAccount, false);
	}
	
	// Load it from the store
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(const
		BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly);
	
	// Data access functions
	int32_t GetRefCount(int64_t ObjectID) const;
	int64_t GetLastObjectIDUsed() const;
	
	// Data modification functions
	void AddReference(int64_t ObjectID);
	bool RemoveReference(int64_t ObjectID);

private:
	// Create a new database for an existing account. Used during
	// account checking if opening the old database throws an exception.
	// This method will overwrite any existing file.
	static void CreateForRegeneration(const
		BackupStoreAccountDatabase::Entry& rAccount)
	{
		Create(rAccount, true);
	}

	static void Create(const BackupStoreAccountDatabase::Entry& rAccount,
		bool AllowOverwrite);

	typedef int32_t refcount_t;

	static std::string GetFilename(const BackupStoreAccountDatabase::Entry&
		rAccount);
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
	BackupStoreAccountDatabase::Entry mAccount;
	std::string mFilename;
	bool mReadOnly;
	bool mIsModified;
	std::auto_ptr<FileStream> mapDatabaseFile;
};

#endif // BACKUPSTOREREFCOUNTDATABASE__H
