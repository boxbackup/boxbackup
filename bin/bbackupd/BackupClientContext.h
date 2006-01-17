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
//		Name:    BackupClientContext.h
//		Purpose: Keep track of context
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTCONTEXT__H
#define BACKUPCLIENTCONTEXT__H

#include "BoxTime.h"
#include "BackupClientDeleteList.h"
#include "ExcludeList.h"

class TLSContext;
class BackupProtocolClient;
class SocketStreamTLS;
class BackupClientInodeToIDMap;
class BackupDaemon;
class BackupStoreFilenameClear;

#include <string>

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupClientContext
//		Purpose: 
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class BackupClientContext
{
public:
	BackupClientContext(BackupDaemon &rDaemon, TLSContext &rTLSContext, const std::string &rHostname,
		int32_t AccountNumber, bool ExtendedLogging);
	~BackupClientContext();
private:
	BackupClientContext(const BackupClientContext &);
public:

	BackupProtocolClient &GetConnection();
	
	void CloseAnyOpenConnection();
	
	int GetTimeout() const;
	
	BackupClientDeleteList &GetDeleteList();
	void PerformDeletions();

	enum
	{
		ClientStoreMarker_NotKnown = 0
	};

	void SetClientStoreMarker(int64_t ClientStoreMarker) {mClientStoreMarker = ClientStoreMarker;}
	int64_t GetClientStoreMarker() const {return mClientStoreMarker;}
	
	bool StorageLimitExceeded() {return mStorageLimitExceeded;}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetIDMaps(const BackupClientInodeToIDMap *, BackupClientInodeToIDMap *)
	//		Purpose: Store pointers to the Current and New ID maps
	//		Created: 11/11/03
	//
	// --------------------------------------------------------------------------
	void SetIDMaps(const BackupClientInodeToIDMap *pCurrent, BackupClientInodeToIDMap *pNew)
	{
		ASSERT(pCurrent != 0);
		ASSERT(pNew != 0);
		mpCurrentIDMap = pCurrent;
		mpNewIDMap = pNew;
	}
	const BackupClientInodeToIDMap &GetCurrentIDMap() const;
	BackupClientInodeToIDMap &GetNewIDMap() const;
	
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetExcludeLists(ExcludeList *, ExcludeList *)
	//		Purpose: Sets the exclude lists for the operation. Can be 0.
	//		Created: 28/1/04
	//
	// --------------------------------------------------------------------------
	void SetExcludeLists(ExcludeList *pExcludeFiles, ExcludeList *pExcludeDirs)
	{
		mpExcludeFiles = pExcludeFiles;
		mpExcludeDirs = pExcludeDirs;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::ExcludeFile(const std::string &)
	//		Purpose: Returns true is this file should be excluded from the backup
	//		Created: 28/1/04
	//
	// --------------------------------------------------------------------------
	inline bool ExcludeFile(const std::string &rFullFilename)
	{
		if(mpExcludeFiles != 0)
		{
			return mpExcludeFiles->IsExcluded(rFullFilename);
		}
		// If no list, don't exclude anything
		return false;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::ExcludeDir(const std::string &)
	//		Purpose: Returns true is this directory should be excluded from the backup
	//		Created: 28/1/04
	//
	// --------------------------------------------------------------------------
	inline bool ExcludeDir(const std::string &rFullDirName)
	{
		if(mpExcludeDirs != 0)
		{
			return mpExcludeDirs->IsExcluded(rFullDirName);
		}
		// If no list, don't exclude anything
		return false;
	}

	// Utility functions -- may do a lot of work
	bool FindFilename(int64_t ObjectID, int64_t ContainingDirectory, std::string &rPathOut, bool &rIsDirectoryOut,
		bool &rIsCurrentVersionOut, box_time_t *pModTimeOnServer = 0, box_time_t *pAttributesHashOnServer = 0,
		BackupStoreFilenameClear *pLeafname = 0); // not const as may connect to server

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetMaximumDiffingTime()
	//		Purpose: Sets the maximum time that will be spent diffing a file
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	static void SetMaximumDiffingTime(int iSeconds);

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::SetKeepAliveTime()
	//		Purpose: Sets the time interval for repetitive keep-alive operation
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	static void SetKeepAliveTime(int iSeconds);

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::ManageDiffProcess()
	//		Purpose: Initiates an SSL connection/session keep-alive process
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	void ManageDiffProcess();

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::UnManageDiffProcess()
	//		Purpose: Suspends an SSL connection/session keep-alive process
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	void UnManageDiffProcess();

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupClientContext::DoKeepAlive()
	//		Purpose: Does something inconsequential over the SSL link to keep it up
	//		Created: 04/19/2005
	//
	// --------------------------------------------------------------------------
	void DoKeepAlive();
private:
	BackupDaemon &mrDaemon;
	TLSContext &mrTLSContext;
	std::string mHostname;
	int32_t mAccountNumber;
	SocketStreamTLS *mpSocket;
	BackupProtocolClient *mpConnection;
	bool mExtendedLogging;
	int64_t mClientStoreMarker;
	BackupClientDeleteList *mpDeleteList;
	const BackupClientInodeToIDMap *mpCurrentIDMap;
	BackupClientInodeToIDMap *mpNewIDMap;
	bool mStorageLimitExceeded;
	ExcludeList *mpExcludeFiles;
	ExcludeList *mpExcludeDirs;

	bool mbIsManaged;
};


#endif // BACKUPCLIENTCONTEXT__H

