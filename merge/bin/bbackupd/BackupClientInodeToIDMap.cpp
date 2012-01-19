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

#include "BackupStoreException.h"

#include "MemLeakFindOn.h"

typedef struct
{
	int64_t mObjectID;
	int64_t mInDirectory;
} IDBRecord;

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
	
	ASSERT_DBM_OK(mpDepot, "Failed to open inode database", mFilename,
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
	int64_t InDirectory)
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
	IDBRecord rec;
	rec.mObjectID = ObjectID;
	rec.mInDirectory = InDirectory;

	ASSERT_DBM_OK(dpput(mpDepot, (const char *)&InodeRef, sizeof(InodeRef),
		(const char *)&rec, sizeof(rec), DP_DOVER),
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
bool BackupClientInodeToIDMap::Lookup(InodeRefType InodeRef,
	int64_t &rObjectIDOut, int64_t &rInDirectoryOut) const
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

	IDBRecord rec;
	
	if(dpgetwb(mpDepot, (const char *)&InodeRef, sizeof(InodeRef),
		0, sizeof(IDBRecord), (char *)&rec) == -1)
	{
		// key not in file
		return false;
	}
		
	// Return data
	rObjectIDOut = rec.mObjectID;
	rInDirectoryOut = rec.mInDirectory;

	// Found
	return true;
}
