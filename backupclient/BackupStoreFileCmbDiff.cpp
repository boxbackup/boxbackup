// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileCmbDiff.cpp
//		Purpose: Combine two diffs together
//		Created: 12/7/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <new>
#include <stdlib.h>

#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreException.h"
#include "BackupStoreConstants.h"
#include "BackupStoreFilename.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::CombineDiffs(IOStream &, IOStream &, IOStream &rOut)
//		Purpose: Given two diffs, combine them into a single diff, to produce a diff
//				 which, combined with the original file, creates the result of applying
//				 rDiff, then rDiff2. Two opens of rDiff2 are required
//		Created: 12/7/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::CombineDiffs(IOStream &rDiff1, IOStream &rDiff2, IOStream &rDiff2b, IOStream &rOut)
{
	// Skip header of first diff, record where the data starts, and skip to the index
	int64_t diff1DataStarts = 0;
	{
		// Read the header for the From file
		file_StreamFormat diff1Hdr;
		if(!rDiff1.ReadFullBuffer(&diff1Hdr, sizeof(diff1Hdr), 0))
		{
			THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
		}
		if(ntohl(diff1Hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1)
		{
			THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
		}
		// Skip over the filename and attributes of the From file
		// BLOCK
		{
			BackupStoreFilename filename2;
			filename2.ReadFromStream(rDiff1, IOStream::TimeOutInfinite);
			int32_t size_s;
			if(!rDiff1.ReadFullBuffer(&size_s, sizeof(size_s), 0 /* not interested in bytes read if this fails */))
			{
				THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
			}
			int size = ntohl(size_s);
			// Skip forward the size
			rDiff1.Seek(size, IOStream::SeekType_Relative);		
		}
		// Record position
		diff1DataStarts = rDiff1.GetPosition();
		// Skip to index
		rDiff1.Seek(0 - (((box_ntoh64(diff1Hdr.mNumBlocks)) * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader)), IOStream::SeekType_End);
	}

	// Read the index of the first diff
	// Header first
	file_BlockIndexHeader diff1IdxHdr;
	if(!rDiff1.ReadFullBuffer(&diff1IdxHdr, sizeof(diff1IdxHdr), 0))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}
	if(ntohl(diff1IdxHdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	int64_t diff1NumBlocks = box_ntoh64(diff1IdxHdr.mNumBlocks);
	// Allocate some memory
	int64_t *diff1BlockStartPositions = (int64_t*)::malloc((diff1NumBlocks + 1) * sizeof(int64_t));
	if(diff1BlockStartPositions == 0)
	{
		throw std::bad_alloc();
	}

	// Buffer data
	void *buffer = 0;
	int bufferSize = 0;	
	
	try
	{
		// Then the entries:
		// For each entry, want to know if it's in the file, and if so, how big it is.
		// We'll store this as an array of file positions in the file, with an additioal
		// entry on the end so that we can work out the length of the last block.
		// If an entry isn't in the file, then store 0 - (position in other file).
		int64_t diff1Position = diff1DataStarts;
		for(int64_t b = 0; b < diff1NumBlocks; ++b)
		{
			file_BlockIndexEntry e;
			if(!rDiff1.ReadFullBuffer(&e, sizeof(e), 0))
			{
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}
	
			// Where's the block?
			int64_t blockEn = box_ntoh64(e.mEncodedSize);
			if(blockEn <= 0)
			{
				// Just store the negated block number
				diff1BlockStartPositions[b] = blockEn;
			}
			else
			{
				// Block is present in this file
				diff1BlockStartPositions[b] = diff1Position;
				diff1Position += blockEn;
			}
		}
		
		// Finish off the list, so the last entry can have it's size calcuated.
		diff1BlockStartPositions[diff1NumBlocks] = diff1Position;

		// Now read the second diff's header, copying it to the out file
		file_StreamFormat diff2Hdr;
		if(!rDiff2.ReadFullBuffer(&diff2Hdr, sizeof(diff2Hdr), 0))
		{
			THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
		}
		if(ntohl(diff2Hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1)
		{
			THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
		}
		// Copy
		rOut.Write(&diff2Hdr, sizeof(diff2Hdr));
		// Copy over filename and attributes
		// BLOCK
		{
			BackupStoreFilename filename;
			filename.ReadFromStream(rDiff2, IOStream::TimeOutInfinite);
			filename.WriteToStream(rOut);
			StreamableMemBlock attr;
			attr.ReadFromStream(rDiff2, IOStream::TimeOutInfinite);
			attr.WriteToStream(rOut);
		}
		
		// Get to the index of rDiff2b, and read the header
		MoveStreamPositionToBlockIndex(rDiff2b);
		file_BlockIndexHeader diff2IdxHdr;
		if(!rDiff2b.ReadFullBuffer(&diff2IdxHdr, sizeof(diff2IdxHdr), 0))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		if(ntohl(diff2IdxHdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
		{
			THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
		}
		int64_t diff2NumBlocks = box_ntoh64(diff2IdxHdr.mNumBlocks);
		int64_t diff2IndexEntriesStart = rDiff2b.GetPosition();
		
		// Then read all the entries
		int64_t diff2FilePosition = rDiff2.GetPosition();
		for(int64_t b = 0; b < diff2NumBlocks; ++b)
		{
			file_BlockIndexEntry e;
			if(!rDiff2b.ReadFullBuffer(&e, sizeof(e), 0))
			{
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}

			// What do to next about copying data
			bool copyBlock = false;
			int copySize = 0;
			int64_t copyFrom = 0;
			bool fromFileDiff1 = false;
	
			// Where's the block?
			int64_t blockEn = box_ntoh64(e.mEncodedSize);
			if(blockEn > 0)
			{
				// Block is present in this file -- copy to out
				copyBlock = true;
				copyFrom = diff2FilePosition;
				copySize = (int)blockEn;
				
				// Move pointer onwards
				diff2FilePosition += blockEn;
			}
			else
			{
				// Block isn't present here -- is it present in the old one?
				int64_t blockIndex = 0 - blockEn;
				if(blockIndex < 0 || blockIndex > diff1NumBlocks)
				{
					THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
				}
				if(diff1BlockStartPositions[blockIndex] > 0)
				{
					// Block is in the old diff file, copy it across
					copyBlock = true;
					copyFrom = diff1BlockStartPositions[blockIndex];
					int nb = blockIndex + 1;
					while(diff1BlockStartPositions[nb] <= 0)
					{
						// This is safe, because the last entry will terminate it properly!
						++nb;
						ASSERT(nb <= diff1NumBlocks);
					}
					copySize = diff1BlockStartPositions[nb] - copyFrom;
					fromFileDiff1 = true;
				}
			}
			//TRACE4("%d %d %lld %d\n", copyBlock, copySize, copyFrom, fromFileDiff1);
						
			// Copy data to the output file?
			if(copyBlock)
			{
				// Allocate enough space
				if(bufferSize < copySize || buffer == 0)
				{
					// Free old block
					if(buffer != 0)
					{
						::free(buffer);
						buffer = 0;
						bufferSize = 0;
					}
					// Allocate new block
					buffer = ::malloc(copySize);
					if(buffer == 0)
					{
						throw std::bad_alloc();
					}
					bufferSize = copySize;
				}
				ASSERT(bufferSize >= copySize);
				
				// Load in the data
				if(fromFileDiff1)
				{
					rDiff1.Seek(copyFrom, IOStream::SeekType_Absolute);
					if(!rDiff1.ReadFullBuffer(buffer, copySize, 0))
					{
						THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
					}
				}
				else
				{
					rDiff2.Seek(copyFrom, IOStream::SeekType_Absolute);
					if(!rDiff2.ReadFullBuffer(buffer, copySize, 0))
					{
						THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
					}
				}
				// Write out data
				rOut.Write(buffer, copySize);
			}
		}
		
		// Write the modified header
		diff2IdxHdr.mOtherFileID = diff1IdxHdr.mOtherFileID;
		rOut.Write(&diff2IdxHdr, sizeof(diff2IdxHdr));
		
		// Then we'll write out the index, reading the data again
		rDiff2b.Seek(diff2IndexEntriesStart, IOStream::SeekType_Absolute);
		for(int64_t b = 0; b < diff2NumBlocks; ++b)
		{
			file_BlockIndexEntry e;
			if(!rDiff2b.ReadFullBuffer(&e, sizeof(e), 0))
			{
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}
	
			// Where's the block?
			int64_t blockEn = box_ntoh64(e.mEncodedSize);
		
			// If it's not in this file, it needs modification...
			if(blockEn <= 0)
			{
				int64_t blockIndex = 0 - blockEn;
				// In another file. Need to translate this against the other diff
				if(diff1BlockStartPositions[blockIndex] > 0)
				{
					// Block is in the first diff file, stick in size
					int nb = blockIndex + 1;
					while(diff1BlockStartPositions[nb] <= 0)
					{
						// This is safe, because the last entry will terminate it properly!
						++nb;
						ASSERT(nb <= diff1NumBlocks);
					}
					int64_t size = diff1BlockStartPositions[nb] - diff1BlockStartPositions[blockIndex];
					e.mEncodedSize = box_hton64(size);
				}
				else
				{
					// Block in the original file, use translated value
					e.mEncodedSize = box_hton64(diff1BlockStartPositions[blockIndex]);
				}
			}
			
			// Write entry
			rOut.Write(&e, sizeof(e));
		}
	}
	catch(...)
	{
		// clean up
		::free(diff1BlockStartPositions);
		if(buffer != 0)
		{
			::free(buffer);
		}
		throw;
	}
	
	// Clean up allocated memory
	::free(diff1BlockStartPositions);
	if(buffer != 0)
	{
		::free(buffer);
	}
}

