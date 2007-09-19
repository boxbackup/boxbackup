// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDirectory.h
//		Purpose: Representation of a backup directory
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>

#include "BackupStoreDirectory.h"
#include "IOStream.h"
#include "BackupStoreException.h"
#include "BackupStoreObjectMagic.h"

#include "MemLeakFindOn.h"

// set packing to one byte
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

typedef struct
{
	int32_t mMagicValue;	// also the version number
	int32_t mNumEntries;
	int64_t mObjectID;		// this object ID
	int64_t mContainerID;	// ID of container
	uint64_t mAttributesModTime;
	int32_t mOptionsPresent;	// bit mask of optional sections / features present
	// Then a StreamableMemBlock for attributes
} dir_StreamFormat;

typedef enum
{
	Option_DependencyInfoPresent = 1
} dir_StreamFormatOptions;

typedef struct
{
	uint64_t mModificationTime;
	int64_t mObjectID;
	int64_t mSizeInBlocks;
	uint64_t mAttributesHash;
	int16_t mFlags;				// order smaller items after bigger ones (for alignment)
	// Then a BackupStoreFilename
	// Then a StreamableMemBlock for attributes
} en_StreamFormat;

typedef struct
{
	int64_t mDependsNewer;
	int64_t mDependsOlder;
} en_StreamFormatDepends;

// Use default packing
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::BackupStoreDirectory()
//		Purpose: Constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::BackupStoreDirectory()
	: mRevisionID(0), mObjectID(0), mContainerID(0), mAttributesModTime(0), mUserInfo1(0)
{
	ASSERT(sizeof(u_int64_t) == sizeof(box_time_t));
}


// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDirectory::BackupStoreDirectory(int64_t, int64_t)
//		Purpose: Constructor giving object and container IDs
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreDirectory::BackupStoreDirectory(int64_t ObjectID, int64_t ContainerID)
	: mRevisionID(0), mObjectID(ObjectID), mContainerID(ContainerID), mAttributesModTime(0), mUserInfo1(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::~BackupStoreDirectory()
//		Purpose: Destructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::~BackupStoreDirectory()
{
	for(std::vector<Entry*>::iterator i(mEntries.begin()); i != mEntries.end(); ++i)
	{
		delete (*i);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::ReadFromStream(IOStream &, int)
//		Purpose: Reads the directory contents from a stream. Exceptions will yeild incomplete reads.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::ReadFromStream(IOStream &rStream, int Timeout)
{
	// Get the header
	dir_StreamFormat hdr;
	if(!rStream.ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */, Timeout))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Check magic value...
	if(OBJECTMAGIC_DIR_MAGIC_VALUE != ntohl(hdr.mMagicValue))
	{
		THROW_EXCEPTION(BackupStoreException, BadDirectoryFormat)
	}
	
	// Get data
	mObjectID = box_ntoh64(hdr.mObjectID);
	mContainerID = box_ntoh64(hdr.mContainerID);
	mAttributesModTime = box_ntoh64(hdr.mAttributesModTime);
	
	// Options
	int32_t options = ntohl(hdr.mOptionsPresent);
	
	// Get attributes
	mAttributes.ReadFromStream(rStream, Timeout);
	
	// Decode count
	int count = ntohl(hdr.mNumEntries);
	
	// Clear existing list
	for(std::vector<Entry*>::iterator i = mEntries.begin(); 
		i != mEntries.end(); i++)
	{
		delete (*i);
	}
	mEntries.clear();
	
	// Read them in!
	for(int c = 0; c < count; ++c)
	{
		Entry *pen = new Entry;
		try
		{
			// Read from stream
			pen->ReadFromStream(rStream, Timeout);
			
			// Add to list
			mEntries.push_back(pen);
		}
		catch(...)
		{
			delete pen;
			throw;
		}
	}
	
	// Read in dependency info?
	if(options & Option_DependencyInfoPresent)
	{
		// Read in extra dependency data
		for(int c = 0; c < count; ++c)
		{
			mEntries[c]->ReadFromStreamDependencyInfo(rStream, Timeout);
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::WriteToStream(IOStream &, int16_t, int16_t, bool, bool)
//		Purpose: Writes a selection of entries to a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::WriteToStream(IOStream &rStream, int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet, bool StreamAttributes, bool StreamDependencyInfo) const
{
	// Get count of entries
	int32_t count = mEntries.size();
	if(FlagsMustBeSet != Entry::Flags_INCLUDE_EVERYTHING || FlagsNotToBeSet != Entry::Flags_EXCLUDE_NOTHING)
	{
		// Need to count the entries
		count = 0;
		Iterator i(*this);
		while(i.Next(FlagsMustBeSet, FlagsNotToBeSet) != 0)
		{
			count++;
		}
	}
	
	// Check that sensible IDs have been set
	ASSERT(mObjectID != 0);
	ASSERT(mContainerID != 0);
	
	// Need dependency info?
	bool dependencyInfoRequired = false;
	if(StreamDependencyInfo)
	{
		Iterator i(*this);
		Entry *pen = 0;
		while((pen = i.Next(FlagsMustBeSet, FlagsNotToBeSet)) != 0)
		{
			if(pen->HasDependencies())
			{
				dependencyInfoRequired = true;
			}
		}	
	}
	
	// Options
	int32_t options = 0;
	if(dependencyInfoRequired) options |= Option_DependencyInfoPresent;

	// Build header
	dir_StreamFormat hdr;
	hdr.mMagicValue = htonl(OBJECTMAGIC_DIR_MAGIC_VALUE);
	hdr.mNumEntries = htonl(count);
	hdr.mObjectID = box_hton64(mObjectID);
	hdr.mContainerID = box_hton64(mContainerID);
	hdr.mAttributesModTime = box_hton64(mAttributesModTime);
	hdr.mOptionsPresent = htonl(options);
	
	// Write header
	rStream.Write(&hdr, sizeof(hdr));
	
	// Write the attributes?
	if(StreamAttributes)
	{
		mAttributes.WriteToStream(rStream);
	}
	else
	{
		// Write a blank header instead
		StreamableMemBlock::WriteEmptyBlockToStream(rStream);
	}

	// Then write all the entries
	Iterator i(*this);
	Entry *pen = 0;
	while((pen = i.Next(FlagsMustBeSet, FlagsNotToBeSet)) != 0)
	{
		pen->WriteToStream(rStream);
	}
	
	// Write dependency info?
	if(dependencyInfoRequired)
	{
		Iterator i(*this);
		Entry *pen = 0;
		while((pen = i.Next(FlagsMustBeSet, FlagsNotToBeSet)) != 0)
		{
			pen->WriteToStreamDependencyInfo(rStream);
		}	
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::AddEntry(const Entry &)
//		Purpose: Adds entry to directory (no checking)
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry *BackupStoreDirectory::AddEntry(const Entry &rEntryToCopy)
{
	Entry *pnew = new Entry(rEntryToCopy);
	try
	{
		mEntries.push_back(pnew);
	}
	catch(...)
	{
		delete pnew;
		throw;
	}
	
	return pnew;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::AddEntry(const BackupStoreFilename &, int64_t, int64_t, int16_t)
//		Purpose: Adds entry to directory (no checking)
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry *BackupStoreDirectory::AddEntry(const BackupStoreFilename &rName, box_time_t ModificationTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags, box_time_t AttributesModTime)
{
	Entry *pnew = new Entry(rName, ModificationTime, ObjectID, SizeInBlocks, Flags, AttributesModTime);
	try
	{
		mEntries.push_back(pnew);
	}
	catch(...)
	{
		delete pnew;
		throw;
	}
	
	return pnew;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::DeleteEntry(int64_t)
//		Purpose: Deletes entry with given object ID (uses linear search, maybe a little inefficient)
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::DeleteEntry(int64_t ObjectID)
{
	for(std::vector<Entry*>::iterator i(mEntries.begin());
		i != mEntries.end(); ++i)
	{
		if((*i)->mObjectID == ObjectID)
		{
			// Delete
			delete (*i);
			// Remove from list
			mEntries.erase(i);
			// Done
			return;
		}
	}
	
	// Not found
	THROW_EXCEPTION(BackupStoreException, CouldNotFindEntryInDirectory)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::FindEntryByID(int64_t)
//		Purpose: Finds a specific entry. Returns 0 if the entry doesn't exist.
//		Created: 12/11/03
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry *BackupStoreDirectory::FindEntryByID(int64_t ObjectID) const
{
	for(std::vector<Entry*>::const_iterator i(mEntries.begin());
		i != mEntries.end(); ++i)
	{
		if((*i)->mObjectID == ObjectID)
		{
			// Found
			return (*i);
		}
	}

	// Not found
	return 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::Entry()
//		Purpose: Constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::Entry()
	: mModificationTime(0),
	  mObjectID(0),
	  mSizeInBlocks(0),
	  mFlags(0),
	  mAttributesHash(0),
	  mMinMarkNumber(0),
	  mMarkNumber(0),
	  mDependsNewer(0),
	  mDependsOlder(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::~Entry()
//		Purpose: Destructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::~Entry()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::Entry(const Entry &)
//		Purpose: Copy constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::Entry(const Entry &rToCopy)
	: mName(rToCopy.mName),
	  mModificationTime(rToCopy.mModificationTime),
	  mObjectID(rToCopy.mObjectID),
	  mSizeInBlocks(rToCopy.mSizeInBlocks),
	  mFlags(rToCopy.mFlags),
	  mAttributesHash(rToCopy.mAttributesHash),
	  mAttributes(rToCopy.mAttributes),
	  mMinMarkNumber(rToCopy.mMinMarkNumber),
	  mMarkNumber(rToCopy.mMarkNumber),
	  mDependsNewer(rToCopy.mDependsNewer),
	  mDependsOlder(rToCopy.mDependsOlder)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::Entry(const BackupStoreFilename &, int64_t, int64_t, int16_t)
//		Purpose: Constructor from values
//		Created: 2003/08/27
//
// --------------------------------------------------------------------------
BackupStoreDirectory::Entry::Entry(const BackupStoreFilename &rName, box_time_t ModificationTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags, uint64_t AttributesHash)
	: mName(rName),
	  mModificationTime(ModificationTime),
	  mObjectID(ObjectID),
	  mSizeInBlocks(SizeInBlocks),
	  mFlags(Flags),
	  mAttributesHash(AttributesHash),
	  mMinMarkNumber(0),
	  mMarkNumber(0),
	  mDependsNewer(0),
	  mDependsOlder(0)
{
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::TryReading(IOStream &, int)
//		Purpose: Read an entry from a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::ReadFromStream(IOStream &rStream, int Timeout)
{
	// Grab the raw bytes from the stream which compose the header
	en_StreamFormat entry;
	if(!rStream.ReadFullBuffer(&entry, sizeof(entry), 0 /* not interested in bytes read if this fails */, Timeout))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Do reading first before modifying the variables, to be more exception safe
	
	// Get the filename
	BackupStoreFilename name;
	name.ReadFromStream(rStream, Timeout);
	
	// Get the attributes
	mAttributes.ReadFromStream(rStream, Timeout);

	// Store the rest of the bits
	mModificationTime =		box_ntoh64(entry.mModificationTime);
	mObjectID = 			box_ntoh64(entry.mObjectID);
	mSizeInBlocks = 		box_ntoh64(entry.mSizeInBlocks);
	mAttributesHash =		box_ntoh64(entry.mAttributesHash);
	mFlags = 				ntohs(entry.mFlags);
	mName =					name;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::WriteToStream(IOStream &)
//		Purpose: Writes the entry to a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::WriteToStream(IOStream &rStream) const
{
	// Build a structure
	en_StreamFormat entry;
	entry.mModificationTime = 	box_hton64(mModificationTime);
	entry.mObjectID = 			box_hton64(mObjectID);
	entry.mSizeInBlocks = 		box_hton64(mSizeInBlocks);
	entry.mAttributesHash =		box_hton64(mAttributesHash);
	entry.mFlags = 				htons(mFlags);
	
	// Write it
	rStream.Write(&entry, sizeof(entry));
	
	// Write the filename
	mName.WriteToStream(rStream);
	
	// Write any attributes
	mAttributes.WriteToStream(rStream);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::ReadFromStreamDependencyInfo(IOStream &, int)
//		Purpose: Read the optional dependency info from a stream
//		Created: 13/7/04
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::ReadFromStreamDependencyInfo(IOStream &rStream, int Timeout)
{
	// Grab the raw bytes from the stream which compose the header
	en_StreamFormatDepends depends;
	if(!rStream.ReadFullBuffer(&depends, sizeof(depends), 0 /* not interested in bytes read if this fails */, Timeout))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Store the data
	mDependsNewer = box_ntoh64(depends.mDependsNewer);
	mDependsOlder = box_ntoh64(depends.mDependsOlder);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Entry::WriteToStreamDependencyInfo(IOStream &)
//		Purpose: Write the optional dependency info to a stream
//		Created: 13/7/04
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Entry::WriteToStreamDependencyInfo(IOStream &rStream) const
{
	// Build structure
	en_StreamFormatDepends depends;	
	depends.mDependsNewer = box_hton64(mDependsNewer);
	depends.mDependsOlder = box_hton64(mDependsOlder);
	// Write
	rStream.Write(&depends, sizeof(depends));
}



