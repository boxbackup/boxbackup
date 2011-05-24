// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileDiff.cpp
//		Purpose: Functions relating to diffing BackupStoreFiles
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include <new>
#include <map>

#ifdef HAVE_TIME_H
	#include <time.h>
#elif HAVE_SYS_TIME_H
	#include <sys/time.h>
#endif

#include "BackupStoreConstants.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileCryptVar.h"
#include "BackupStoreFileEncodeStream.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreObjectMagic.h"
#include "CommonException.h"
#include "FileStream.h"
#include "MD5Digest.h"
#include "RollingChecksum.h"
#include "Timer.h"

#include "MemLeakFindOn.h"

#include <cstring>

using namespace BackupStoreFileCryptVar;
using namespace BackupStoreFileCreation;

// By default, don't trace out details of the diff as we go along -- would fill up logs significantly.
// But it's useful for the test.
#ifndef BOX_RELEASE_BUILD
	bool BackupStoreFile::TraceDetailsOfDiffProcess = false;
#endif

static void LoadIndex(IOStream &rBlockIndex, int64_t ThisID, BlocksAvailableEntry **ppIndex, int64_t &rNumBlocksOut, int Timeout, bool &rCanDiffFromThis);
static void FindMostUsedSizes(BlocksAvailableEntry *pIndex, int64_t NumBlocks, int32_t Sizes[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES]);
static void SearchForMatchingBlocks(IOStream &rFile, 
	std::map<int64_t, int64_t> &rFoundBlocks, BlocksAvailableEntry *pIndex, 
	int64_t NumBlocks, int32_t Sizes[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES],
	DiffTimer *pDiffTimer);
static void SetupHashTable(BlocksAvailableEntry *pIndex, int64_t NumBlocks, int32_t BlockSize, BlocksAvailableEntry **pHashTable);
static bool SecondStageMatch(BlocksAvailableEntry *pFirstInHashList, RollingChecksum &fastSum, uint8_t *pBeginnings, uint8_t *pEndings, int Offset, int32_t BlockSize, int64_t FileBlockNumber,
BlocksAvailableEntry *pIndex, std::map<int64_t, int64_t> &rFoundBlocks);
static void GenerateRecipe(BackupStoreFileEncodeStream::Recipe &rRecipe, BlocksAvailableEntry *pIndex, int64_t NumBlocks, std::map<int64_t, int64_t> &rFoundBlocks, int64_t SizeOfInputFile);

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::MoveStreamPositionToBlockIndex(IOStream &)
//		Purpose: Move the file pointer in this stream to just before the block index.
//				 Assumes that the stream is at the beginning, seekable, and
//				 reading from the stream is OK.
//		Created: 12/1/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::MoveStreamPositionToBlockIndex(IOStream &rStream)
{
	// Size of file
	int64_t fileSize = rStream.BytesLeftToRead();

	// Get header
	file_StreamFormat hdr;

	// Read the header
	if(!rStream.ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */, IOStream::TimeOutInfinite))
	{
		// Couldn't read header
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Check magic number
	if(ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		&& ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V0
#endif
		)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Work out where the index is
	int64_t numBlocks = box_ntoh64(hdr.mNumBlocks);
	int64_t blockHeaderPosFromEnd = ((numBlocks * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader));
	
	// Sanity check
	if(blockHeaderPosFromEnd > static_cast<int64_t>(fileSize - sizeof(file_StreamFormat)))
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Seek to that position
	rStream.Seek(0 - blockHeaderPosFromEnd, IOStream::SeekType_End);
	
	// Done. Stream now in right position (as long as the file is formatted correctly)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::EncodeFileDiff(const char *, int64_t, const BackupStoreFilename &, int64_t, IOStream &, int64_t *)
//		Purpose: Similar to EncodeFile, but takes the object ID of the file it's
//				 diffing from, and the index of the blocks in a stream. It'll then
//				 calculate which blocks can be reused from that old file.
//				 The timeout is the timeout value for reading the diff block index.
//				 If pIsCompletelyDifferent != 0, it will be set to true if the
//				 the two files are completely different (do not share any block), false otherwise.
//				 
//		Created: 12/1/04
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupStoreFile::EncodeFileDiff
(
	const char *Filename, int64_t ContainerID,
	const BackupStoreFilename &rStoreFilename, int64_t DiffFromObjectID, 
	IOStream &rDiffFromBlockIndex, int Timeout, DiffTimer *pDiffTimer, 
	int64_t *pModificationTime, bool *pIsCompletelyDifferent)
{
	// Is it a symlink?
	{
		EMU_STRUCT_STAT st;
		if(EMU_LSTAT(Filename, &st) != 0)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
		if((st.st_mode & S_IFLNK) == S_IFLNK)
		{
			// Don't do diffs for symlinks
			if(pIsCompletelyDifferent != 0)
			{
				*pIsCompletelyDifferent = true;
			}
			return EncodeFile(Filename, ContainerID, rStoreFilename, pModificationTime);
		}
	}

	// Load in the blocks
	BlocksAvailableEntry *pindex = 0;
	int64_t blocksInIndex = 0;
	bool canDiffFromThis = false;
	LoadIndex(rDiffFromBlockIndex, DiffFromObjectID, &pindex, blocksInIndex, Timeout, canDiffFromThis);
	// BOX_TRACE("Diff: Blocks in index: " << blocksInIndex);
	
	if(!canDiffFromThis)
	{
		// Don't do diffing...
		if(pIsCompletelyDifferent != 0)
		{
			*pIsCompletelyDifferent = true;
		}
		return EncodeFile(Filename, ContainerID, rStoreFilename, pModificationTime);
	}
	
	// Pointer to recipe we're going to create
	BackupStoreFileEncodeStream::Recipe *precipe = 0;
	
	try
	{
		// Find which sizes should be scanned
		int32_t sizesToScan[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES];
		FindMostUsedSizes(pindex, blocksInIndex, sizesToScan);
		
		// Flag for reporting to the user
		bool completelyDifferent;
			
		// BLOCK
		{
			// Search the file to find matching blocks
			std::map<int64_t, int64_t> foundBlocks; // map of offset in file to index in block index
			int64_t sizeOfInputFile = 0;
			// BLOCK
			{
				FileStream file(Filename);
				// Get size of file
				sizeOfInputFile = file.BytesLeftToRead();
				// Find all those lovely matching blocks
				SearchForMatchingBlocks(file, foundBlocks, pindex, 
					blocksInIndex, sizesToScan, pDiffTimer);
				
				// Is it completely different?
				completelyDifferent = (foundBlocks.size() == 0);
			}
			
			// Create a recipe -- if the two files are completely different, don't put the from file ID in the recipe.
			precipe = new BackupStoreFileEncodeStream::Recipe(pindex, blocksInIndex, completelyDifferent?(0):(DiffFromObjectID));
			BlocksAvailableEntry *pindexKeptRef = pindex;	// we need this later, but must set pindex == 0 now, because of exceptions
			pindex = 0;		// Recipe now has ownership
			
			// Fill it in
			GenerateRecipe(*precipe, pindexKeptRef, blocksInIndex, foundBlocks, sizeOfInputFile);
		}
		// foundBlocks no longer required
		
		// Create the stream
		std::auto_ptr<IOStream> stream(new BackupStoreFileEncodeStream);
	
		// Do the initial setup
		((BackupStoreFileEncodeStream*)stream.get())->Setup(Filename, precipe, ContainerID, rStoreFilename, pModificationTime);
		precipe = 0;	// Stream has taken ownership of this
		
		// Tell user about completely different status?
		if(pIsCompletelyDifferent != 0)
		{
			*pIsCompletelyDifferent = completelyDifferent;
		}
	
		// Return the stream for the caller
		return stream;
	}
	catch(...)
	{
		// cleanup
		if(pindex != 0)
		{
			::free(pindex);
			pindex = 0;
		}
		if(precipe != 0)
		{
			delete precipe;
			precipe = 0;
		}
		throw;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static LoadIndex(IOStream &, int64_t, BlocksAvailableEntry **, int64_t, bool &)
//		Purpose: Read in an index, and decrypt, and store in the in memory block format.
//				 rCanDiffFromThis is set to false if the version of the from file is too old.
//		Created: 12/1/04
//
// --------------------------------------------------------------------------
static void LoadIndex(IOStream &rBlockIndex, int64_t ThisID, BlocksAvailableEntry **ppIndex, int64_t &rNumBlocksOut, int Timeout, bool &rCanDiffFromThis)
{
	// Reset
	rNumBlocksOut = 0;
	rCanDiffFromThis = false;
	
	// Read header
	file_BlockIndexHeader hdr;
	if(!rBlockIndex.ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */, Timeout))
	{
		// Couldn't read header
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	// Check against backwards comptaibility stuff
	if(hdr.mMagicValue == (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0))
	{
		// Won't diff against old version
		
		// Absorb rest of stream
		char buffer[2048];
		while(rBlockIndex.StreamDataLeft())
		{
			rBlockIndex.Read(buffer, sizeof(buffer), 1000 /* 1 sec timeout */);
		}
		
		// Tell caller
		rCanDiffFromThis = false;
		return;
	}
#endif

	// Check magic
	if(hdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1))
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Check that we're not trying to diff against a file which references blocks from another file
	if(((int64_t)box_ntoh64(hdr.mOtherFileID)) != 0)
	{
		THROW_EXCEPTION(BackupStoreException, CannotDiffAnIncompleteStoreFile)
	}

	// Mark as an acceptable diff.
	rCanDiffFromThis = true;

	// Get basic information
	int64_t numBlocks = box_ntoh64(hdr.mNumBlocks);
	uint64_t entryIVBase = box_ntoh64(hdr.mEntryIVBase);
	
	//TODO: Verify that these sizes look reasonable
	
	// Allocate space for the index
	BlocksAvailableEntry *pindex = (BlocksAvailableEntry*)::malloc(sizeof(BlocksAvailableEntry) * numBlocks);
	if(pindex == 0)
	{
		throw std::bad_alloc();
	}
	
	try
	{	
		for(int64_t b = 0; b < numBlocks; ++b)
		{
			// Read an entry from the stream
			file_BlockIndexEntry entry;
			if(!rBlockIndex.ReadFullBuffer(&entry, sizeof(entry), 0 /* not interested in bytes read if this fails */, Timeout))
			{
				// Couldn't read entry
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}	
		
			// Calculate IV for this entry
			uint64_t iv = entryIVBase;
			iv += b;
			// Network byte order
			iv = box_hton64(iv);
			sBlowfishDecryptBlockEntry.SetIV(&iv);			
			
			// Decrypt the encrypted section
			file_BlockIndexEntryEnc entryEnc;
			int sectionSize = sBlowfishDecryptBlockEntry.TransformBlock(&entryEnc, sizeof(entryEnc),
					entry.mEnEnc, sizeof(entry.mEnEnc));
			if(sectionSize != sizeof(entryEnc))
			{
				THROW_EXCEPTION(BackupStoreException, BlockEntryEncodingDidntGiveExpectedLength)
			}

			// Check that we're not trying to diff against a file which references blocks from another file
			if(((int64_t)box_ntoh64(entry.mEncodedSize)) <= 0)
			{
				THROW_EXCEPTION(BackupStoreException, CannotDiffAnIncompleteStoreFile)
			}
			
			// Store all the required information
			pindex[b].mpNextInHashList = 0;	// hash list not set up yet
			pindex[b].mSize = ntohl(entryEnc.mSize);
			pindex[b].mWeakChecksum = ntohl(entryEnc.mWeakChecksum);
			::memcpy(pindex[b].mStrongChecksum, entryEnc.mStrongChecksum, sizeof(pindex[b].mStrongChecksum));
		}
		
		// Store index pointer for called
		ASSERT(ppIndex != 0);
		*ppIndex = pindex;

		// Store number of blocks for caller
		rNumBlocksOut = numBlocks;	

	}
	catch(...)
	{
		// clean up and send the exception along its way
		::free(pindex);
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static FindMostUsedSizes(BlocksAvailableEntry *, int64_t, int32_t[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES])
//		Purpose: Finds the most commonly used block sizes in the index
//		Created: 12/1/04
//
// --------------------------------------------------------------------------
static void FindMostUsedSizes(BlocksAvailableEntry *pIndex, int64_t NumBlocks, int32_t Sizes[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES])
{
	// Array for lengths
	int64_t sizeCounts[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES];

	// Set arrays to lots of zeros (= unused entries)
	for(int l = 0; l < BACKUP_FILE_DIFF_MAX_BLOCK_SIZES; ++l)
	{
		Sizes[l] = 0;
		sizeCounts[l] = 0;
	}

	// Array for collecting sizes
	std::map<int32_t, int64_t> foundSizes;
	
	// Run through blocks and make a count of the entries
	for(int64_t b = 0; b < NumBlocks; ++b)
	{
		// Only if the block size is bigger than the minimum size we'll scan for
		if(pIndex[b].mSize > BACKUP_FILE_DIFF_MIN_BLOCK_SIZE)
		{
			// Find entry?
			std::map<int32_t, int64_t>::const_iterator f(foundSizes.find(pIndex[b].mSize));
			if(f != foundSizes.end())
			{
				// Increment existing entry
				foundSizes[pIndex[b].mSize] = foundSizes[pIndex[b].mSize] + 1;
			}
			else
			{
				// New entry
				foundSizes[pIndex[b].mSize] = 1;
			}
		}
	}
	
	// Make the block sizes
	for(std::map<int32_t, int64_t>::const_iterator i(foundSizes.begin()); i != foundSizes.end(); ++i)
	{
		// Find the position of the size in the array
		for(int t = 0; t < BACKUP_FILE_DIFF_MAX_BLOCK_SIZES; ++t)
		{
			// Instead of sorting on the raw count of blocks,
			// take the file area covered by this block size.
			if(i->second * i->first > sizeCounts[t] * Sizes[t])
			{
				// Then this size belong before this entry -- shuffle them up
				for(int s = (BACKUP_FILE_DIFF_MAX_BLOCK_SIZES - 1); s >= t; --s)
				{
					Sizes[s] = Sizes[s-1];
					sizeCounts[s] = sizeCounts[s-1];
				}
				
				// Insert this size
				Sizes[t] = i->first;
				sizeCounts[t] = i->second;
				
				// Shouldn't do any more searching
				break;
			}
		}
	}
	
	// trace the size table in debug builds
#ifndef BOX_RELEASE_BUILD
	if(BackupStoreFile::TraceDetailsOfDiffProcess)
	{
		for(int t = 0; t < BACKUP_FILE_DIFF_MAX_BLOCK_SIZES; ++t)
		{
			BOX_TRACE("Diff block size " << t << ": " <<
				Sizes[t] << " (count = " << 
				sizeCounts[t] << ")");
		}
	}
#endif
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    static SearchForMatchingBlocks(IOStream &, std::map<int64_t, int64_t> &, BlocksAvailableEntry *, int64_t, int32_t[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES])
//		Purpose: Find the matching blocks within the file.
//		Created: 12/1/04
//
// --------------------------------------------------------------------------
static void SearchForMatchingBlocks(IOStream &rFile, std::map<int64_t, int64_t> &rFoundBlocks,
	BlocksAvailableEntry *pIndex, int64_t NumBlocks, 
	int32_t Sizes[BACKUP_FILE_DIFF_MAX_BLOCK_SIZES], DiffTimer *pDiffTimer)
{
	Timer maximumDiffingTime(0, "MaximumDiffingTime");

	if(pDiffTimer && pDiffTimer->IsManaged())
	{
		maximumDiffingTime = Timer(pDiffTimer->GetMaximumDiffingTime(),
			"MaximumDiffingTime");
	}
	
	std::map<int64_t, int32_t> goodnessOfFit;

	// Allocate the hash lookup table
	BlocksAvailableEntry **phashTable = (BlocksAvailableEntry **)::malloc(sizeof(BlocksAvailableEntry *) * (64*1024));

	// Choose a size for the buffer, just a little bit more than the maximum block size
	int32_t bufSize = Sizes[0];
	for(int z = 1; z < BACKUP_FILE_DIFF_MAX_BLOCK_SIZES; ++z)
	{
		if(Sizes[z] > bufSize) bufSize = Sizes[z];
	}
	bufSize += 4;
	ASSERT(bufSize > Sizes[0]);
	ASSERT(bufSize > 0);
	if(bufSize > (BACKUP_FILE_MAX_BLOCK_SIZE + 1024))
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// TODO: Because we read in the file a scanned block size at a time,
	// it is likely to be inefficient. Probably will be much better to
	// calculate checksums for all block sizes in a single pass.

	// Allocate the buffers.
	uint8_t *pbuffer0 = (uint8_t *)::malloc(bufSize);
	uint8_t *pbuffer1 = (uint8_t *)::malloc(bufSize);
	try
	{
		// Check buffer allocation
		if(pbuffer0 == 0 || pbuffer1 == 0 || phashTable == 0)
		{
			// If a buffer got allocated, it will be cleaned up in the catch block
			throw std::bad_alloc();
		}
		
		// Flag to abort the run, if too many blocks are found -- avoid using
		// huge amounts of processor time when files contain many similar blocks.
		bool abortSearch = false;
		
		// Search for each block size in turn
		// NOTE: Do the smallest size first, so that the scheme for adding
		// entries in the found list works as expected and replaces smallers block
		// with larger blocks when it finds matches at the same offset in the file.
		for(int s = BACKUP_FILE_DIFF_MAX_BLOCK_SIZES - 1; s >= 0; --s)
		{
			ASSERT(Sizes[s] <= bufSize);
			BOX_TRACE("Diff pass " << s << ", for block size " <<
				Sizes[s]);
			
			// Check we haven't finished
			if(Sizes[s] == 0)
			{
				// empty entry, try next size
				continue;
			}
			
			// Set up the hash table entries
			SetupHashTable(pIndex, NumBlocks, Sizes[s], phashTable);
		
			// Shift file position to beginning
			rFile.Seek(0, IOStream::SeekType_Absolute);
			
			// Read first block
			if(rFile.Read(pbuffer0, Sizes[s]) != Sizes[s])
			{
				// Size of file too short to match -- do next size
				continue;
			}
			
			// Setup block pointers
			uint8_t *beginnings = pbuffer0;
			uint8_t *endings = pbuffer1;
			int offset = 0;
			
			// Calculate the first checksum, ready for rolling
			RollingChecksum rolling(beginnings, Sizes[s]);
			
			// Then roll, until the file is exhausted
			int64_t fileBlockNumber = 0;
			int64_t fileOffset = 0;
			int rollOverInitialBytes = 0;
			while(true)
			{
				if(maximumDiffingTime.HasExpired())
				{
					ASSERT(pDiffTimer != NULL);
					BOX_INFO("MaximumDiffingTime reached - "
						"suspending file diff");
					abortSearch = true;
					break;
				}
				
				if(pDiffTimer)
				{
					pDiffTimer->DoKeepAlive();
				}
				
				// Load in another block of data, and record how big it is
				int bytesInEndings = rFile.Read(endings, Sizes[s]);
				int tmp;

				// Skip any bytes from a previous matched block
				if(rollOverInitialBytes > 0 && offset < bytesInEndings)
				{
					int spaceLeft = bytesInEndings - offset;
					int thisRoll = (rollOverInitialBytes > spaceLeft) ? spaceLeft : rollOverInitialBytes;

					rolling.RollForwardSeveral(beginnings+offset, endings+offset, Sizes[s], thisRoll);

					offset += thisRoll;
					fileOffset += thisRoll;
					rollOverInitialBytes -= thisRoll;

					if(rollOverInitialBytes)
					{
						goto refresh;
					}
				}

				if(goodnessOfFit.count(fileOffset))
				{
					tmp = goodnessOfFit[fileOffset];
				}
				else
				{
					tmp = 0;
				}

				if(tmp >= Sizes[s])
				{
					// Skip over bigger ready-matched blocks completely
					rollOverInitialBytes = tmp;
					int spaceLeft = bytesInEndings - offset;
					int thisRoll = (rollOverInitialBytes > spaceLeft) ? spaceLeft : rollOverInitialBytes;

					rolling.RollForwardSeveral(beginnings+offset, endings+offset, Sizes[s], thisRoll);

					offset += thisRoll;
					fileOffset += thisRoll;
					rollOverInitialBytes -= thisRoll;

					if(rollOverInitialBytes)
					{
						goto refresh;
					}
				}

				while(offset < bytesInEndings)
				{
					// Is current checksum in hash list?
					uint16_t hash = rolling.GetComponentForHashing();
					if(phashTable[hash] != 0 && (goodnessOfFit.count(fileOffset) == 0 || goodnessOfFit[fileOffset] < Sizes[s]))
					{
						if(SecondStageMatch(phashTable[hash], rolling, beginnings, endings, offset, Sizes[s], fileBlockNumber, pIndex, rFoundBlocks))
						{
							BOX_TRACE("Found block match for " << hash << " of " << Sizes[s] << " bytes at offset " << fileOffset);
							goodnessOfFit[fileOffset] = Sizes[s];

							// Block matched, roll the checksum forward to the next block without doing
							// any more comparisons, because these are pointless (as any more matches will be ignored when
							// the recipe is generated) and just take up valuable processor time. Edge cases are
							// especially nasty, using huge amounts of time and memory.
							int skip = Sizes[s];
							if(offset < bytesInEndings && skip > 0)
							{
								int spaceLeft = bytesInEndings - offset;
								int thisRoll = (skip > spaceLeft) ? spaceLeft : skip;

								rolling.RollForwardSeveral(beginnings+offset, endings+offset, Sizes[s], thisRoll);

								offset += thisRoll;
								fileOffset += thisRoll;
								skip -= thisRoll;
							}
							// Not all the bytes necessary will have been skipped, so get them
							// skipped after the next block is loaded.
							rollOverInitialBytes = skip;
							
							// End this loop, so the final byte isn't used again
							break;
						}
						else
						{
							BOX_TRACE("False alarm match for " << hash << " of " << Sizes[s] << " bytes at offset " << fileOffset);
						}

						int64_t NumBlocksFound = static_cast<int64_t>(
							rFoundBlocks.size());
						int64_t MaxBlocksFound = NumBlocks * 
							BACKUP_FILE_DIFF_MAX_BLOCK_FIND_MULTIPLE;
						
						if(NumBlocksFound > MaxBlocksFound)
						{
							abortSearch = true;
							break;
						}
					}
					
					// Roll checksum forward
					rolling.RollForward(beginnings[offset], endings[offset], Sizes[s]);
				
					// Increment offsets
					++offset;
					++fileOffset;
				}
				
				if(abortSearch) break;
				
			refresh:
				// Finished?
				if(bytesInEndings != Sizes[s])
				{
					// No more data in file -- check the final block
					// (Do a copy and paste of 5 lines of code instead of introducing a comparison for
					// each byte of the file)
					uint16_t hash = rolling.GetComponentForHashing();
					if(phashTable[hash] != 0 && (goodnessOfFit.count(fileOffset) == 0 || goodnessOfFit[fileOffset] < Sizes[s]))
					{
						if(SecondStageMatch(phashTable[hash], rolling, beginnings, endings, offset, Sizes[s], fileBlockNumber, pIndex, rFoundBlocks))
						{
							goodnessOfFit[fileOffset] = Sizes[s];
						}
					}

					// finish
					break;
				}
				
				// Switch buffers, reset offset
				beginnings = endings;
				endings = (beginnings == pbuffer0)?(pbuffer1):(pbuffer0);	// ie the other buffer
				offset = 0;

				// And count the blocks which have been done
				++fileBlockNumber;
			}

			if(abortSearch) break;
		}
		
		// Free buffers and hash table
		::free(pbuffer1);
		pbuffer1 = 0;
		::free(pbuffer0);
		pbuffer0 = 0;
		::free(phashTable);
		phashTable = 0;
	}
	catch(...)
	{
		// Cleanup and throw
		if(pbuffer1 != 0) ::free(pbuffer1);
		if(pbuffer0 != 0) ::free(pbuffer0);
		if(phashTable != 0) ::free(phashTable);
		throw;
	}
	
#ifndef BOX_RELEASE_BUILD
	if(BackupStoreFile::TraceDetailsOfDiffProcess)
	{
		// Trace out the found blocks in debug mode
		BOX_TRACE("Diff: list of found blocks");
		BOX_TRACE("======== ======== ======== ========");
		BOX_TRACE("  Offset   BlkIdx     Size Movement");
		for(std::map<int64_t, int64_t>::const_iterator i(rFoundBlocks.begin()); i != rFoundBlocks.end(); ++i)
		{
			int64_t orgLoc = 0;
			for(int64_t b = 0; b < i->second; ++b)
			{
				orgLoc += pIndex[b].mSize;
			}
			BOX_TRACE(std::setw(8) << i->first << " " <<
				std::setw(8) << i->second << " " <<
				std::setw(8) << pIndex[i->second].mSize << 
				" " << 
				std::setw(8) << (i->first - orgLoc));
		}
		BOX_TRACE("======== ======== ======== ========");
	}
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static SetupHashTable(BlocksAvailableEntry *, int64_t, in32_t, BlocksAvailableEntry **)
//		Purpose: Set up the hash table ready for a scan
//		Created: 14/1/04
//
// --------------------------------------------------------------------------
static void SetupHashTable(BlocksAvailableEntry *pIndex, int64_t NumBlocks, int32_t BlockSize, BlocksAvailableEntry **pHashTable)
{
	// Set all entries in the hash table to zero
	::memset(pHashTable, 0, (sizeof(BlocksAvailableEntry *) * (64*1024)));

	// Scan through the blocks, building the hash table
	for(int64_t b = 0; b < NumBlocks; ++b)
	{
		// Only look at the required block size
		if(pIndex[b].mSize == BlockSize)
		{
			// Get the value under which to hash this entry
			uint16_t hash = RollingChecksum::ExtractHashingComponent(pIndex[b].mWeakChecksum);
			
			// Already present in table?
			if(pHashTable[hash] != 0)
			{
				//BOX_TRACE("Another hash entry for " << hash << " found");
				// Yes -- need to set the pointer in this entry to the current entry to build the linked list
				pIndex[b].mpNextInHashList = pHashTable[hash];
			}

			// Put a pointer to this entry in the hash table
			pHashTable[hash] = pIndex + b;
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static bool SecondStageMatch(xxx)
//		Purpose: When a match in the hash table is found, scan for second stage match using strong checksum.
//		Created: 14/1/04
//
// --------------------------------------------------------------------------
static bool SecondStageMatch(BlocksAvailableEntry *pFirstInHashList, RollingChecksum &fastSum, uint8_t *pBeginnings, uint8_t *pEndings,
	int Offset, int32_t BlockSize, int64_t FileBlockNumber, BlocksAvailableEntry *pIndex, std::map<int64_t, int64_t> &rFoundBlocks)
{
	// Check parameters
	ASSERT(pBeginnings != 0);
	ASSERT(pEndings != 0);
	ASSERT(Offset >= 0);
	ASSERT(BlockSize > 0);
	ASSERT(pFirstInHashList != 0);
	ASSERT(pIndex != 0);

#ifndef BOX_RELEASE_BUILD
	uint16_t DEBUG_Hash = fastSum.GetComponentForHashing();
#endif
	uint32_t Checksum = fastSum.GetChecksum();

	// Before we go to the expense of the MD5, make sure it's a darn good match on the checksum we already know.
	BlocksAvailableEntry *scan = pFirstInHashList;
	bool found=false;
	while(scan != 0)
	{
		if(scan->mWeakChecksum == Checksum)
		{
			found = true;
			break;
		}
		scan = scan->mpNextInHashList;
	}
	if(!found)
	{
		return false;
	}

	// Calculate the strong MD5 digest for this block
	MD5Digest strong;
	// Add the data from the beginnings
	strong.Add(pBeginnings + Offset, BlockSize - Offset);
	// Add any data from the endings
	if(Offset > 0)
	{
		strong.Add(pEndings, Offset);
	}
	strong.Finish();
	
	// Then go through the entries in the hash list, comparing with the strong digest calculated
	scan = pFirstInHashList;
	//BOX_TRACE("second stage match");
	while(scan != 0)
	{
		//BOX_TRACE("scan size " << scan->mSize <<
		//	", block size " << BlockSize <<
		//	", hash " << Hash);
		ASSERT(scan->mSize == BlockSize);
		ASSERT(RollingChecksum::ExtractHashingComponent(scan->mWeakChecksum) == DEBUG_Hash);
	
		// Compare?
		if(strong.DigestMatches(scan->mStrongChecksum))
		{
			//BOX_TRACE("Match!\n");
			// Found! Add to list of found blocks...
			int64_t fileOffset = (FileBlockNumber * BlockSize) + Offset;
			int64_t blockIndex = (scan - pIndex);	// pointer arthmitic is frowned upon. But most efficient way of doing it here -- alternative is to use more memory
			
			// We do NOT search for smallest blocks first, as this code originally assumed.
			// To prevent this from potentially overwriting a better match, the caller must determine
			// the relative "goodness" of any existing match and this one, and avoid the call if it
			// could be detrimental.
			rFoundBlocks[fileOffset] = blockIndex;
			
			// No point in searching further, report success
			return true;
		}
	
		// Next
		scan = scan->mpNextInHashList;
	}
	
	// Not matched
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static GenerateRecipe(BackupStoreFileEncodeStream::Recipe &, BlocksAvailableEntry *, int64_t, std::map<int64_t, int64_t> &)
//		Purpose: Fills in the recipe from the found block list
//		Created: 15/1/04
//
// --------------------------------------------------------------------------
static void GenerateRecipe(BackupStoreFileEncodeStream::Recipe &rRecipe, BlocksAvailableEntry *pIndex,
		int64_t NumBlocks, std::map<int64_t, int64_t> &rFoundBlocks, int64_t SizeOfInputFile)
{
	// NOTE: This function could be a lot more sophisiticated. For example, if
	// a small block overlaps a big block like this
	//   ****
	//      *******************************
	// then the small block will be used, not the big one. But it'd be better to
	// just ignore the small block and keep the big one. However, some stats should
	// be gathered about real world files before writing complex code which might
	// go wrong.

	// Initialise a blank instruction
	BackupStoreFileEncodeStream::RecipeInstruction instruction;
	#define RESET_INSTRUCTION			\
	instruction.mSpaceBefore = 0;		\
	instruction.mBlocks = 0;			\
	instruction.mpStartBlock = 0;
	RESET_INSTRUCTION

	// First, a special case for when there are no found blocks
	if(rFoundBlocks.size() == 0)
	{
		// No blocks, just a load of space
		instruction.mSpaceBefore = SizeOfInputFile;
		rRecipe.push_back(instruction);	
	
		#ifndef BOX_RELEASE_BUILD
		if(BackupStoreFile::TraceDetailsOfDiffProcess)
		{
			BOX_TRACE("Diff: Default recipe generated, " << 
				SizeOfInputFile << " bytes of file");
		}
		#endif
		
		// Don't do anything
		return;
	}

	// Current location
	int64_t loc = 0;

	// Then iterate through the list, generating the recipe
	std::map<int64_t, int64_t>::const_iterator i(rFoundBlocks.begin());
	ASSERT(i != rFoundBlocks.end());	// check logic

	// Counting for debug tracing
#ifndef BOX_RELEASE_BUILD
	int64_t debug_NewBytesFound = 0;
	int64_t debug_OldBlocksUsed = 0;
#endif
	
	for(; i != rFoundBlocks.end(); ++i)
	{
		// Remember... map is (position in file) -> (index of block in pIndex)
		
		if(i->first < loc)
		{
			// This block overlaps the last one
			continue;
		}
		else if(i->first > loc)
		{
			// There's a gap between the end of the last thing and this block.
			// If there's an instruction waiting, push it onto the list
			if(instruction.mSpaceBefore != 0 || instruction.mpStartBlock != 0)
			{
				rRecipe.push_back(instruction);
			}
			// Start a new instruction, with the gap ready
			RESET_INSTRUCTION
			instruction.mSpaceBefore = i->first - loc;
			// Move location forward to match
			loc += instruction.mSpaceBefore;
#ifndef BOX_RELEASE_BUILD
			debug_NewBytesFound += instruction.mSpaceBefore;
#endif
		}
		
		// First, does the current instruction need pushing back, because this block is not
		// sequential to the last one?
		if(instruction.mpStartBlock != 0 && (pIndex + i->second) != (instruction.mpStartBlock + instruction.mBlocks))
		{
			rRecipe.push_back(instruction);
			RESET_INSTRUCTION
		}
		
		// Add in this block
		if(instruction.mpStartBlock == 0)
		{
			// This block starts a new instruction
			instruction.mpStartBlock = pIndex + i->second;
			instruction.mBlocks = 1;
		}
		else
		{
			// It continues the previous section of blocks
			instruction.mBlocks += 1;
		}

#ifndef BOX_RELEASE_BUILD
		debug_OldBlocksUsed++;
#endif

		// Move location forward
		loc += pIndex[i->second].mSize;
	}
	
	// Push the last instruction generated
	rRecipe.push_back(instruction);
	
	// Is there any space left at the end which needs sending?
	if(loc != SizeOfInputFile)
	{
		RESET_INSTRUCTION
		instruction.mSpaceBefore = SizeOfInputFile - loc;
#ifndef BOX_RELEASE_BUILD
		debug_NewBytesFound += instruction.mSpaceBefore;
#endif
		rRecipe.push_back(instruction);		
	}
	
	// dump out the recipe
#ifndef BOX_RELEASE_BUILD
	BOX_TRACE("Diff: " << 
		debug_NewBytesFound << " new bytes found, " <<
		debug_OldBlocksUsed << " old blocks used");
	if(BackupStoreFile::TraceDetailsOfDiffProcess)
	{
		BOX_TRACE("Diff: Recipe generated (size " << rRecipe.size());
		BOX_TRACE("======== ========= ========");
		BOX_TRACE("Space b4 FirstBlk  NumBlks");
		{
			for(unsigned int e = 0; e < rRecipe.size(); ++e)
			{
				char b[64];
#ifdef WIN32
				sprintf(b, "%8I64d", (int64_t)(rRecipe[e].mpStartBlock - pIndex));
#else
				sprintf(b, "%8lld", (int64_t)(rRecipe[e].mpStartBlock - pIndex));
#endif
				BOX_TRACE(std::setw(8) <<
					rRecipe[e].mSpaceBefore <<
					" " <<
					((rRecipe[e].mpStartBlock == 0)?"       -":b) <<
					" " << std::setw(8) <<
					rRecipe[e].mBlocks);
			}
		}
		BOX_TRACE("======== ========= ========");
	}
#endif
}
