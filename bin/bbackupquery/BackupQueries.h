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
	BackupQueries(BackupProtocolClient &rConnection,
		const Configuration &rConfiguration,
		bool readWrite);
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
	void CommandDelete(const std::vector<std::string> &args,
		const bool *opts);
	void CommandUsage(const bool *opts);
	void CommandUsageDisplayEntry(const char *Name, int64_t Size,
		int64_t HardLimit, int32_t BlockSize, bool MachineReadable);
	void CommandHelp(const std::vector<std::string> &args);

	// Implementations
	void List(int64_t DirID, const std::string &rListRoot, const bool *opts,
		bool FirstLevel);
	
public:
	class CompareParams
	{
	public:
		CompareParams();
		~CompareParams();
		void DeleteExcludeLists();
		bool mQuickCompare;
		bool mQuietCompare;
		bool mIgnoreExcludes;
		bool mIgnoreAttributes;
		int mDifferences;
		int mDifferencesExplainedByModTime;
		int mUncheckedFiles;
		int mExcludedDirs;
		int mExcludedFiles;
		const ExcludeList *mpExcludeFiles;
		const ExcludeList *mpExcludeDirs;
		box_time_t mLatestFileUploadTime;
	};
	void CompareLocation(const std::string &rLocation,
		CompareParams &rParams);
	void Compare(const std::string &rStoreDir,
		const std::string &rLocalDir, CompareParams &rParams);
	void Compare(int64_t DirID, const std::string &rStoreDir,
		const std::string &rLocalDir, CompareParams &rParams);

public:

	class ReturnCode
	{
		public:
		enum {
			Command_OK = 0,
			Compare_Same = 1,
			Compare_Different,
			Compare_Error,
			Command_Error,
		} Type;
	};

private:

	// Utility functions
	int64_t FindDirectoryObjectID(const std::string &rDirName,
		bool AllowOldVersion = false, bool AllowDeletedDirs = false,
		std::vector<std::pair<std::string, int64_t> > *pStack = 0);
	int64_t FindFileID(const std::string& rNameOrIdString,
		const bool *opts, int64_t *pDirIdOut,
		std::string* pFileNameOut, int16_t flagsInclude,
		int16_t flagsExclude, int16_t* pFlagsOut);
	int64_t GetCurrentDirectoryID();
	std::string GetCurrentDirectoryName();
	void SetReturnCode(int code) {mReturnCode = code;}

private:
	bool mReadWrite;
	BackupProtocolClient &mrConnection;
	const Configuration &mrConfiguration;
	bool mQuitNow;
	std::vector<std::pair<std::string, int64_t> > mDirStack;
	bool mRunningAsRoot;
	bool mWarnedAboutOwnerAttributes;
	int mReturnCode;
};

#endif // BACKUPQUERIES__H

