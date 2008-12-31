// --------------------------------------------------------------------------
//
// File
//		Name:    BoxBackupCompareParams.h
//		Purpose: Parameters and notifiers for a compare operation
//		Created: 2008/12/30
//
// --------------------------------------------------------------------------

#ifndef BOXBACKUPCOMPAREPARAMS__H
#define BOXBACKUPCOMPAREPARAMS__H

#include <string>

#include "BoxTime.h"
#include "ExcludeList.h"
#include "BackupClientMakeExcludeList.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BoxBackupCompareParams
//		Purpose: Parameters and notifiers for a compare operation
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
class BoxBackupCompareParams
{
private:
	std::auto_ptr<const ExcludeList> mapExcludeFiles, mapExcludeDirs;
	bool mQuickCompare;
	bool mIgnoreExcludes;
	bool mIgnoreAttributes;
	box_time_t mLatestFileUploadTime;
	
public:
	BoxBackupCompareParams(bool QuickCompare, bool IgnoreExcludes,
		bool IgnoreAttributes, box_time_t LatestFileUploadTime)
	: mQuickCompare(QuickCompare),
	  mIgnoreExcludes(IgnoreExcludes),
	  mIgnoreAttributes(IgnoreAttributes),
	  mLatestFileUploadTime(LatestFileUploadTime)
	{ }
	
	virtual ~BoxBackupCompareParams() { }
	
	bool QuickCompare() { return mQuickCompare; }
	bool IgnoreExcludes() { return mIgnoreExcludes; }
	bool IgnoreAttributes() { return mIgnoreAttributes; }
	box_time_t LatestFileUploadTime() { return mLatestFileUploadTime; }
		
	void LoadExcludeLists(const Configuration& rLoc)
	{
		mapExcludeFiles.reset(BackupClientMakeExcludeList_Files(rLoc));
		mapExcludeDirs.reset(BackupClientMakeExcludeList_Dirs(rLoc));
	}
	bool IsExcludedFile(const std::string& rLocalPath)
	{
		if (!mapExcludeFiles.get()) return false;
		return mapExcludeFiles->IsExcluded(rLocalPath);
	}
	bool IsExcludedDir(const std::string& rLocalPath)
	{
		if (!mapExcludeDirs.get()) return false;
		return mapExcludeDirs->IsExcluded(rLocalPath);
	}

	virtual void NotifyLocalDirMissing(const std::string& rLocalPath,
		const std::string& rRemotePath) = 0;
	virtual void NotifyLocalDirAccessFailed(const std::string& rLocalPath,
		const std::string& rRemotePath) = 0;
	virtual void NotifyStoreDirMissingAttributes(const std::string& rLocalPath,
		const std::string& rRemotePath) = 0;
	virtual void NotifyRemoteFileMissing(const std::string& rLocalPath,
		const std::string& rRemotePath,
		bool modifiedAfterLastSync) = 0;
	virtual void NotifyLocalFileMissing(const std::string& rLocalPath,
		const std::string& rRemotePath) = 0;
	virtual void NotifyExcludedFileNotDeleted(const std::string& rLocalPath,
		const std::string& rRemotePath) = 0;
	virtual void NotifyDownloadFailed(const std::string& rLocalPath,
		const std::string& rRemotePath, int64_t NumBytes,
		BoxException& rException) = 0;
	virtual void NotifyDownloadFailed(const std::string& rLocalPath,
		const std::string& rRemotePath, int64_t NumBytes,
		std::exception& rException) = 0;
	virtual void NotifyDownloadFailed(const std::string& rLocalPath,
		const std::string& rRemotePath, int64_t NumBytes) = 0;
	virtual void NotifyExcludedFile(const std::string& rLocalPath,
		const std::string& rRemotePath) = 0;
	virtual void NotifyExcludedDir(const std::string& rLocalPath,
		const std::string& rRemotePath) = 0;
	virtual void NotifyDirCompared(const std::string& rLocalPath,
		const std::string& rRemotePath,	bool HasDifferentAttributes,
		bool modifiedAfterLastSync) = 0;
	virtual void NotifyFileCompared(const std::string& rLocalPath,
		const std::string& rRemotePath, int64_t NumBytes,
		bool HasDifferentAttributes, bool HasDifferentContents,
		bool modifiedAfterLastSync, bool newAttributesApplied) = 0;
};

#endif // BOXBACKUPCOMPAREPARAMS__H
