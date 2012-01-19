// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreCheckData.cpp
//		Purpose: Data handling for store checking
//		Created: 21/4/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <memory>

#include "BackupStoreCheck.h"
#include "autogen_BackupStoreException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::FreeInfo()
//		Purpose: Free all the data stored
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::FreeInfo()
{
	// Free all the blocks
	for(Info_t::iterator i(mInfo.begin()); i != mInfo.end(); ++i)
	{
		::free(i->second);
	}
	
	// Clear the contents of the map
	mInfo.clear();
	
	// Reset the last ID, just in case
	mpInfoLastBlock = 0;
	mInfoLastBlockEntries = 0;
	mLastIDInInfo = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::AddID(BackupStoreCheck_ID_t, BackupStoreCheck_ID_t, bool)
//		Purpose: Add an ID to the list
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::AddID(BackupStoreCheck_ID_t ID,
	BackupStoreCheck_ID_t Container, BackupStoreCheck_Size_t ObjectSize, bool IsFile)
{
	// Check ID is OK.
	if(ID <= mLastIDInInfo)
	{
		THROW_EXCEPTION(BackupStoreException, InternalAlgorithmErrorCheckIDNotMonotonicallyIncreasing)
	}
	
	// Can this go in the current block?
	if(mpInfoLastBlock == 0 || mInfoLastBlockEntries >= BACKUPSTORECHECK_BLOCK_SIZE)
	{
		// No. Allocate a new one
		IDBlock *pblk = (IDBlock*)::malloc(sizeof(IDBlock));
		if(pblk == 0)
		{
			throw std::bad_alloc();
		}
		// Zero all the flags entries
		for(int z = 0; z < (BACKUPSTORECHECK_BLOCK_SIZE * Flags__NumFlags / Flags__NumItemsPerEntry); ++z)
		{
			pblk->mFlags[z] = 0;
		} 
		// Store in map
		mInfo[ID] = pblk;
		// Allocated and stored OK, setup for use
		mpInfoLastBlock = pblk;
		mInfoLastBlockEntries = 0;
	}
	ASSERT(mpInfoLastBlock != 0 && mInfoLastBlockEntries < BACKUPSTORECHECK_BLOCK_SIZE);
	
	// Add to block
	mpInfoLastBlock->mID[mInfoLastBlockEntries] = ID;
	mpInfoLastBlock->mContainer[mInfoLastBlockEntries] = Container;
	mpInfoLastBlock->mObjectSizeInBlocks[mInfoLastBlockEntries] = ObjectSize;
	SetFlags(mpInfoLastBlock, mInfoLastBlockEntries, IsFile?(0):(Flags_IsDir));
	
	// Increment size
	++mInfoLastBlockEntries;
	
	// Store last ID
	mLastIDInInfo = ID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::LookupID(BackupStoreCheck_ID_t, int32_t
//		Purpose: Look up an ID. Return the block it's in, or zero if not found, and the 
//				 index within that block if the thing is found.
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
BackupStoreCheck::IDBlock *BackupStoreCheck::LookupID(BackupStoreCheck_ID_t ID, int32_t &rIndexOut)
{
	IDBlock *pblock = 0;

	// Find the lower matching block who's first entry is not less than ID
	Info_t::const_iterator ib(mInfo.lower_bound(ID));
	
	// Was there a block
	if(ib == mInfo.end())
	{
		// Block wasn't found... could be in last block
		pblock = mpInfoLastBlock;
	}
	else
	{
		// Found it as first entry?
		if(ib->first == ID)
		{
			rIndexOut = 0;
			return ib->second;
		}
		
		// Go back one block as it's not the first entry in this one
		if(ib == mInfo.begin())
		{
			// Was first block, can't go back
			return 0;
		}
		// Go back...
		--ib;

		// So, the ID will be in this block, if it's in anything
		pblock = ib->second;
	}

	ASSERT(pblock != 0);
	if(pblock == 0) return 0;
	
	// How many entries are there in the block
	int32_t bentries = (pblock == mpInfoLastBlock)?mInfoLastBlockEntries:BACKUPSTORECHECK_BLOCK_SIZE;
	
	// Do binary search within block
	int high = bentries;
	int low = -1;
	while(high - low > 1)
	{
		int i = (high + low) / 2;
		if(ID <= pblock->mID[i])
		{
			high = i;
		}
		else
		{
			low = i;
		}
	}
	if(ID == pblock->mID[high])
	{
		// Found
		rIndexOut = high;
		return pblock;
	}

	// Not found
	return 0;
}


#ifndef BOX_RELEASE_BUILD
// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreCheck::DumpObjectInfo()
//		Purpose: Debug only. Trace out all object info.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreCheck::DumpObjectInfo()
{
	for(Info_t::const_iterator i(mInfo.begin()); i != mInfo.end(); ++i)
	{
		IDBlock *pblock = i->second;
		int32_t bentries = (pblock == mpInfoLastBlock)?mInfoLastBlockEntries:BACKUPSTORECHECK_BLOCK_SIZE;
		BOX_TRACE("BLOCK @ " << BOX_FORMAT_HEX32(pblock) <<
			", " << bentries << " entries");
		
		for(int e = 0; e < bentries; ++e)
		{
			uint8_t flags = GetFlags(pblock, e);
			BOX_TRACE(std::hex << 
				"id "  << pblock->mID[e] <<
				", c " << pblock->mContainer[e] <<
				", " << ((flags & Flags_IsDir)?"dir":"file") <<
				", " << ((flags & Flags_IsContained) ? 
					"contained":"unattached"));
		}
	}
}
#endif

