// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileEncodeStream.cpp
//		Purpose: Implement stream-based file encoding for the backup store
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "BackupStoreFileEncodeStream.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreFileCryptVar.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreException.h"
#include "BackupStoreConstants.h"
#include "BoxTime.h"
#include "BackupClientFileAttributes.h"
#include "FileStream.h"
#include "RollingChecksum.h"
#include "Random.h"

#include "MemLeakFindOn.h"

using namespace BackupStoreFileCryptVar;


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::BackupStoreFileEncodeStream
//		Purpose: Constructor (opens file)
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
BackupStoreFileEncodeStream::BackupStoreFileEncodeStream()
	: mpRecipe(0),
	  mpFile(0),
	  mStatus(Status_Header),
	  mSendData(true),
	  mTotalBlocks(0),
	  mAbsoluteBlockNumber(-1),
	  mInstructionNumber(-1),
	  mNumBlocks(0),
	  mCurrentBlock(-1),
	  mCurrentBlockEncodedSize(0),
	  mPositionInCurrentBlock(0),
	  mBlockSize(BACKUP_FILE_MIN_BLOCK_SIZE),
	  mLastBlockSize(0),
	  mpRawBuffer(0),
	  mAllocatedBufferSize(0),
	  mEntryIVBase(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::~BackupStoreFileEncodeStream()
//		Purpose: Destructor
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
BackupStoreFileEncodeStream::~BackupStoreFileEncodeStream()
{
	// Free buffers
	if(mpRawBuffer)
	{
		::free(mpRawBuffer);
		mpRawBuffer = 0;
	}
	
	// Close the file, which we might have open
	if(mpFile)
	{
		delete mpFile;
		mpFile = 0;
	}
	
	// Free the recipe
	if(mpRecipe != 0)
	{
		delete mpRecipe;
		mpRecipe = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::Setup(const char *, Recipe *, int64_t, const BackupStoreFilename &, int64_t *)
//		Purpose: Reads file information, and builds file header reading for sending.
//				 Takes ownership of the Recipe.
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFileEncodeStream::Setup(const char *Filename, BackupStoreFileEncodeStream::Recipe *pRecipe,
	int64_t ContainerID, const BackupStoreFilename &rStoreFilename, int64_t *pModificationTime)
{
	// Pointer to a blank recipe which we might create
	BackupStoreFileEncodeStream::Recipe *pblankRecipe = 0;

	try
	{
		// Get file attributes
		box_time_t modTime = 0;
		int64_t fileSize = 0;
		BackupClientFileAttributes attr;
		attr.ReadAttributes(Filename, false /* no zeroing of modification times */, &modTime,
			0 /* not interested in attr mod time */, &fileSize);
	
		// Might need to create a blank recipe...
		if(pRecipe == 0)
		{
			pblankRecipe = new BackupStoreFileEncodeStream::Recipe(0, 0);
			
			BackupStoreFileEncodeStream::RecipeInstruction instruction;
			instruction.mSpaceBefore = fileSize; 	// whole file
			instruction.mBlocks = 0;				// no blocks
			instruction.mpStartBlock = 0;			// no block
			pblankRecipe->push_back(instruction);		

			pRecipe = pblankRecipe;
		}
	
		// Tell caller?
		if(pModificationTime != 0)
		{
			*pModificationTime = modTime;
		}
		
		// Go through each instruction in the recipe and work out how many blocks
		// it will add, and the max clear size of these blocks
		int maxBlockClearSize = 0;
		for(uint64_t inst = 0; inst < pRecipe->size(); ++inst)
		{
			if((*pRecipe)[inst].mSpaceBefore > 0)
			{
				// Calculate the number of blocks the space before requires
				int64_t numBlocks;
				int32_t blockSize, lastBlockSize;
				CalculateBlockSizes((*pRecipe)[inst].mSpaceBefore, numBlocks, blockSize, lastBlockSize);
				// Add to accumlated total
				mTotalBlocks += numBlocks;
				// Update maximum clear size
				if(blockSize > maxBlockClearSize) maxBlockClearSize = blockSize;
				if(lastBlockSize > maxBlockClearSize) maxBlockClearSize = lastBlockSize;
			}
			
			// Add number of blocks copied from the previous file
			mTotalBlocks += (*pRecipe)[inst].mBlocks;
			
			// Check for bad things
			if((*pRecipe)[inst].mBlocks < 0 || ((*pRecipe)[inst].mBlocks != 0 && (*pRecipe)[inst].mpStartBlock == 0))
			{
				THROW_EXCEPTION(BackupStoreException, Internal)
			}

			// Run through blocks to get the max clear size
			for(int32_t b = 0; b < (*pRecipe)[inst].mBlocks; ++b)
			{
				if((*pRecipe)[inst].mpStartBlock[b].mSize > maxBlockClearSize) maxBlockClearSize = (*pRecipe)[inst].mpStartBlock[b].mSize;
			}
		}
		
		// Send data? (symlinks don't have any data in them)
		mSendData = !attr.IsSymLink();

		// If not data is being sent, then the max clear block size is zero
		if(!mSendData)
		{
			maxBlockClearSize = 0;
		}
		
		// Header
		file_StreamFormat hdr;
		hdr.mMagicValue = htonl(OBJECTMAGIC_FILE_MAGIC_VALUE_V1);
		hdr.mNumBlocks = (mSendData)?(box_hton64(mTotalBlocks)):(0);
		hdr.mContainerID = box_hton64(ContainerID);
		hdr.mModificationTime = box_hton64(modTime);
		// add a bit to make it harder to tell what's going on -- try not to give away too much info about file size
		hdr.mMaxBlockClearSize = htonl(maxBlockClearSize + 128);
		hdr.mOptions = 0;		// no options defined yet
		
		// Write header to stream
		mData.Write(&hdr, sizeof(hdr));
		
		// Write filename to stream
		rStoreFilename.WriteToStream(mData);
		
		// Write attributes to stream
		attr.WriteToStream(mData);
	
		// Allocate some buffers for writing data
		if(mSendData)
		{
			// Open the file
			mpFile = new FileStream(Filename);
		
			// Work out the largest possible block required for the encoded data
			mAllocatedBufferSize = BackupStoreFile::MaxBlockSizeForChunkSize(maxBlockClearSize);
			
			// Then allocate two blocks of this size
			mpRawBuffer = (uint8_t*)::malloc(mAllocatedBufferSize);
			if(mpRawBuffer == 0)
			{
				throw std::bad_alloc();
			}
#ifndef NDEBUG
			// In debug builds, make sure that the reallocation code is exercised.
			mEncodedBuffer.Allocate(mAllocatedBufferSize / 4);
#else
			mEncodedBuffer.Allocate(mAllocatedBufferSize);
#endif
		}
		else
		{
			// Write an empty block index for the symlink
			file_BlockIndexHeader blkhdr;
			blkhdr.mMagicValue = htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1);
			blkhdr.mOtherFileID = box_hton64(0);	// not other file ID
			blkhdr.mEntryIVBase = box_hton64(0);
			blkhdr.mNumBlocks = box_hton64(0);
			mData.Write(&blkhdr, sizeof(blkhdr));
		}
	
		// Ready for reading
		mData.SetForReading();
		
		// Update stats
		BackupStoreFile::msStats.mBytesInEncodedFiles += fileSize;
		
		// Finally, store the pointer to the recipe, when we know exceptions won't occur
		mpRecipe = pRecipe;
	}
	catch(...)
	{
		// Clean up any blank recipe
		if(pblankRecipe != 0)
		{
			delete pblankRecipe;
			pblankRecipe = 0;
		}
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::CalculateBlockSizes(int64_t &, int32_t &, int32_t &)
//		Purpose: Calculates the sizes of blocks in a section of the file
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
void BackupStoreFileEncodeStream::CalculateBlockSizes(int64_t DataSize, int64_t &rNumBlocksOut, int32_t &rBlockSizeOut, int32_t &rLastBlockSizeOut)
{
	// How many blocks, and how big?
	rBlockSizeOut = BACKUP_FILE_MIN_BLOCK_SIZE / 2;
	do
	{
		rBlockSizeOut *= 2;
		
		rNumBlocksOut = (DataSize + rBlockSizeOut - 1) / rBlockSizeOut;
		
	} while(rBlockSizeOut <= BACKUP_FILE_MAX_BLOCK_SIZE && rNumBlocksOut > BACKUP_FILE_INCREASE_BLOCK_SIZE_AFTER);
	
	// Last block size
	rLastBlockSizeOut = DataSize - ((rNumBlocksOut - 1) * rBlockSizeOut);
	
	// Avoid small blocks?
	if(rLastBlockSizeOut < BACKUP_FILE_AVOID_BLOCKS_LESS_THAN
		&& rNumBlocksOut > 1)
	{
		// Add the small bit of data to the last block
		--rNumBlocksOut;
		rLastBlockSizeOut += rBlockSizeOut;
	}
	
	// checks!
	ASSERT((((rNumBlocksOut-1) * rBlockSizeOut) + rLastBlockSizeOut) == DataSize);
	//TRACE4("CalcBlockSize, sz %lld, num %lld, blocksize %d, last %d\n", DataSize, rNumBlocksOut, (int32_t)rBlockSizeOut, (int32_t)rLastBlockSizeOut);
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::Read(void *, int, int)
//		Purpose: As interface -- generates encoded file data on the fly from the raw file
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
int BackupStoreFileEncodeStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	// Check there's something to do.
	if(mStatus == Status_Finished)
	{
		return 0;
	}

	int bytesToRead = NBytes;
	uint8_t *buffer = (uint8_t*)pBuffer;
	
	while(bytesToRead > 0 && mStatus != Status_Finished)
	{
		if(mStatus == Status_Header || mStatus == Status_BlockListing)
		{
			// Header or block listing phase -- send from the buffered stream
		
			// Send bytes from the data buffer
			int b = mData.Read(buffer, bytesToRead, Timeout);
			bytesToRead -= b;
			buffer += b;
			
			// Check to see if all the data has been used from this stream
			if(!mData.StreamDataLeft())
			{
				// Yes, move on to next phase (or finish, if there's no file data)
				if(!mSendData)
				{
					mStatus = Status_Finished;
				}
				else
				{
					// Reset the buffer so it can be used for the next phase
					mData.Reset();
		
					// Get buffer ready for index?
					if(mStatus == Status_Header)
					{
						// Just finished doing the stream header, create the block index header
						file_BlockIndexHeader blkhdr;
						blkhdr.mMagicValue = htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1);
						ASSERT(mpRecipe != 0);
						blkhdr.mOtherFileID = box_hton64(mpRecipe->GetOtherFileID());
						blkhdr.mNumBlocks = box_hton64(mTotalBlocks);
						
						// Generate the IV base
						Random::Generate(&mEntryIVBase, sizeof(mEntryIVBase));
						blkhdr.mEntryIVBase = box_hton64(mEntryIVBase);
						
						mData.Write(&blkhdr, sizeof(blkhdr));
					}
				
					++mStatus;
				}
			}
		}
		else if(mStatus == Status_Blocks)
		{
			// Block sending phase
			
			if(mPositionInCurrentBlock >= mCurrentBlockEncodedSize)
			{
				// Next block!
				++mCurrentBlock;
				++mAbsoluteBlockNumber;
				if(mCurrentBlock >= mNumBlocks)
				{
					// Output extra blocks for this instruction and move forward in file
					if(mInstructionNumber >= 0)
					{
						SkipPreviousBlocksInInstruction();
					}
				
					// Is there another instruction to go?
					++mInstructionNumber;
					
					// Skip instructions which don't contain any data
					while(mInstructionNumber < static_cast<int64_t>(mpRecipe->size())
						&& (*mpRecipe)[mInstructionNumber].mSpaceBefore == 0)
					{
						SkipPreviousBlocksInInstruction();
						++mInstructionNumber;
					}
					
					if(mInstructionNumber >= static_cast<int64_t>(mpRecipe->size()))
					{
						// End of blocks, go to next phase
						++mStatus;
						
						// Set the data to reading so the index can be written
						mData.SetForReading();
					}
					else
					{
						// Get ready for this instruction
						SetForInstruction();
					}
				}

				// Can't use 'else' here as SetForInstruction() will change this
				if(mCurrentBlock < mNumBlocks)
				{
					EncodeCurrentBlock();
				}
			}
			
			// Send data from the current block (if there's data to send)
			if(mPositionInCurrentBlock < mCurrentBlockEncodedSize)
			{
				// How much data to put in the buffer?
				int s = mCurrentBlockEncodedSize - mPositionInCurrentBlock;
				if(s > bytesToRead) s = bytesToRead;
				
				// Copy it in
				::memcpy(buffer, mEncodedBuffer.mpBuffer + mPositionInCurrentBlock, s);
				
				// Update variables
				bytesToRead -= s;
				buffer += s;
				mPositionInCurrentBlock += s;
			}
		}
		else
		{
			// Should never get here, as it'd be an invalid status
			ASSERT(false);
		}
	}
	
	// Add encoded size to stats
	BackupStoreFile::msStats.mTotalFileStreamSize += (NBytes - bytesToRead);
	
	// Return size of data to caller
	return NBytes - bytesToRead;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::StorePreviousBlocksInInstruction()
//		Purpose: Private. Stores the blocks of the old file referenced in the current
//				 instruction into the index and skips over the data in the file
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
void BackupStoreFileEncodeStream::SkipPreviousBlocksInInstruction()
{
	// Check something is necessary
	if((*mpRecipe)[mInstructionNumber].mpStartBlock == 0 || (*mpRecipe)[mInstructionNumber].mBlocks == 0)
	{
		return;
	}

	// Index of the first block in old file (being diffed from)
	int firstIndex = mpRecipe->BlockPtrToIndex((*mpRecipe)[mInstructionNumber].mpStartBlock);
	
	int64_t sizeToSkip = 0;

	for(int32_t b = 0; b < (*mpRecipe)[mInstructionNumber].mBlocks; ++b)
	{
		// Update stats
		BackupStoreFile::msStats.mBytesAlreadyOnServer += (*mpRecipe)[mInstructionNumber].mpStartBlock[b].mSize;
	
		// Store the entry
		StoreBlockIndexEntry(0 - (firstIndex + b),
			(*mpRecipe)[mInstructionNumber].mpStartBlock[b].mSize,
			(*mpRecipe)[mInstructionNumber].mpStartBlock[b].mWeakChecksum,
			(*mpRecipe)[mInstructionNumber].mpStartBlock[b].mStrongChecksum);	

		// Increment the absolute block number -- kept encryption IV in sync
		++mAbsoluteBlockNumber;
		
		// Add the size of this block to the size to skip
		sizeToSkip += (*mpRecipe)[mInstructionNumber].mpStartBlock[b].mSize;
	}
	
	// Move forward in the stream
	mpFile->Seek(sizeToSkip, IOStream::SeekType_Relative);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::SetForInstruction()
//		Purpose: Private. Sets the state of the internal variables for the current instruction in the recipe
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
void BackupStoreFileEncodeStream::SetForInstruction()
{
	// Calculate block sizes
	CalculateBlockSizes((*mpRecipe)[mInstructionNumber].mSpaceBefore, mNumBlocks, mBlockSize, mLastBlockSize);
	
	// Set variables
	mCurrentBlock = 0;
	mCurrentBlockEncodedSize = 0;
	mPositionInCurrentBlock = 0;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::EncodeCurrentBlock()
//		Purpose: Private. Encodes the current block, and writes the block data to the index
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFileEncodeStream::EncodeCurrentBlock()
{
	// How big is the block, raw?
	int blockRawSize = mBlockSize;
	if(mCurrentBlock == (mNumBlocks - 1))
	{
		blockRawSize = mLastBlockSize;
	}
	ASSERT(blockRawSize < mAllocatedBufferSize);

	// Check file open
	if(mpFile == 0)
	{
		// File should be open, but isn't. So logical error.
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
	
	// Read the data in
	if(!mpFile->ReadFullBuffer(mpRawBuffer, blockRawSize, 0 /* not interested in size if failure */))
	{
		// TODO: Do something more intelligent, and abort this upload because the file
		// has changed
		THROW_EXCEPTION(BackupStoreException, Temp_FileEncodeStreamDidntReadBuffer)
	}
	
	// Encode it
	mCurrentBlockEncodedSize = BackupStoreFile::EncodeChunk(mpRawBuffer, blockRawSize, mEncodedBuffer);
	
	//TRACE2("Encode: Encoded size of block %d is %d\n", (int32_t)mCurrentBlock, (int32_t)mCurrentBlockEncodedSize);
	
	// Create block listing data -- generate checksums
	RollingChecksum weakChecksum(mpRawBuffer, blockRawSize);
	MD5Digest strongChecksum;
	strongChecksum.Add(mpRawBuffer, blockRawSize);
	strongChecksum.Finish();

	// Add entry to the index
	StoreBlockIndexEntry(mCurrentBlockEncodedSize, blockRawSize, weakChecksum.GetChecksum(), strongChecksum.DigestAsData());
	
	// Set vars to reading this block
	mPositionInCurrentBlock = 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::StoreBlockIndexEntry(int64_t, int32_t, uint32_t, uint8_t *)
//		Purpose: Private. Adds an entry to the index currently being stored for sending at end of the stream.
//		Created: 16/1/04
//
// --------------------------------------------------------------------------
void BackupStoreFileEncodeStream::StoreBlockIndexEntry(int64_t EncSizeOrBlkIndex, int32_t ClearSize, uint32_t WeakChecksum, uint8_t *pStrongChecksum)
{
	// First, the encrypted section
	file_BlockIndexEntryEnc entryEnc;
	entryEnc.mSize = htonl(ClearSize);
	entryEnc.mWeakChecksum = htonl(WeakChecksum);
	::memcpy(entryEnc.mStrongChecksum, pStrongChecksum, sizeof(entryEnc.mStrongChecksum));

	// Then the clear section
	file_BlockIndexEntry entry;
	entry.mEncodedSize = box_hton64(((uint64_t)EncSizeOrBlkIndex));
	
	// Then encrypt the encryted section
	// Generate the IV from the block number
	if(sBlowfishEncryptBlockEntry.GetIVLength() != sizeof(mEntryIVBase))
	{
		THROW_EXCEPTION(BackupStoreException, IVLengthForEncodedBlockSizeDoesntMeetLengthRequirements)
	}
	uint64_t iv = mEntryIVBase;
	iv += mAbsoluteBlockNumber;
	// Convert to network byte order before encrypting with it, so that restores work on
	// platforms with different endiannesses.
	iv = box_hton64(iv);
	sBlowfishEncryptBlockEntry.SetIV(&iv);

	// Encode the data
	int encodedSize = sBlowfishEncryptBlockEntry.TransformBlock(entry.mEnEnc, sizeof(entry.mEnEnc), &entryEnc, sizeof(entryEnc));
	if(encodedSize != sizeof(entry.mEnEnc))
	{
		THROW_EXCEPTION(BackupStoreException, BlockEntryEncodingDidntGiveExpectedLength)
	}

	// Save to data block for sending at the end of the stream
	mData.Write(&entry, sizeof(entry));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::Write(const void *, int)
//		Purpose: As interface. Exceptions.
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFileEncodeStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(BackupStoreException, CantWriteToEncodedFileStream)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::StreamDataLeft()
//		Purpose: As interface -- end of stream reached?
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreFileEncodeStream::StreamDataLeft()
{
	return (mStatus != Status_Finished);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::StreamClosed()
//		Purpose: As interface
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreFileEncodeStream::StreamClosed()
{
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::Recipe::Recipe(BackupStoreFileCreation::BlocksAvailableEntry *, int64_t)
//		Purpose: Constructor. Takes ownership of the block index, and will delete it when it's deleted
//		Created: 15/1/04
//
// --------------------------------------------------------------------------
BackupStoreFileEncodeStream::Recipe::Recipe(BackupStoreFileCreation::BlocksAvailableEntry *pBlockIndex,
		int64_t NumBlocksInIndex, int64_t OtherFileID)
	: mpBlockIndex(pBlockIndex),
	  mNumBlocksInIndex(NumBlocksInIndex),
	  mOtherFileID(OtherFileID)
{
	ASSERT((mpBlockIndex == 0) || (NumBlocksInIndex != 0))
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFileEncodeStream::Recipe::~Recipe()
//		Purpose: Destructor
//		Created: 15/1/04
//
// --------------------------------------------------------------------------
BackupStoreFileEncodeStream::Recipe::~Recipe()
{
	// Free the block index, if there is one
	if(mpBlockIndex != 0)
	{
		::free(mpBlockIndex);
	}
}




