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
//		Name:    BackupStoreInfo.h
//		Purpose: Main backup store information storage
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREINFO__H
#define BACKUPSTOREINFO__H

#include <memory>
#include <string>
#include <vector>

class BackupStoreCheck;

// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreInfo
//		Purpose: Main backup store information storage
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
class BackupStoreInfo
{
	friend class BackupStoreCheck;
public:
	~BackupStoreInfo();
private:
	// Creation through static functions only
	BackupStoreInfo();
	// No copying allowed
	BackupStoreInfo(const BackupStoreInfo &);
	
public:
	// Create a New account, saving a blank info object to the disc
	static void CreateNew(int32_t AccountID, const std::string &rRootDir, int DiscSet, int64_t BlockSoftLimit, int64_t BlockHardLimit);
	
	// Load it from the store
	static std::auto_ptr<BackupStoreInfo> Load(int32_t AccountID, const std::string &rRootDir, int DiscSet, bool ReadOnly, int64_t *pRevisionID = 0);
	
	// Has info been modified?
	bool IsModified() const {return mIsModified;}
	
	// Save modified infomation back to store
	void Save();
	
	// Data access functions
	int32_t GetAccountID() const {return mAccountID;}
	int64_t GetLastObjectIDUsed() const {return mLastObjectIDUsed;}
	int64_t GetBlocksUsed() const {return mBlocksUsed;}
	int64_t GetBlocksInOldFiles() const {return mBlocksInOldFiles;}
	int64_t GetBlocksInDeletedFiles() const {return mBlocksInDeletedFiles;}
	int64_t GetBlocksInDirectories() const {return mBlocksInDirectories;}
	const std::vector<int64_t> &GetDeletedDirectories() const {return mDeletedDirectories;}
	int64_t GetBlocksSoftLimit() const {return mBlocksSoftLimit;}
	int64_t GetBlocksHardLimit() const {return mBlocksHardLimit;}
	bool IsReadOnly() const {return mReadOnly;}
	int GetDiscSetNumber() const {return mDiscSet;}
	
	// Data modification functions
	void ChangeBlocksUsed(int64_t Delta);
	void ChangeBlocksInOldFiles(int64_t Delta);
	void ChangeBlocksInDeletedFiles(int64_t Delta);
	void ChangeBlocksInDirectories(int64_t Delta);
	void CorrectAllUsedValues(int64_t Used, int64_t InOldFiles, int64_t InDeletedFiles, int64_t InDirectories);
	void AddDeletedDirectory(int64_t DirID);
	void RemovedDeletedDirectory(int64_t DirID);
	void ChangeLimits(int64_t BlockSoftLimit, int64_t BlockHardLimit);
	
	// Object IDs
	int64_t AllocateObjectID();
		
	// Client marker set and get
	int64_t GetClientStoreMarker() {return mClientStoreMarker;}
	void SetClientStoreMarker(int64_t ClientStoreMarker);

private:
	static std::auto_ptr<BackupStoreInfo> CreateForRegeneration(int32_t AccountID, const std::string &rRootDir,
		int DiscSet, int64_t LastObjectID, int64_t BlocksUsed, int64_t BlocksInOldFiles,
		int64_t BlocksInDeletedFiles, int64_t BlocksInDirectories, int64_t BlockSoftLimit, int64_t BlockHardLimit);

private:
	// Location information
	int32_t mAccountID;
	int mDiscSet;
	std::string mFilename;
	bool mReadOnly;
	bool mIsModified;
	
	// Client infomation
	int64_t mClientStoreMarker;
	
	// Account information
	int64_t mLastObjectIDUsed;
	int64_t mBlocksUsed;
	int64_t mBlocksInOldFiles;
	int64_t mBlocksInDeletedFiles;
	int64_t mBlocksInDirectories;
	int64_t mBlocksSoftLimit;
	int64_t mBlocksHardLimit;
	std::vector<int64_t> mDeletedDirectories;
};


#endif // BACKUPSTOREINFO__H


