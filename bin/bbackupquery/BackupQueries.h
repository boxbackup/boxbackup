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

	void DoCommand(const char *Command, bool isFromCommandLine);

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
	void CommandGet(std::vector<std::string> args, const bool *opts);
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

