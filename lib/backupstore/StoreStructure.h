// --------------------------------------------------------------------------
//
// File
//		Name:    StoreStructure.h
//		Purpose: Functions for placing files in the store
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#ifndef STORESTRUCTURE__H
#define STORESTRUCTURE__H

#include <string>

#ifdef NDEBUG
	#define STORE_ID_SEGMENT_LENGTH		8
	#define STORE_ID_SEGMENT_MASK		0xff
#else
	// Debug we'll use lots and lots of directories to stress things
	#define STORE_ID_SEGMENT_LENGTH		2
	#define STORE_ID_SEGMENT_MASK		0x03
#endif


namespace StoreStructure
{
	void MakeObjectFilename(int64_t ObjectID, const std::string &rStoreRoot, int DiscSet, std::string &rFilenameOut, bool EnsureDirectoryExists);
	void MakeWriteLockFilename(const std::string &rStoreRoot, int DiscSet, std::string &rFilenameOut);
};

#endif // STORESTRUCTURE__H

