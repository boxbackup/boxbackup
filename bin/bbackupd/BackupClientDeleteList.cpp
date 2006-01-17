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
//		Name:    BackupClientDeleteList.cpp
//		Purpose: List of pending deletes for backup
//		Created: 10/11/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>

#include "BackupClientDeleteList.h"
#include "BackupClientContext.h"
#include "autogen_BackupProtocolClient.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDeleteList::BackupClientDeleteList()
//		Purpose: Constructor
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
BackupClientDeleteList::BackupClientDeleteList()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDeleteList::~BackupClientDeleteList()
//		Purpose: Destructor
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
BackupClientDeleteList::~BackupClientDeleteList()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDeleteList::AddDirectoryDelete(int64_t)
//		Purpose: Add a directory to the list of directories to be deleted.
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
void BackupClientDeleteList::AddDirectoryDelete(int64_t ObjectID)
{
	// Only add the delete to the list if it's not in the "no delete" set
	if(mDirectoryNoDeleteList.find(ObjectID) == mDirectoryNoDeleteList.end())
	{
		// Not in the list, so should delete it
		mDirectoryList.push_back(ObjectID);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDeleteList::AddFileDelete(int64_t, BackupStoreFilenameClear &)
//		Purpose: 
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
void BackupClientDeleteList::AddFileDelete(int64_t DirectoryID, const BackupStoreFilename &rFilename)
{
	// Try to find it in the no delete list
	std::vector<std::pair<int64_t, BackupStoreFilename> >::iterator delEntry(mFileNoDeleteList.begin());
	while(delEntry != mFileNoDeleteList.end())
	{
		if((delEntry)->first == DirectoryID && (delEntry)->second == rFilename)
		{
			// Found!
			break;
		}
		++delEntry;
	}
	
	// Only add it to the delete list if it wasn't in the no delete list
	if(delEntry == mFileNoDeleteList.end())
	{
		mFileList.push_back(std::pair<int64_t, BackupStoreFilename>(DirectoryID, rFilename));
	}
}


	
// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDeleteList::PerformDeletions(BackupClientContext &rContext)
//		Purpose: Perform all the pending deletes
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
void BackupClientDeleteList::PerformDeletions(BackupClientContext &rContext)
{
	// Anything to do?
	if(mDirectoryList.empty() && mFileList.empty())
	{
		// Nothing!
		return;
	}
	
	// Get a connection
	BackupProtocolClient &connection(rContext.GetConnection());
	
	// Do the deletes
	for(std::vector<int64_t>::iterator i(mDirectoryList.begin()); i != mDirectoryList.end(); ++i)
	{
		connection.QueryDeleteDirectory(*i);
	}
	
	// Clear the directory list
	mDirectoryList.clear();
	
	// Delete the files
	for(std::vector<std::pair<int64_t, BackupStoreFilename> >::iterator i(mFileList.begin()); i != mFileList.end(); ++i)
	{
		connection.QueryDeleteFile(i->first, i->second);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDeleteList::StopDirectoryDeletion(int64_t)
//		Purpose: Stop a directory being deleted
//		Created: 19/11/03
//
// --------------------------------------------------------------------------
void BackupClientDeleteList::StopDirectoryDeletion(int64_t ObjectID)
{
	// First of all, is it in the delete vector?
	std::vector<int64_t>::iterator delEntry(std::find(mDirectoryList.begin(), mDirectoryList.end(), ObjectID));
	if(delEntry != mDirectoryList.end())
	{
		// erase this entry
		mDirectoryList.erase(delEntry);
	}
	else
	{
		// Haven't been asked to delete it yet, put it in the no delete list
		mDirectoryNoDeleteList.insert(ObjectID);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDeleteList::StopFileDeletion(int64_t, const BackupStoreFilename &)
//		Purpose: Stop a file from being deleted
//		Created: 19/11/03
//
// --------------------------------------------------------------------------
void BackupClientDeleteList::StopFileDeletion(int64_t DirectoryID, const BackupStoreFilename &rFilename)
{
	// Find this in the delete list
	std::vector<std::pair<int64_t, BackupStoreFilename> >::iterator delEntry(mFileList.begin());
	while(delEntry != mFileList.end())
	{
		if((delEntry)->first == DirectoryID && (delEntry)->second == rFilename)
		{
			// Found!
			break;
		}
		++delEntry;
	}
	
	if(delEntry != mFileList.end())
	{
		// erase this entry
		mFileList.erase(delEntry);
	}
	else
	{
		// Haven't been asked to delete it yet, put it in the no delete list
		mFileNoDeleteList.push_back(std::pair<int64_t, BackupStoreFilename>(DirectoryID, rFilename));
	}

}





