// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientInodeToIDMap.cpp
//		Purpose: Map of inode numbers to file IDs on the store
//		Created: 11/11/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <depot.h>

#define BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION
#include "BackupClientInodeToIDMap.h"
#undef BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION

#include "Archive.h"
#include "BackupStoreException.h"
#include "CollectInBufferStream.h"
#include "MemBlockStream.h"
#include "autogen_CommonException.h"

#include "MemLeakFindOn.h"

#define BOX_DBM_INODE_DB_VERSION_KEY "BackupClientInodeToIDMap.Version"
#define BOX_DBM_INODE_DB_VERSION_CURRENT 2

#define BOX_DBM_MESSAGE(stuff) stuff << " (qdbm): " << dperrmsg(dpecode)

#define BOX_LOG_DBM_ERROR(stuff) \
	BOX_ERROR(BOX_DBM_MESSAGE(stuff))

#define THROW_DBM_ERROR(message, filename, exception, subtype) \
	BOX_LOG_DBM_ERROR(message << ": " << filename); \
	THROW_EXCEPTION_MESSAGE(exception, subtype, \
		BOX_DBM_MESSAGE(message << ": " << filename));

#define ASSERT_DBM_OK(operation, message, filename, exception, subtype) \
	if(!(operation)) \
	{ \
		THROW_DBM_ERROR(message, filename, exception, subtype); \
	}

#define ASSERT_DBM_OPEN() \
	if(mpDepot == 0) \
	{ \
		THROW_EXCEPTION_MESSAGE(BackupStoreException, InodeMapNotOpen, \
			"Inode database not open"); \
	}

#define ASSERT_DBM_CLOSED() \
	if(mpDepot != 0) \
	{ \
		THROW_EXCEPTION_MESSAGE(CommonException, Internal, \
			"Inode database already open: " << mFilename); \
	} 

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientInodeToIDMap::BackupClientInodeToIDMap()
//		Purpose: Constructor
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
BackupClientInodeToIDMap::BackupClientInodeToIDMap()
	: mReadOnly(true),
	  mEmpty(false),
	  mpDepot(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientInodeToIDMap::~BackupClientInodeToIDMap()
//		Purpose: Destructor
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
BackupClientInodeToIDMap::~BackupClientInodeToIDMap()
{
	if(mpDepot != 0)
	{
		Close();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientInodeToIDMap::Open(const char *, bool, bool)
//		Purpose: Open the database map, creating a file on disc to store everything
//		Created: 20/11/03
//
// --------------------------------------------------------------------------
void BackupClientInodeToIDMap::Open(const char *Filename, bool ReadOnly,
	bool CreateNew)
{
	mFilename = Filename;

	// Correct arguments?
	ASSERT(!(CreateNew && ReadOnly));
	
	// Correct usage?
	ASSERT_DBM_CLOSED();
	ASSERT(!mEmpty);
	
	// Open the database file
	int mode = ReadOnly ? DP_OREADER : DP_OWRITER;
	if(CreateNew)
	{
		mode |= DP_OCREAT;
	}
	
	mpDepot = dpopen(Filename, mode, 0);
	
	if(!mpDepot)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, BerkelyDBFailure,
			BOX_DBM_MESSAGE("Failed to open inode database: " <<
				mFilename));
	}

	const char* version_key = BOX_DBM_INODE_DB_VERSION_KEY;
	int32_t version = 0;

	if(CreateNew)
	{
		version = BOX_DBM_INODE_DB_VERSION_CURRENT;

		int ret = dpput(mpDepot, version_key, strlen(version_key),
			(char *)(&version), sizeof(version), DP_DKEEP);

		if(!ret)
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException, BerkelyDBFailure,
				BOX_DBM_MESSAGE("Failed to write version number to inode "
					"database: " << mFilename));
		}
	}
	else
	{
		int ret = dpgetwb(mpDepot, version_key, strlen(version_key), 0,
			sizeof(version), (char *)(&version));

		if(ret == -1)
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException, BerkelyDBFailure,
				"Missing version number in inode database. Perhaps it "
				"needs to be recreated: " << mFilename);
		}

		if(ret != sizeof(version))
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException, BerkelyDBFailure,
				"Wrong size version number in inode database: expected "
				<< sizeof(version) << " bytes but found " << ret);
		}

		if(version != BOX_DBM_INODE_DB_VERSION_CURRENT)
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException, BerkelyDBFailure,
				"Wrong version number in inode database: expected " <<
				BOX_DBM_INODE_DB_VERSION_CURRENT << " but found " <<
				version << ". Perhaps it needs to be recreated: " <<
				mFilename);
		}

		// By this point the version number has been checked and is OK.
	}
	
	// Read only flag
	mReadOnly = ReadOnly;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientInodeToIDMap::OpenEmpty()
//		Purpose: 'Open' this map. Not associated with a disc file.
//			 Useful for when a map is required, but is against
//			 an empty file on disc which shouldn't be created.
//			 Implies read only.
//		Created: 20/11/03
//
// --------------------------------------------------------------------------
void BackupClientInodeToIDMap::OpenEmpty()
{
	ASSERT_DBM_CLOSED();
	ASSERT(mpDepot == 0);
	mEmpty = true;
	mReadOnly = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientInodeToIDMap::Close()
//		Purpose: Close the database file
//		Created: 20/11/03
//
// --------------------------------------------------------------------------
void BackupClientInodeToIDMap::Close()
{
	ASSERT_DBM_OPEN();
	ASSERT_DBM_OK(dpclose(mpDepot), "Failed to close inode database",
		mFilename, BackupStoreException, BerkelyDBFailure);
	mpDepot = 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientInodeToIDMap::AddToMap(InodeRefType,
//			 int64_t, int64_t)
//		Purpose: Adds an entry to the map. Overwrites any existing
//			 entry.
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
void BackupClientInodeToIDMap::AddToMap(InodeRefType InodeRef, int64_t ObjectID,
	int64_t InDirectory, const std::string& LocalPath)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, InodeMapIsReadOnly);
	}

	if(mpDepot == 0)
	{
		THROW_EXCEPTION(BackupStoreException, InodeMapNotOpen);
	}

	ASSERT_DBM_OPEN();

	// Setup structures
	CollectInBufferStream buf;
	Archive arc(buf, IOStream::TimeOutInfinite);
	arc.WriteExact((uint64_t)ObjectID);
	arc.WriteExact((uint64_t)InDirectory);
	arc.Write(LocalPath);
	buf.SetForReading();

	ASSERT_DBM_OK(dpput(mpDepot, (const char *)&InodeRef, sizeof(InodeRef),
		(const char *)buf.GetBuffer(), buf.GetSize(), DP_DOVER),
		"Failed to add record to inode database", mFilename,
		BackupStoreException, BerkelyDBFailure);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientInodeToIDMap::Lookup(InodeRefType,
//			 int64_t &, int64_t &) const
//		Purpose: Looks up an inode in the map, returning true if it
//			 exists, and the object ids of it and the directory
//			 it's in the reference arguments.
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
bool BackupClientInodeToIDMap::Lookup(InodeRefType InodeRef, int64_t &rObjectIDOut,
	int64_t &rInDirectoryOut, std::string* pLocalPathOut) const
{
	if(mEmpty)
	{
		// Map is empty
		return false;
	}

	if(mpDepot == 0)
	{
		THROW_EXCEPTION(BackupStoreException, InodeMapNotOpen);
	}
	
	ASSERT_DBM_OPEN();
	int size;
	char* data = dpget(mpDepot, (const char *)&InodeRef, sizeof(InodeRef),
		0, -1, &size);
	if(data == NULL)
	{
		// key not in file
		return false;
	}

	// Free data automatically when the guard goes out of scope.
	MemoryBlockGuard<char *> guard(data);
	MemBlockStream stream(data, size);
	Archive arc(stream, IOStream::TimeOutInfinite);

	// Return data
	try
	{
		arc.Read(rObjectIDOut);
		arc.Read(rInDirectoryOut);
		if(pLocalPathOut)
		{
			arc.Read(*pLocalPathOut);
		}
	}
	catch(CommonException &e)
	{
		if(e.GetSubType() == CommonException::ArchiveBlockIncompleteRead)
		{
			THROW_FILE_ERROR("Failed to lookup record in inode database: "
				<< InodeRef << ": not enough data in record", mFilename,
				BackupStoreException, BerkelyDBFailure);
			// Need to throw precisely that exception to ensure that the
			// invalid database is deleted, so that we don't hit the same
			// error next time.
		}

		throw;
	}

	// Found
	return true;
}
