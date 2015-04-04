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

#define _PUBLIC_
#include "tdb.h"

#define BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION
#include "BackupClientInodeToIDMap.h"
#undef BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION

#include "BackupStoreException.h"

#include "MemLeakFindOn.h"

typedef struct
{
	int64_t mObjectID;
	int64_t mInDirectory;
} IDBRecord;

#define BOX_DBM_MESSAGE(stuff) stuff << " (tdb): " << tdb_error(mpContext)

#define BOX_LOG_DBM_ERROR(stuff) \
	BOX_ERROR(BOX_DBM_MESSAGE(stuff))

#define THROW_DBM_ERROR(message, filename, exception, subtype) \
	BOX_LOG_DBM_ERROR(message << ": " << filename); \
	THROW_EXCEPTION_MESSAGE(exception, subtype, \
		BOX_DBM_MESSAGE(message << ": " << filename));

#define ASSERT_DBM(success, message, exception, subtype) \
	if(!(success)) \
	{ \
		THROW_DBM_ERROR(message, mFilename, exception, subtype); \
	}

#define ASSERT_DBM_OPEN() \
	if(mpContext == 0) \
	{ \
		THROW_EXCEPTION_MESSAGE(BackupStoreException, InodeMapNotOpen, \
			"Inode database not open"); \
	}

#define ASSERT_DBM_CLOSED() \
	if(mpContext != 0) \
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
	  mpContext(NULL)
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
	if(mpContext != NULL)
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
	int mode = ReadOnly ? O_RDONLY : O_RDWR;
	if(CreateNew)
	{
		mode |= O_CREAT;
	}
	
	mpContext = tdb_open(Filename, 0, 0, mode, 0700);
	
	ASSERT_DBM(mpContext != NULL, "Failed to open inode database",
		BackupStoreException, BerkelyDBFailure);
	
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
	ASSERT(mpContext == NULL);
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
	ASSERT_DBM(tdb_close(mpContext) == 0, "Failed to close inode database",
		BackupStoreException, BerkelyDBFailure);
	mpContext = NULL;
}

static TDB_DATA GetDatum(void* dptr, size_t dsize)
{
	TDB_DATA datum;
	datum.dptr = (unsigned char *)dptr;
	datum.dsize = dsize;
	return datum;
}

#define GET_STRUCT_DATUM(structure) \
	GetDatum(&structure, sizeof(structure))

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
	int64_t InDirectory)
{
	if(mReadOnly)
	{
		THROW_EXCEPTION(BackupStoreException, InodeMapIsReadOnly);
	}

	if(mpContext == 0)
	{
		THROW_EXCEPTION(BackupStoreException, InodeMapNotOpen);
	}

	ASSERT_DBM_OPEN();

	// Setup structures
	IDBRecord rec;
	rec.mObjectID = ObjectID;
	rec.mInDirectory = InDirectory;

	ASSERT_DBM(tdb_store(mpContext, GET_STRUCT_DATUM(InodeRef),
		GET_STRUCT_DATUM(rec), 0) == 0,
		"Failed to add record to inode database",
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
bool BackupClientInodeToIDMap::Lookup(InodeRefType InodeRef,
	int64_t &rObjectIDOut, int64_t &rInDirectoryOut) const
{
	if(mEmpty)
	{
		// Map is empty
		return false;
	}

	if(mpContext == 0)
	{
		THROW_EXCEPTION(BackupStoreException, InodeMapNotOpen);
	}
	
	ASSERT_DBM_OPEN();

	TDB_DATA datum = tdb_fetch(mpContext, GET_STRUCT_DATUM(InodeRef));
	if(datum.dptr == NULL)
	{
		// key not in file
		return false;
	}

	IDBRecord rec;
	if(datum.dsize != sizeof(rec))
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Failed to get inode database entry: "
			"record has wrong size: expected " <<
			sizeof(rec) << " but was " << datum.dsize <<
			" in " << mFilename);
	}
		
	rec = *(IDBRecord *)datum.dptr;
	free(datum.dptr);
	
	// Return data
	rObjectIDOut = rec.mObjectID;
	rInDirectoryOut = rec.mInDirectory;

	// Found
	return true;
}
