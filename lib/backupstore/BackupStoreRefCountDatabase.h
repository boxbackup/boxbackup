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
#include "BackupStoreException.h"
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
	uint32_t mMagicValue; // also the version number
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
	BackupStoreRefCountDatabase(const BackupStoreAccountDatabase::Entry&
		rAccount, bool ReadOnly, bool Temporary,
		std::auto_ptr<FileStream> apDatabaseFile);
	// No copying allowed
	BackupStoreRefCountDatabase(const BackupStoreRefCountDatabase &);

public:
	typedef uint32_t refcount_t;

	class EntryData {
		friend class BackupStoreRefCountDatabase;

	protected:
		int16_t mFlags;
		refcount_t mRefCount;
		uint64_t mSizeInBlocks;
		uint64_t mDependsNewer;
		uint64_t mDependsOlder;
	public:
		EntryData()
		: mFlags(0),
		  mRefCount(0),
		  mSizeInBlocks(0),
		  mDependsNewer(0),
		  mDependsOlder(0)
		{ }

		EntryData(const EntryData& data)
		: mFlags(data.mFlags),
		  mRefCount(data.mRefCount),
		  mSizeInBlocks(data.mSizeInBlocks),
		  mDependsNewer(data.mDependsNewer),
		  mDependsOlder(data.mDependsOlder)
		{ }
	};

	class Entry : public EntryData {
		friend class BackupStoreRefCountDatabase;

	private:
		int64_t mObjectID;

	public:
		Entry(int64_t ObjectID)
		: EntryData(),
		  mObjectID(ObjectID)
		{ }

		Entry(int64_t ObjectID, const EntryData& data)
		: EntryData(data),
		  mObjectID(ObjectID)
		{ }

		// Data access functions
		refcount_t GetRefCount() const { return mRefCount; }

		// Data modification functions
		refcount_t AddReference()
		{
			ASSERT(mRefCount >= 0);
			return ++mRefCount;
		}
		refcount_t RemoveReference()
		{
			ASSERT(mRefCount > 0);
			return --mRefCount;
		}
	private:
		void SetRefCount(refcount_t NewRefCount)
		{
			mRefCount = NewRefCount;
		}
	public:
		// Make sure these flags are synced with those in backupprocotol.txt
		// ListDirectory command
		enum
		{
			Flags_INCLUDE_EVERYTHING 	= -1,
			Flags_EXCLUDE_NOTHING 		= 0,
			Flags_EXCLUDE_EVERYTHING	= 31,	// make sure this is kept as sum of ones below!
			Flags_File					= 1,
			Flags_Dir					= 2,
			Flags_Deleted				= 4,
			Flags_OldVersion			= 8,
			Flags_RemoveASAP			= 16	// if this flag is set, housekeeping will remove it as it is marked Deleted or OldVersion
		};

		void AssertMutable()
		{
			// No need to check parent directories, because other
			// assertions in BackupStoreContext stop us modifying
			// anything in a directory that has multiple references.

			if(mRefCount != 1)
			{
				THROW_EXCEPTION_MESSAGE(BackupStoreException,
					MultiplyReferencedObject,
					"Refusing to change flags of multiply "
					"referenced object " <<
					BOX_FORMAT_OBJECTID(mObjectID));
			}
		}

		int64_t GetSizeInBlocks() const
		{
			return mSizeInBlocks;
		}
		void SetSizeInBlocks(int64_t SizeInBlocks)
		{
			mSizeInBlocks = SizeInBlocks;
		}
		void AddFlags(int16_t Flags)
		{
			if((mFlags & Flags) != Flags)
			{
				// TODO FIXME: should be allowed to change old
				// flag even on immutable files, for conversion
				// to patch.
				AssertMutable();
				mFlags |= Flags;
			}
		}
		void RemoveFlags(int16_t Flags)
		{
			if((mFlags & ~Flags) != Flags)
			{
				// TODO FIXME: should be allowed to change old
				// flag even on immutable files, for conversion
				// to patch.
				AssertMutable();
				mFlags &= ~Flags;
			}
		}
		int16_t GetFlags() const
		{
			return mFlags;
		}

		// convenience methods
		bool IsDir()
		{
			return GetFlags() & Flags_Dir;
		}
		bool IsFile()
		{
			return GetFlags() & Flags_File;
		}
		bool inline IsOld()
		{
			return GetFlags() & Flags_OldVersion;
		}
		bool inline IsDeleted()
		{
			return GetFlags() & Flags_Deleted;
		}
		bool inline MatchesFlags(int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet)
		{
			return ((FlagsMustBeSet == Flags_INCLUDE_EVERYTHING) || ((mFlags & FlagsMustBeSet) == FlagsMustBeSet))
				&& ((mFlags & FlagsNotToBeSet) == 0);
		};

		// Get dependency info
		// new version this depends on
		int64_t GetDependsNewer() const
		{
			return mDependsNewer;
		}
		void SetDependsNewer(int64_t ObjectID)
		{
			mDependsNewer = ObjectID;
		}
		// older version which depends on this
		int64_t GetDependsOlder() const
		{
			return mDependsOlder;
		}
		void SetDependsOlder(int64_t ObjectID)
		{
			mDependsOlder = ObjectID;
		}
		// Dependency info saving
		bool HasDependencies()
		{
			return mDependsNewer != 0 || mDependsOlder != 0;
		}
		void ReadFromStreamDependencyInfo(IOStream &rStream, int Timeout);
		void WriteToStreamDependencyInfo(IOStream &rStream, int Timeout) const;
	};

	// Create a blank database, using a temporary file that you must
	// Discard() or Commit() to make permanent.
	static std::auto_ptr<BackupStoreRefCountDatabase> Create
		(const BackupStoreAccountDatabase::Entry& rAccount);
	void Commit();
	void Discard();

	// Load it from the store
	static std::auto_ptr<BackupStoreRefCountDatabase> Load(const
		BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly);

	// Data access functions
	refcount_t GetRefCount(int64_t ObjectID)
	{
		return GetEntry(ObjectID).GetRefCount();
	}
	int64_t GetLastObjectIDUsed() const;
	Entry GetEntry(int64_t ObjectID)
	{
		// Returns a copy, therefore cache-safe and const.
		return Entry(ObjectID, GetEntryData(ObjectID));
	}
	void PutEntry(const Entry& entry)
	{
		PutEntryData(entry.mObjectID, entry);
	}

	// Data modification functions
	refcount_t AddReference(int64_t ObjectID);
	refcount_t RemoveReference(int64_t ObjectID);
	int ReportChangesTo(BackupStoreRefCountDatabase& rOldRefs);

protected:
	// Raw data read/write functions
	const EntryData GetEntryData(int64_t ObjectID);
	void PutEntryData(int64_t ObjectID, const EntryData& data);

private:
	static std::string GetFilename(const BackupStoreAccountDatabase::Entry&
		rAccount, bool Temporary);

	IOStream::pos_type GetSize() const
	{
		return mapDatabaseFile->GetPosition() +
			mapDatabaseFile->BytesLeftToRead();
	}
	IOStream::pos_type GetEntrySize() const
	{
		return sizeof(EntryData);
	}
	IOStream::pos_type GetOffset(int64_t ObjectID) const
	{
		return ((ObjectID - 1) * GetEntrySize()) +
			sizeof(refcount_StreamFormat);
	}
	
	// Location information
	BackupStoreAccountDatabase::Entry mAccount;
	std::string mFilename;
	bool mReadOnly;
	bool mIsModified;
	bool mIsTemporaryFile;
	std::auto_ptr<FileStream> mapDatabaseFile;

	bool NeedsCommitOrDiscard()
	{
		return mapDatabaseFile.get() && mIsModified && mIsTemporaryFile;
	}
};

#endif // BACKUPSTOREREFCOUNTDATABASE__H
