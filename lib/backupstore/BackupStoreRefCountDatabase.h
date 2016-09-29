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
//		Purpose: Abstract interface for an object reference count
//			 database.
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------
class BackupStoreRefCountDatabase
{
	friend class BackupStoreCheck;
	friend class BackupStoreContext;
	friend class HousekeepStoreAccount;

private:
	// No copying allowed
	BackupStoreRefCountDatabase(const BackupStoreRefCountDatabase &);

protected:
	// Protected constructor which does nothing, to allow concrete implementations
	// to initialise themselves.
	BackupStoreRefCountDatabase() { }

public:
	virtual ~BackupStoreRefCountDatabase() { }

	// Create a blank database, using a temporary file that you must
	// Discard() or Commit() to make permanent.
	virtual void Commit() = 0;
	virtual void Discard() = 0;
	virtual void Close() = 0;

	// I'm not sure that Reopen() is a good idea, but it's part of BackupFileSystem's
	// API that it manages the lifetime of two BackupStoreRefCountDatabases, and
	// Commit() changes the temporary DB to permanent, and it should not be
	// invalidated by this, so we need a way to reopen it to make the existing object
	// usable again.
	virtual void Reopen() = 0;
	virtual bool IsReadOnly() = 0;

	// These static methods actually create instances of
	// BackupStoreRefCountDatabaseImpl.

	// Create a new empty database:
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const BackupStoreAccountDatabase::Entry& rAccount);
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const std::string& Filename, int64_t AccountID);
	// Load it from the store
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(
		const BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly);
	// Load it from a stream (file or RaidFile)
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(
		const std::string& FileName, int64_t AccountID, bool ReadOnly);
	static std::string GetFilename(const BackupStoreAccountDatabase::Entry&
		rAccount);

	typedef uint32_t refcount_t;

	// Data access functions
	virtual refcount_t GetRefCount(int64_t ObjectID) const = 0;
	virtual int64_t GetLastObjectIDUsed() const = 0;

	// Data modification functions
	virtual void AddReference(int64_t ObjectID) = 0;
	// RemoveReference returns false if refcount drops to zero
	virtual bool RemoveReference(int64_t ObjectID) = 0;
	virtual int ReportChangesTo(BackupStoreRefCountDatabase& rOldRefs) = 0;
};

#endif // BACKUPSTOREREFCOUNTDATABASE__H
