// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileCombine.cpp
//		Purpose: File combining for BackupStoreFile
//		Created: 16/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <new>

#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreException.h"
#include "BackupStoreConstants.h"
#include "BackupStoreFilename.h"
#include "FileStream.h"

#include "MemLeakFindOn.h"

typedef struct
{
	int64_t mFilePosition;
} FromIndexEntry;

static void LoadFromIndex(IOStream &rFrom, FromIndexEntry *pIndex, int64_t NumEntries);
static void CopyData(IOStream &rDiffData, IOStream &rDiffIndex, int64_t DiffNumBlocks, IOStream &rFrom, FromIndexEntry *pFromIndex, int64_t FromNumBlocks, IOStream &rOut);
static void WriteNewIndex(IOStream &rDiff, int64_t DiffNumBlocks, FromIndexEntry *pFromIndex, int64_t FromNumBlocks, IOStream &rOut);

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::CombineFile(IOStream &, IOStream &, IOStream &)
//		Purpose: Where rDiff is a store file which is incomplete as a result of a
//				 diffing operation, rFrom is the file it is diffed from, and 
//				 rOut is the stream in which to place the result, the old file
//				 and new file are combined into a file containing all the data.
//				 rDiff2 is the same file as rDiff, opened again to get two
//				 independent streams to the same file.
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::CombineFile(IOStream &rDiff, IOStream &rDiff2, IOStream &rFrom, IOStream &rOut)
{
	// Read and copy the header.
	file_StreamFormat hdr;
	if(!rDiff.ReadFullBuffer(&hdr, sizeof(hdr), 0))
	{
		THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
	}
	if(ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	// Copy
	rOut.Write(&hdr, sizeof(hdr));
	// Copy over filename and attributes
	// BLOCK
	{
		BackupStoreFilename filename;
		filename.ReadFromStream(rDiff, IOStream::TimeOutInfinite);
		filename.WriteToStream(rOut);
		StreamableMemBlock attr;
		attr.ReadFromStream(rDiff, IOStream::TimeOutInfinite);
		attr.WriteToStream(rOut);
	}
	
	// Read the header for the From file
	file_StreamFormat fromHdr;
	if(!rFrom.ReadFullBuffer(&fromHdr, sizeof(fromHdr), 0))
	{
		THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
	}
	if(ntohl(fromHdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	// Skip over the filename and attributes of the From file
	// BLOCK
	{
		BackupStoreFilename filename2;
		filename2.ReadFromStream(rFrom, IOStream::TimeOutInfinite);
		int32_t size_s;
		if(!rFrom.ReadFullBuffer(&size_s, sizeof(size_s), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
		}
		int size = ntohl(size_s);
		// Skip forward the size
		rFrom.Seek(size, IOStream::SeekType_Relative);		
	}
	
	// Allocate memory for the block index of the From file
	int64_t fromNumBlocks = ntoh64(fromHdr.mNumBlocks);
	// NOTE: An extra entry is required so that the length of the last block can be calculated
	FromIndexEntry *pFromIndex = (FromIndexEntry*)::malloc((fromNumBlocks+1) * sizeof(FromIndexEntry));
	if(pFromIndex == 0)
	{
		throw std::bad_alloc();
	}
	
	try
	{
		// Load the index from the From file, calculating the offsets in the
		// file as we go along, and enforce that everything should be present.
		LoadFromIndex(rFrom, pFromIndex, fromNumBlocks);
		
		// Read in the block index of the Diff file in small chunks, and output data
		// for each block, either from this file, or the other file.
		int64_t diffNumBlocks = ntoh64(hdr.mNumBlocks);
		CopyData(rDiff /* positioned at start of data */, rDiff2, diffNumBlocks, rFrom, pFromIndex, fromNumBlocks, rOut);
		
		// Read in the block index again, and output the new block index, simply
		// filling in the sizes of blocks from the old file.
		WriteNewIndex(rDiff, diffNumBlocks, pFromIndex, fromNumBlocks, rOut);
		
		// Free buffers
		::free(pFromIndex);
		pFromIndex = 0;
	}
	catch(...)
	{
		// Clean up
		if(pFromIndex != 0)
		{
			::free(pFromIndex);
			pFromIndex = 0;
		}	
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static LoadFromIndex(IOStream &, FromIndexEntry *, int64_t)
//		Purpose: Static. Load the index from the From file
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
static void LoadFromIndex(IOStream &rFrom, FromIndexEntry *pIndex, int64_t NumEntries)
{
	ASSERT(pIndex != 0);
	ASSERT(NumEntries >= 0);

	// Get the starting point in the file
	int64_t filePos = rFrom.GetPosition();
	
	// Jump to the end of the file to read the index
	rFrom.Seek(0 - ((NumEntries * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader)), IOStream::SeekType_End);
	
	// Read block index header
	file_BlockIndexHeader blkhdr;
	if(!rFrom.ReadFullBuffer(&blkhdr, sizeof(blkhdr), 0))
	{
		THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
	}
	if(ntohl(blkhdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1
		|| (int64_t)ntoh64(blkhdr.mNumBlocks) != NumEntries)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// And then the block entries
	for(int64_t b = 0; b < NumEntries; ++b)
	{
		// Read
		file_BlockIndexEntry en;
		if(!rFrom.ReadFullBuffer(&en, sizeof(en), 0))
		{
			THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
		}
		
		// Add to list
		pIndex[b].mFilePosition = filePos;

		// Encoded size?
		int64_t encodedSize = ntoh64(en.mEncodedSize);
		// Check that the block is actually there
		if(encodedSize <= 0)
		{
			THROW_EXCEPTION(BackupStoreException, OnCombineFromFileIsIncomplete)
		}

		// Move file pointer on
		filePos += encodedSize;
	}
	
	// Store the position in the very last entry, so the size of the last entry can be calculated
	pIndex[NumEntries].mFilePosition = filePos;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static CopyData(IOStream &, IOStream &, int64_t, IOStream &, FromIndexEntry *, int64_t, IOStream &)
//		Purpose: Static. Copy data from the Diff and From file to the out file.
//				 rDiffData is at beginning of data.
//				 rDiffIndex at any position.
//				 rFrom is at any position.
//				 rOut is after the header, ready for data
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
static void CopyData(IOStream &rDiffData, IOStream &rDiffIndex, int64_t DiffNumBlocks,
	IOStream &rFrom, FromIndexEntry *pFromIndex, int64_t FromNumBlocks, IOStream &rOut)
{
	// Jump to the end of the diff file to read the index
	rDiffIndex.Seek(0 - ((DiffNumBlocks * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader)), IOStream::SeekType_End);
	
	// Read block index header
	file_BlockIndexHeader diffBlkhdr;
	if(!rDiffIndex.ReadFullBuffer(&diffBlkhdr, sizeof(diffBlkhdr), 0))
	{
		THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
	}
	if(ntohl(diffBlkhdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1
		|| (int64_t)ntoh64(diffBlkhdr.mNumBlocks) != DiffNumBlocks)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Record where the From file is
	int64_t fromPos = rFrom.GetPosition();
	
	// Buffer data
	void *buffer = 0;
	int bufferSize = 0;
	
	try
	{
		// Read the blocks in!
		for(int64_t b = 0; b < DiffNumBlocks; ++b)
		{
			// Read
			file_BlockIndexEntry en;
			if(!rDiffIndex.ReadFullBuffer(&en, sizeof(en), 0))
			{
				THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
			}
			
			// What's the size value stored in the entry
			int64_t encodedSize = ntoh64(en.mEncodedSize);
			
			// How much data will be read?
			int32_t blockSize = 0;
			if(encodedSize > 0)
			{
				// The block is actually in the diff file
				blockSize = encodedSize;
			}
			else
			{
				// It's in the from file. First, check to see if it's valid
				int64_t blockIdx = (0 - encodedSize);
				if(blockIdx > FromNumBlocks)
				{
					// References a block which doesn't actually exist
					THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
				}
				// Calculate size. This operation is safe because of the extra entry at the end
				blockSize = pFromIndex[blockIdx + 1].mFilePosition - pFromIndex[blockIdx].mFilePosition;
			}
			ASSERT(blockSize > 0);
			
			// Make sure there's memory available to copy this
			if(bufferSize < blockSize || buffer == 0)
			{
				// Free old block
				if(buffer != 0)
				{
					::free(buffer);
					buffer = 0;
					bufferSize = 0;
				}
				// Allocate new block
				buffer = ::malloc(blockSize);
				if(buffer == 0)
				{
					throw std::bad_alloc();
				}
				bufferSize = blockSize;
			}
			ASSERT(bufferSize >= blockSize);
			
			// Load in data from one of the files
			if(encodedSize > 0)
			{
				// Load from diff file
				if(!rDiffData.ReadFullBuffer(buffer, blockSize, 0))
				{
					THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
				}				
			}
			else
			{
				// Locate and read the data from the from file
				int64_t blockIdx = (0 - encodedSize);
				// Seek if necessary
				if(fromPos != pFromIndex[blockIdx].mFilePosition)
				{
					rFrom.Seek(pFromIndex[blockIdx].mFilePosition, IOStream::SeekType_Absolute);
					fromPos = pFromIndex[blockIdx].mFilePosition;
				}
				// Read
				if(!rFrom.ReadFullBuffer(buffer, blockSize, 0))
				{
					THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
				}
				
				// Update fromPos to current position
				fromPos += blockSize;
			}
			
			// Write data to out file
			rOut.Write(buffer, blockSize);
		}
		
		// Free buffer
		::free(buffer);
		buffer = 0;
	}
	catch(...)
	{
		if(buffer != 0)
		{
			::free(buffer);
			buffer = 0;
		}
		throw;
	}
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    static WriteNewIndex(IOStream &, int64_t, FromIndexEntry *, int64_t, IOStream &)
//		Purpose: Write the index to the out file, just copying from the diff file and
//				 adjusting the entries.
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
static void WriteNewIndex(IOStream &rDiff, int64_t DiffNumBlocks, FromIndexEntry *pFromIndex, int64_t FromNumBlocks, IOStream &rOut)
{
	// Jump to the end of the diff file to read the index
	rDiff.Seek(0 - ((DiffNumBlocks * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader)), IOStream::SeekType_End);
	
	// Read block index header
	file_BlockIndexHeader diffBlkhdr;
	if(!rDiff.ReadFullBuffer(&diffBlkhdr, sizeof(diffBlkhdr), 0))
	{
		THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
	}
	if(ntohl(diffBlkhdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1
		|| (int64_t)ntoh64(diffBlkhdr.mNumBlocks) != DiffNumBlocks)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Write it out with a blanked out other file ID
	diffBlkhdr.mOtherFileID = hton64(0);
	rOut.Write(&diffBlkhdr, sizeof(diffBlkhdr));
	
	// Rewrite the index
	for(int64_t b = 0; b < DiffNumBlocks; ++b)
	{
		file_BlockIndexEntry en;
		if(!rDiff.ReadFullBuffer(&en, sizeof(en), 0))
		{
			THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
		}
		
		// What's the size value stored in the entry
		int64_t encodedSize = ntoh64(en.mEncodedSize);
		
		// Need to adjust it?
		if(encodedSize <= 0)
		{
			// This actually refers to a block in the from file. So rewrite this.
			int64_t blockIdx = (0 - encodedSize);
			if(blockIdx > FromNumBlocks)
			{
				// References a block which doesn't actually exist
				THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
			}
			// Calculate size. This operation is safe because of the extra entry at the end
			int32_t blockSize = pFromIndex[blockIdx + 1].mFilePosition - pFromIndex[blockIdx].mFilePosition;
			// Then replace entry
			en.mEncodedSize = hton64(((uint64_t)blockSize));
		}
		
		// Write entry
		rOut.Write(&en, sizeof(en));
	}
}





