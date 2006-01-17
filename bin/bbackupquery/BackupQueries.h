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
//		Name:    BackupQueries.h
//		Purpose: Perform various queries on the backup store server.
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------

#ifndef BACKUPQUERIES__H
#define BACKUPQUERIES__H

#include <vector>
#include <string>

#include "BoxTime.h"

class BackupProtocolClient;
class Configuration;
class ExcludeList;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupQueries
//		Purpose: Perform various queries on the backup store server.
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
class BackupQueries
{
public:
	BackupQueries(BackupProtocolClient &rConnection, const Configuration &rConfiguration);
	~BackupQueries();
private:
	BackupQueries(const BackupQueries &);
public:

	void DoCommand(const char *Command);

	// Ready to stop?
	bool Stop() {return mQuitNow;}
	
	// Return code?
	int GetReturnCode() {return mReturnCode;}

private:
	// Commands
	void CommandList(const std::vector<std::string> &args, const bool *opts);
	void CommandChangeDir(const std::vector<std::string> &args, const bool *opts);
	void CommandChangeLocalDir(const std::vector<std::string> &args);
	void CommandGetObject(const std::vector<std::string> &args, const bool *opts);
	void CommandGet(const std::vector<std::string> &args, const bool *opts);
	void CommandCompare(const std::vector<std::string> &args, const bool *opts);
	void CommandRestore(const std::vector<std::string> &args, const bool *opts);
	void CommandUndelete(const std::vector<std::string> &args, const bool *opts);
	void CommandUsage();
	void CommandUsageDisplayEntry(const char *Name, int64_t Size, int64_t HardLimit, int32_t BlockSize);
	void CommandHelp(const std::vector<std::string> &args);

	// Implementations
	void List(int64_t DirID, const std::string &rListRoot, const bool *opts, bool FirstLevel);
	class CompareParams
	{
	public:
		CompareParams();
		~CompareParams();
		void DeleteExcludeLists();
		bool mQuickCompare;
		bool mIgnoreExcludes;
		int mDifferences;
		int mDifferencesExplainedByModTime;
		int mExcludedDirs;
		int mExcludedFiles;
		const ExcludeList *mpExcludeFiles;
		const ExcludeList *mpExcludeDirs;
		box_time_t mLatestFileUploadTime;
	};
	void CompareLocation(const std::string &rLocation, CompareParams &rParams);
	void Compare(const std::string &rStoreDir, const std::string &rLocalDir, CompareParams &rParams);
	void Compare(int64_t DirID, const std::string &rStoreDir, const std::string &rLocalDir, CompareParams &rParams);

	// Utility functions
	int64_t FindDirectoryObjectID(const std::string &rDirName, bool AllowOldVersion = false,
		bool AllowDeletedDirs = false, std::vector<std::pair<std::string, int64_t> > *pStack = 0);
	int64_t GetCurrentDirectoryID();
	std::string GetCurrentDirectoryName();
	void SetReturnCode(int code) {mReturnCode = code;}

private:
	BackupProtocolClient &mrConnection;
	const Configuration &mrConfiguration;
	bool mQuitNow;
	std::vector<std::pair<std::string, int64_t> > mDirStack;
	bool mRunningAsRoot;
	bool mWarnedAboutOwnerAttributes;
	int mReturnCode;
};

#endif // BACKUPQUERIES__H

