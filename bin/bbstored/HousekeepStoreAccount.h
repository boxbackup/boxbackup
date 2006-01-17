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
//		Name:    HousekeepStoreAccount.h
//		Purpose: Action class to perform housekeeping on a store account
//		Created: 11/12/03
//
// --------------------------------------------------------------------------

#ifndef HOUSEKEEPSTOREACCOUNT__H
#define HOUSEKEEPSTOREACCOUNT__H

#include <string>
#include <set>
#include <vector>

class BackupStoreDaemon;
class BackupStoreDirectory;


// --------------------------------------------------------------------------
//
// Class
//		Name:    HousekeepStoreAccount
//		Purpose: Action class to perform housekeeping on a store account
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
class HousekeepStoreAccount
{
public:
	HousekeepStoreAccount(int AccountID, const std::string &rStoreRoot, int StoreDiscSet, BackupStoreDaemon &rDaemon);
	~HousekeepStoreAccount();
	
	void DoHousekeeping();
	
	
private:
	// utility functions
	void MakeObjectFilename(int64_t ObjectID, std::string &rFilenameOut);

	bool ScanDirectory(int64_t ObjectID);
	bool DeleteFiles();
	bool DeleteEmptyDirectories();
	void DeleteFile(int64_t InDirectory, int64_t ObjectID, BackupStoreDirectory &rDirectory, const std::string &rDirectoryFilename, int64_t OriginalDirSizeInBlocks);

private:
	typedef struct
	{
		int64_t mObjectID;
		int64_t mInDirectory;
		int64_t mSizeInBlocks;
		int32_t mMarkNumber;
		int32_t mVersionAgeWithinMark;	// 0 == current, 1 latest old version, etc
	} DelEn;
	
	struct DelEnCompare
	{
		bool operator()(const DelEn &x, const DelEn &y);
	};
	
	int mAccountID;
	std::string mStoreRoot;
	int mStoreDiscSet;
	BackupStoreDaemon &mrDaemon;
	
	int64_t mDeletionSizeTarget;
	
	std::set<DelEn, DelEnCompare> mPotentialDeletions;
	int64_t mPotentialDeletionsTotalSize;
	int64_t mMaxSizeInPotentialDeletions;
	
	// List of directories which are empty, and might be good for deleting
	std::vector<int64_t> mEmptyDirectories;
	
	// The re-calculated blocks used stats
	int64_t mBlocksUsed;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;

	// Deltas from deletion
	int64_t mBlocksUsedDelta;
	int64_t mBlocksInOldFilesDelta;
	int64_t mBlocksInDeletedFilesDelta;
	int64_t mBlocksInDirectoriesDelta;
	
	// Deletion count
	int64_t mFilesDeleted;
	int64_t mEmptyDirectoriesDeleted;
	
	// Poll frequency
	int mCountUntilNextInterprocessMsgCheck;
};

#endif // HOUSEKEEPSTOREACCOUNT__H

