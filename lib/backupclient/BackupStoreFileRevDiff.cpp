// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileRevDiff.cpp
//		Purpose: Reverse a patch, to build a new patch from new to old files
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
//		Name:    BackupStoreFile::ReverseDiffFile(IOStream &, IOStream &, IOStream &, IOStream &, int64_t)
//		Purpose: Reverse a patch, to build a new patch from new to old files. Takes
//				 two independent copies to the From file, for efficiency.
//		Created: 12/7/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::ReverseDiffFile(IOStream &rDiff, IOStream &rFrom, IOStream &rFrom2, IOStream &rOut, int64_t ObjectIDOfFrom, bool *pIsCompletelyDifferent)
{
	// Read and copy the header from the from file to the out file -- beginnings of the patch
	file_StreamFormat hdr;
	if(!rFrom.ReadFullBuffer(&hdr, sizeof(hdr), 0))
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
		filename.ReadFromStream(rFrom, IOStream::TimeOutInfinite);
		filename.WriteToStream(rOut);
		StreamableMemBlock attr;
		attr.ReadFromStream(rFrom, IOStream::TimeOutInfinite);
		attr.WriteToStream(rOut);
	}
	
	// Build an index of common blocks.
	// For each block in the from file, we want to know it's index in the 
	// diff file. Allocate memory for this information.
	int64_t fromNumBlocks = ntoh64(hdr.mNumBlocks);
	int64_t *pfromIndexInfo = (int64_t*)::malloc(fromNumBlocks * sizeof(int64_t));
	if(pfromIndexInfo == 0)
	{
		throw std::bad_alloc();
	}

	// Buffer data
	void *buffer = 0;
	int bufferSize = 0;	
	
	// flag
	bool isCompletelyDifferent = true;
	
	try
	{
		// Initialise the index to be all 0, ie not filled in yet
		for(int64_t i = 0; i < fromNumBlocks; ++i)
		{
			pfromIndexInfo[i] = 0;
		}
	
		// Within the from file, skip to the index
		MoveStreamPositionToBlockIndex(rDiff);

		// Read in header of index
		file_BlockIndexHeader diffIdxHdr;
		if(!rDiff.ReadFullBuffer(&diffIdxHdr, sizeof(diffIdxHdr), 0))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		if(ntohl(diffIdxHdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
		{
			THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
		}

		// And then read in each entry
		int64_t diffNumBlocks = ntoh64(diffIdxHdr.mNumBlocks);
		for(int64_t b = 0; b < diffNumBlocks; ++b)
		{
			file_BlockIndexEntry e;
			if(!rDiff.ReadFullBuffer(&e, sizeof(e), 0))
			{
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}

			// Where's the block?
			int64_t blockEn = ntoh64(e.mEncodedSize);
			if(blockEn > 0)
			{
				// Block is in the delta file, is ignored for now -- not relevant to rebuilding the from file
			}
			else
			{
				// Block is in the original file, store which block it is in this file
				int64_t fromIndex = 0 - blockEn;
				if(fromIndex < 0 || fromIndex >= fromNumBlocks)
				{
					THROW_EXCEPTION(BackupStoreException, IncompatibleFromAndDiffFiles)
				}
				
				// Store information about where it is in the new file
				// NOTE: This is slight different to how it'll be stored in the final index.
				pfromIndexInfo[fromIndex] = -1 - b;
			}
		}
		
		// Open the index for the second copy of the from file
		MoveStreamPositionToBlockIndex(rFrom2);

		// Read in header of index
		file_BlockIndexHeader fromIdxHdr;
		if(!rFrom2.ReadFullBuffer(&fromIdxHdr, sizeof(fromIdxHdr), 0))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		if(ntohl(fromIdxHdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1
			|| ntoh64(fromIdxHdr.mOtherFileID) != 0)
		{
			THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
		}

		// So, we can now start building the data in the file
		int64_t filePosition = rFrom.GetPosition();
		for(int64_t b = 0; b < fromNumBlocks; ++b)
		{
			// Read entry from from index
			file_BlockIndexEntry e;
			if(!rFrom2.ReadFullBuffer(&e, sizeof(e), 0))
			{
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}

			// Get size
			int64_t blockSize = hton64(e.mEncodedSize);
			if(blockSize < 0)
			{
				THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
			}
		
			// Copy this block?
			if(pfromIndexInfo[b] == 0)
			{
				// Copy it, first move to file location
				rFrom.Seek(filePosition, IOStream::SeekType_Absolute);
				
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
				
				// Copy the block
				if(!rFrom.ReadFullBuffer(buffer, blockSize, 0))
				{
					THROW_EXCEPTION(BackupStoreException, FailedToReadBlockOnCombine)
				}
				rOut.Write(buffer, blockSize);

				// Store the size
				pfromIndexInfo[b] = blockSize;
			}
			else
			{
				// Block isn't needed, so it's not completely different
				isCompletelyDifferent = false;
			}
			filePosition += blockSize;
		}
		
		// Then write the index, modified header first
		fromIdxHdr.mOtherFileID = isCompletelyDifferent?0:(hton64(ObjectIDOfFrom));
		rOut.Write(&fromIdxHdr, sizeof(fromIdxHdr));

		// Move to start of index entries
		rFrom.Seek(filePosition + sizeof(file_BlockIndexHeader), IOStream::SeekType_Absolute);
		
		// Then copy modified entries
		for(int64_t b = 0; b < fromNumBlocks; ++b)
		{
			// Read entry from from index
			file_BlockIndexEntry e;
			if(!rFrom.ReadFullBuffer(&e, sizeof(e), 0))
			{
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}
			
			// Modify...
			int64_t s = pfromIndexInfo[b];
			// Adjust to reflect real block index (remember 0 has a different meaning here)
			if(s < 0) ++s;
			// Insert
			e.mEncodedSize = hton64(s);
			// Write
			rOut.Write(&e, sizeof(e));
		}
	}
	catch(...)
	{
		::free(pfromIndexInfo);
		if(buffer != 0)
		{
			::free(buffer);
		}
		throw;
	}

	// Free memory used (oh for finally {} blocks)
	::free(pfromIndexInfo);
	if(buffer != 0)
	{
		::free(buffer);
	}
	
	// return completely different flag
	if(pIsCompletelyDifferent != 0)
	{
		*pIsCompletelyDifferent = isCompletelyDifferent;
	}
}



