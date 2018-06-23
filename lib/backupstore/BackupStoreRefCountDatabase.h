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

	// Three routes to close a refcount DB. Choose only one:
	virtual void Commit() = 0; // for potential DBs only
	virtual void Discard() = 0; // for potential and temporary DBs
	virtual void Close() = 0; // for permanent DBs only

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
	// Create a blank database, which is temporary if reuse_existing_file is true, and can only
	// be Discard()ed, or potential if !reuse_existing_file (you can Discard() or Commit() it,
	// the latter of which makes it permanent):
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const std::string& Filename, int64_t AccountID, bool reuse_existing_file = false);
	// Load it from the store
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(
		const BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly);
	// Open an existing, permanent refcount DB stored in a file:
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
