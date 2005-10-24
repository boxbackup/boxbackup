// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileCmbIdx.cpp
//		Purpose: Combine indicies of a delta file and the file it's a diff from.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <new>
#include <string.h>

#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreException.h"
#include "BackupStoreConstants.h"
#include "BackupStoreFilename.h"

#include "MemLeakFindOn.h"

// Hide from outside world
namespace
{

class BSFCombinedIndexStream : public IOStream
{
public:
	BSFCombinedIndexStream(IOStream *pDiff);
	~BSFCombinedIndexStream();
	
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();
	virtual void Initialise(IOStream &rFrom);
	
private:
	IOStream *mpDiff;
	bool mIsInitialised;
	bool mHeaderWritten;
	file_BlockIndexHeader mHeader;
	int64_t mNumEntriesToGo;
	int64_t mNumEntriesInFromFile;
	int64_t *mFromBlockSizes;		// NOTE: Entries in network byte order
};

};

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::CombineFileIndices(IOStream &, IOStream &, bool)
//		Purpose: Given a diff file and the file it's a diff from, return a stream from which
//				 can be read the index of the combined file, without actually combining them.
//				 The stream of the diff must have a lifetime greater than or equal to the
//				 lifetime of the returned stream object. The full "from" file stream
//				 only needs to exist during the actual function call.
//				 If you pass in dodgy files which aren't related, then you will either
//				 get an error or bad results. So don't do that.
//				 If DiffIsIndexOnly is true, then rDiff is assumed to be a stream positioned
//				 at the beginning of the block index. Similarly for FromIsIndexOnly.
//				 WARNING: Reads of the returned streams with buffer sizes less than 64 bytes
//				 will not return any data.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupStoreFile::CombineFileIndices(IOStream &rDiff, IOStream &rFrom, bool DiffIsIndexOnly, bool FromIsIndexOnly)
{
	// Reposition file pointers?
	if(!DiffIsIndexOnly)
	{
		MoveStreamPositionToBlockIndex(rDiff);
	}
	if(!FromIsIndexOnly)
	{
		MoveStreamPositionToBlockIndex(rFrom);
	}

	// Create object
	std::auto_ptr<IOStream> stream(new BSFCombinedIndexStream(&rDiff));

	// Initialise it
	((BSFCombinedIndexStream *)stream.get())->Initialise(rFrom);

	// And return the stream
	return stream;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BSFCombinedIndexStream::BSFCombinedIndexStream()
//		Purpose: Private class. Constructor.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
BSFCombinedIndexStream::BSFCombinedIndexStream(IOStream *pDiff)
	: mpDiff(pDiff),
	  mIsInitialised(false),
	  mHeaderWritten(false),
	  mNumEntriesToGo(0),
	  mNumEntriesInFromFile(0),
	  mFromBlockSizes(0)
{
	ASSERT(mpDiff != 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BSFCombinedIndexStream::~BSFCombinedIndexStream()
//		Purpose: Private class. Destructor.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
BSFCombinedIndexStream::~BSFCombinedIndexStream()
{
	if(mFromBlockSizes != 0)
	{
		::free(mFromBlockSizes);
		mFromBlockSizes = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BSFCombinedIndexStream::Initialise(IOStream &)
//		Purpose: Private class. Initalise from the streams (diff passed in constructor).
//				 Both streams must have file pointer positioned at the block index.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
void BSFCombinedIndexStream::Initialise(IOStream &rFrom)
{
	// Paranoia is good.
	if(mIsInitialised)
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
	
	// Look at the diff file: Read in the header
	if(!mpDiff->ReadFullBuffer(&mHeader, sizeof(mHeader), 0))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}
	if(ntohl(mHeader.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Read relevant data.
	mNumEntriesToGo = ntoh64(mHeader.mNumBlocks);
	
	// Adjust a bit to reflect the fact it's no longer a diff
	mHeader.mOtherFileID = hton64(0);
	
	// Now look at the from file: Read header
	file_BlockIndexHeader fromHdr;
	if(!rFrom.ReadFullBuffer(&fromHdr, sizeof(fromHdr), 0))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}
	if(ntohl(fromHdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Then... allocate memory for the list of sizes
	mNumEntriesInFromFile = ntoh64(fromHdr.mNumBlocks);
	mFromBlockSizes = (int64_t*)::malloc(mNumEntriesInFromFile * sizeof(int64_t));
	if(mFromBlockSizes == 0)
	{
		throw std::bad_alloc();
	}
	
	// And read them all in!
	for(int64_t b = 0; b < mNumEntriesInFromFile; ++b)
	{
		file_BlockIndexEntry e;
		if(!rFrom.ReadFullBuffer(&e, sizeof(e), 0))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		
		// Check that the from file isn't a delta in itself
		if(ntoh64(e.mEncodedSize) <= 0)
		{
			THROW_EXCEPTION(BackupStoreException, OnCombineFromFileIsIncomplete)
		}

		// Store size (in network byte order)
		mFromBlockSizes[b] = e.mEncodedSize;
	}
	
	// Flag as initialised
	mIsInitialised = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BSFCombinedIndexStream::Read(void *, int, int)
//		Purpose: Private class. As interface.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
int BSFCombinedIndexStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	// Paranoia is good.
	if(!mIsInitialised || mFromBlockSizes == 0 || mpDiff == 0)
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
	
	int written = 0;
	
	// Header output yet?
	if(!mHeaderWritten)
	{
		// Enough space?
		if(NBytes < (int)sizeof(mHeader)) return 0;
		
		// Copy in
		::memcpy(pBuffer, &mHeader, sizeof(mHeader));
		NBytes -= sizeof(mHeader);
		written += sizeof(mHeader);
	
		// Flag it's done
		mHeaderWritten = true;
	}

	// How many entries can be written?
	int entriesToWrite = NBytes / sizeof(file_BlockIndexEntry);
	if(entriesToWrite > mNumEntriesToGo)
	{
		entriesToWrite = mNumEntriesToGo;
	}
	
	// Setup ready to go
	file_BlockIndexEntry *poutput = (file_BlockIndexEntry*)(((uint8_t*)pBuffer) + written);

	// Write entries
	for(int b = 0; b < entriesToWrite; ++b)
	{
		if(!mpDiff->ReadFullBuffer(&(poutput[b]), sizeof(file_BlockIndexEntry), 0))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		
		// Does this need adjusting?
		int s = ntoh64(poutput[b].mEncodedSize);
		if(s <= 0)
		{
			// A reference to a block in the from file
			int block = 0 - s;
			ASSERT(block >= 0);
			if(block >= mNumEntriesInFromFile)
			{
				// That's not good, the block doesn't exist
				THROW_EXCEPTION(BackupStoreException, OnCombineFromFileIsIncomplete)
			}
			
			// Adjust the entry in the buffer
			poutput[b].mEncodedSize = mFromBlockSizes[block];	// stored in network byte order, no translation necessary
		}
	}
	
	// Update written count
	written += entriesToWrite * sizeof(file_BlockIndexEntry);
	mNumEntriesToGo -= entriesToWrite;
	
	return written;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BSFCombinedIndexStream::Write(const void *, int)
//		Purpose: Private class. As interface.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
void BSFCombinedIndexStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(BackupStoreException, StreamDoesntHaveRequiredFeatures)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BSFCombinedIndexStream::StreamDataLeft()
//		Purpose: Private class. As interface
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
bool BSFCombinedIndexStream::StreamDataLeft()
{
	return (!mHeaderWritten) || (mNumEntriesToGo > 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BSFCombinedIndexStream::StreamClosed()
//		Purpose: Private class. As interface.
//		Created: 8/7/04
//
// --------------------------------------------------------------------------
bool BSFCombinedIndexStream::StreamClosed()
{
	return true;	// doesn't do writing
}

