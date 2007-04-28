// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileEncodeStream.h
//		Purpose: Implement stream-based file encoding for the backup store
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREFILEENCODESTREAM__H
#define BACKUPSTOREFILEENCODESTREAM__H

#include <vector>

#include "IOStream.h"
#include "BackupStoreFilename.h"
#include "CollectInBufferStream.h"
#include "MD5Digest.h"
#include "BackupStoreFile.h"
#include "ReadLoggingStream.h"

namespace BackupStoreFileCreation
{
	// Diffing and creation of files share some implementation details.
	typedef struct _BlocksAvailableEntry
	{
		struct _BlocksAvailableEntry *mpNextInHashList;
		int32_t mSize;			// size in clear
		uint32_t mWeakChecksum;	// weak, rolling checksum
		uint8_t mStrongChecksum[MD5Digest::DigestLength];	// strong digest based checksum
	} BlocksAvailableEntry;

}


// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreFileEncodeStream
//		Purpose: Encode a file into a stream
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
class BackupStoreFileEncodeStream : public IOStream
{
public:
	BackupStoreFileEncodeStream();
	~BackupStoreFileEncodeStream();
	
	typedef struct
	{
		int64_t mSpaceBefore;				// amount of bytes which aren't taken out of blocks which go
		int32_t mBlocks;					// number of block to reuse, starting at this one
		BackupStoreFileCreation::BlocksAvailableEntry *mpStartBlock;	// may be null
	} RecipeInstruction;
	
	class Recipe : public std::vector<RecipeInstruction>
	{
		// NOTE: This class is rather tied in with the implementation of diffing.
	public:
		Recipe(BackupStoreFileCreation::BlocksAvailableEntry *pBlockIndex, int64_t NumBlocksInIndex,
			int64_t OtherFileID = 0);
		~Recipe();
	
		int64_t GetOtherFileID() {return mOtherFileID;}
		int64_t BlockPtrToIndex(BackupStoreFileCreation::BlocksAvailableEntry *pBlock)
		{
			return pBlock - mpBlockIndex;
		}
	
	private:
		BackupStoreFileCreation::BlocksAvailableEntry *mpBlockIndex;
		int64_t mNumBlocksInIndex;
		int64_t mOtherFileID;
	};
	
	void Setup(const char *Filename, Recipe *pRecipe, int64_t ContainerID, const BackupStoreFilename &rStoreFilename, int64_t *pModificationTime);

	virtual int Read(void *pBuffer, int NBytes, int Timeout);
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

private:
	enum
	{
		Status_Header = 0,
		Status_Blocks = 1,
		Status_BlockListing = 2,
		Status_Finished = 3
	};
	
private:
	void EncodeCurrentBlock();
	void CalculateBlockSizes(int64_t DataSize, int64_t &rNumBlocksOut, int32_t &rBlockSizeOut, int32_t &rLastBlockSizeOut);
	void SkipPreviousBlocksInInstruction();
	void SetForInstruction();
	void StoreBlockIndexEntry(int64_t WncSizeOrBlkIndex, int32_t ClearSize, uint32_t WeakChecksum, uint8_t *pStrongChecksum);

private:
	Recipe *mpRecipe;
	IOStream *mpFile;					// source file
	CollectInBufferStream mData;		// buffer for header and index entries
	ReadLoggingStream *mpLogging;
	int mStatus;
	bool mSendData;						// true if there's file data to send (ie not a symlink)
	int64_t mTotalBlocks;				// Total number of blocks in the file
	int64_t mAbsoluteBlockNumber;		// The absolute block number currently being output
	// Instruction number
	int64_t mInstructionNumber;
	// All the below are within the current instruction
	int64_t mNumBlocks;					// number of blocks. Last one will be a different size to the rest in most cases
	int64_t mCurrentBlock;
	int32_t mCurrentBlockEncodedSize;
	int32_t mPositionInCurrentBlock;	// for reading out
	int32_t mBlockSize;					// Basic block size of most of the blocks in the file
	int32_t mLastBlockSize;				// the size (unencoded) of the last block in the file
	// Buffers
	uint8_t *mpRawBuffer;				// buffer for raw data
	BackupStoreFile::EncodingBuffer mEncodedBuffer;
										// buffer for encoded data
	int32_t mAllocatedBufferSize;		// size of above two allocated blocks
	uint64_t mEntryIVBase;				// base for block entry IV
};



#endif // BACKUPSTOREFILEENCODESTREAM__H

