// --------------------------------------------------------------------------
//
// File
//		Name:    StoreStructure.cpp
//		Purpose: 
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "StoreStructure.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "RaidFileController.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    StoreStructure::MakeObjectFilename(int64_t, const std::string &, int, std::string &, bool)
//		Purpose: Builds the object filename for a given object, given a root. Optionally ensure that the
//				 directory exists.
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void StoreStructure::MakeObjectFilename(int64_t ObjectID, const std::string &rStoreRoot, int DiscSet, std::string &rFilenameOut, bool EnsureDirectoryExists)
{
	const static char *hex = "0123456789abcdef";

	// Set output to root string
	rFilenameOut = rStoreRoot;

	// get the id value from the stored object ID so we can do
	// bitwise operations on it.
	uint64_t id = (uint64_t)ObjectID;

	// get leafname, shift the bits which make up the leafname off
	unsigned int leafname(id & STORE_ID_SEGMENT_MASK);
	id >>= STORE_ID_SEGMENT_LENGTH;

	// build pathname
	while(id != 0)
	{
		// assumes that the segments are no bigger than 8 bits
		int v = id & STORE_ID_SEGMENT_MASK;
		rFilenameOut += hex[(v & 0xf0) >> 4];
		rFilenameOut += hex[v & 0xf];
		rFilenameOut += DIRECTORY_SEPARATOR_ASCHAR;

		// shift the bits we used off the pathname
		id >>= STORE_ID_SEGMENT_LENGTH;
	}
	
	// Want to make sure this exists?
	if(EnsureDirectoryExists)
	{
		if(!RaidFileRead::DirectoryExists(DiscSet, rFilenameOut))
		{
			// Create it
			RaidFileWrite::CreateDirectory(DiscSet, rFilenameOut, true /* recusive */);
		}
	}

	// append the filename
	rFilenameOut += 'o';
	rFilenameOut += hex[(leafname & 0xf0) >> 4];
	rFilenameOut += hex[leafname & 0xf];
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    StoreStructure::MakeWriteLockFilename(const std::string &, int, std::string &)
//		Purpose: Generate the on disc filename of the write lock file
//		Created: 15/12/03
//
// --------------------------------------------------------------------------
void StoreStructure::MakeWriteLockFilename(const std::string &rStoreRoot, int DiscSet, std::string &rFilenameOut)
{
	// Find the disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(DiscSet));
	
	// Make the filename
	std::string writeLockFile(rdiscSet[0] + DIRECTORY_SEPARATOR + rStoreRoot + "write.lock");

	// Return it to the caller
	rFilenameOut = writeLockFile;
}


