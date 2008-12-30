// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemonInterface.h
//		Purpose: Interfaces for managing a BackupDaemon
//		Created: 2008/12/30
//
// --------------------------------------------------------------------------

#ifndef BACKUPDAEMONINTERFACE__H
#define BACKUPDAEMONINTERFACE__H

#include <string>
// #include <map>

// #include "BackupClientFileAttributes.h"
// #include "BackupStoreDirectory.h"
#include "BoxTime.h"
// #include "MD5Digest.h"
// #include "ReadLoggingStream.h"
// #include "RunStatusProvider.h"

class Archive;
class BackupClientContext;
class BackupDaemon;

// --------------------------------------------------------------------------
//
// Class
//		Name:    SysadminNotifier
//		Purpose: Provides a NotifySysadmin() method to send mail to the sysadmin
//		Created: 2005/11/15
//
// --------------------------------------------------------------------------
class SysadminNotifier
{
	public:
	virtual ~SysadminNotifier() { }

	typedef enum
	{
		StoreFull = 0,
		ReadError,
		BackupError,
		BackupStart,
		BackupFinish,
		BackupOK,
		MAX
		// When adding notifications, remember to add
		// strings to NotifySysadmin()
	}
	EventCode;

	virtual void NotifySysadmin(EventCode Event) = 0;
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    ProgressNotifier
//		Purpose: Provides methods for the backup library to inform the user
//		         interface about its progress with the backup
//		Created: 2005/11/20
//
// --------------------------------------------------------------------------

class BackupClientContext;
class BackupClientDirectoryRecord;
	
class ProgressNotifier
{
	public:
	virtual ~ProgressNotifier() { }
	virtual void NotifyIDMapsSetup(BackupClientContext& rContext) = 0;
	virtual void NotifyScanDirectory(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyDirStatFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg) = 0;
	virtual void NotifyFileStatFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg) = 0;
	virtual void NotifyDirListFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg) = 0;
	virtual void NotifyMountPointSkipped(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyFileExcluded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyDirExcluded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyUnsupportedFileType(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyFileReadFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg) = 0;
	virtual void NotifyFileModifiedInFuture(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyFileSkippedServerFull(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyFileUploadException(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const BoxException& rException) = 0;
	virtual void NotifyFileUploadServerError(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		int type, int subtype) = 0;
	virtual void NotifyFileUploading(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyFileUploadingPatch(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) = 0;
	virtual void NotifyFileUploaded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		int64_t FileSize) = 0;
	virtual void NotifyFileSynchronised(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		int64_t FileSize) = 0;
	virtual void NotifyDirectoryDeleted(
		int64_t ObjectID,
		const std::string& rRemotePath) = 0;
	virtual void NotifyFileDeleted(
		int64_t ObjectID,
		const std::string& rRemotePath) = 0;
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset,
		int64_t length, box_time_t elapsed, box_time_t finish) = 0;
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset,
		int64_t length) = 0;
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset) = 0;
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    LocationResolver
//		Purpose: Interface for classes that can resolve locations to paths,
//		         like BackupDaemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class LocationResolver
{
public:
	virtual ~LocationResolver() { }
	virtual bool FindLocationPathName(const std::string &rLocationName, 
		std::string &rPathOut) const = 0;
};

#endif // BACKUPDAEMONINTERFACE__H
