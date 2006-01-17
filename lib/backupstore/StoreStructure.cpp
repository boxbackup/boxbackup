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


