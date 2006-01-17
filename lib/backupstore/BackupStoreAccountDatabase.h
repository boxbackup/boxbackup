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
//		Name:    BackupStoreAccountDatabase.h
//		Purpose: Database of accounts for the backup store
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREACCOUNTDATABASE__H
#define BACKUPSTOREACCOUNTDATABASE__H

#include <memory>
#include <vector>

#include "BoxTime.h"

class _BackupStoreAccountDatabase;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreAccountDatabase
//		Purpose: Database of accounts for the backup store
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
class BackupStoreAccountDatabase
{
public:
	friend class _BackupStoreAccountDatabase;	// to stop compiler warnings
	~BackupStoreAccountDatabase();
private:
	BackupStoreAccountDatabase(const char *Filename);
	BackupStoreAccountDatabase(const BackupStoreAccountDatabase &);
public:

	static std::auto_ptr<BackupStoreAccountDatabase> Read(const char *Filename);
	void Write();

	class Entry
	{
	public:
		Entry();
		Entry(int32_t ID, int DiscSet);
		Entry(const Entry &rEntry);
		~Entry();

		int32_t GetID() const {return mID;}
		int GetDiscSet() const {return mDiscSet;}
		
	private:
		int32_t mID;
		int mDiscSet;
	};

	bool EntryExists(int32_t ID) const;
	const Entry &GetEntry(int32_t ID) const;
	void AddEntry(int32_t ID, int DiscSet);
	void DeleteEntry(int32_t ID);

	// This interface should change in the future. But for now it'll do.
	void GetAllAccountIDs(std::vector<int32_t> &rIDsOut);

private:
	void ReadFile() const;	// const in concept only
	void CheckUpToDate() const;	// const in concept only
	box_time_t GetDBFileModificationTime() const;

private:
	mutable _BackupStoreAccountDatabase *pImpl;
};

#endif // BACKUPSTOREACCOUNTDATABASE__H

