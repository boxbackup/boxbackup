// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientDirectoryRecord.h
//		Purpose: Implementation of record about directory for backup client
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTDIRECTORYRECORD__H
#define BACKUPCLIENTDIRECTORYRECORD__H

#include <string>
#include <map>
#include <memory>

#include "BackupClientFileAttributes.h"
#include "BackupDaemonInterface.h"
#include "BackupStoreDirectory.h"
#include "BoxTime.h"
#include "MD5Digest.h"
#include "ReadLoggingStream.h"
#include "RunStatusProvider.h"

#ifdef ENABLE_VSS
#	include <comdef.h>
#	include <Vss.h>
#	include <VsWriter.h>
#	include <VsBackup.h>
#endif

class Archive;
class BackupClientContext;
class BackupDaemon;
class ExcludeList;
class Location;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupClientDirectoryRecord
//		Purpose: Implementation of record about directory for backup client
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class BackupClientDirectoryRecord
{
public:
	BackupClientDirectoryRecord(int64_t ObjectID, const std::string &rSubDirName);
	~BackupClientDirectoryRecord();

	void Deserialize(Archive & rArchive);
	void Serialize(Archive & rArchive) const;
private:
	BackupClientDirectoryRecord(const BackupClientDirectoryRecord &);
public:

	enum
	{
		UnknownDirectoryID = 0
	};

	// --------------------------------------------------------------------------
	//
	// Class
	//		Name:    BackupClientDirectoryRecord::SyncParams
	//		Purpose: Holds parameters etc for directory syncing. Not passed as
	//				 const, some parameters may be modified during sync.
	//		Created: 8/3/04
	//
	// --------------------------------------------------------------------------
	class SyncParams : public ReadLoggingStream::Logger
	{
	public:
		SyncParams(
			RunStatusProvider &rRunStatusProvider, 
			SysadminNotifier &rSysadminNotifier,
			ProgressNotifier &rProgressNotifier,
			BackupClientContext &rContext);
		~SyncParams();
	private:
		// No copying
		SyncParams(const SyncParams&);
		SyncParams &operator=(const SyncParams&);
		
	public:
		// Data members are public, as accessors are not justified here
		box_time_t mSyncPeriodStart;
		box_time_t mSyncPeriodEnd;
		box_time_t mMaxUploadWait;
		box_time_t mMaxFileTimeInFuture;
		int32_t mFileTrackingSizeThreshold;
		int32_t mDiffingUploadSizeThreshold;
		RunStatusProvider &mrRunStatusProvider;
		SysadminNotifier &mrSysadminNotifier;
		ProgressNotifier &mrProgressNotifier;
		BackupClientContext &mrContext;
		bool mReadErrorsOnFilesystemObjects;
		int64_t mMaxUploadRate;
		
		// Member variables modified by syncing process
		box_time_t mUploadAfterThisTimeInTheFuture;
		bool mHaveLoggedWarningAboutFutureFileTimes;
	
		bool StopRun() { return mrRunStatusProvider.StopRun(); }
		void NotifySysadmin(SysadminNotifier::EventCode Event) 
		{ 
			mrSysadminNotifier.NotifySysadmin(Event); 
		}
		ProgressNotifier& GetProgressNotifier() const 
		{ 
			return mrProgressNotifier;
		}
		
		/* ReadLoggingStream::Logger implementation */
		virtual void Log(int64_t readSize, int64_t offset,
			int64_t length, box_time_t elapsed, box_time_t finish)
		{
			mrProgressNotifier.NotifyReadProgress(readSize, offset,
				length, elapsed, finish);
		}
		virtual void Log(int64_t readSize, int64_t offset,
			int64_t length)
		{
			mrProgressNotifier.NotifyReadProgress(readSize, offset,
				length);
		}
		virtual void Log(int64_t readSize, int64_t offset)
		{
			mrProgressNotifier.NotifyReadProgress(readSize, offset);
		}
	};

	void SyncDirectory(SyncParams &rParams,
		int64_t ContainingDirectoryID,
		const std::string &rLocalPath,
		const std::string &rRemotePath,
		const Location& rBackupLocation,
		bool ThisDirHasJustBeenCreated = false);

	std::string ConvertVssPathToRealPath(const std::string &rVssPath,
		const Location& rBackupLocation);

private:
	void DeleteSubDirectories();
	BackupStoreDirectory *FetchDirectoryListing(SyncParams &rParams);
	void UpdateAttributes(SyncParams &rParams,
		BackupStoreDirectory *pDirOnStore,
		const std::string &rLocalPath);
	bool UpdateItems(SyncParams &rParams, const std::string &rLocalPath,
		const std::string &rRemotePath,
		const Location& rBackupLocation,
		BackupStoreDirectory *pDirOnStore,
		std::vector<BackupStoreDirectory::Entry *> &rEntriesLeftOver,
		std::vector<std::string> &rFiles,
		const std::vector<std::string> &rDirs);
	int64_t UploadFile(SyncParams &rParams,
		const std::string &rFilename,
		const BackupStoreFilename &rStoreFilename,
		int64_t FileSize, box_time_t ModificationTime,
		box_time_t AttributesHash, bool NoPreviousVersionOnServer);
	void SetErrorWhenReadingFilesystemObject(SyncParams &rParams,
		const std::string& rFilename);
	void RemoveDirectoryInPlaceOfFile(SyncParams &rParams,
		BackupStoreDirectory* pDirOnStore,
		BackupStoreDirectory::Entry* pEntry,
		const std::string &rFilename);

private:
	int64_t 	mObjectID;
	std::string 	mSubDirName;
	bool 		mInitialSyncDone;
	bool 		mSyncDone;
	bool		mSuppressMultipleLinksWarning;

	// Checksum of directory contents and attributes, used to detect changes
	uint8_t mStateChecksum[MD5Digest::DigestLength];

	std::map<std::string, box_time_t> *mpPendingEntries;
	std::map<std::string, BackupClientDirectoryRecord *> mSubDirectories;
	// mpPendingEntries is a pointer rather than simple a member
	// variable, because most of the time it'll be empty. This would
	// waste a lot of memory because of STL allocation policies.
};

class Location
{
public:
	Location();
	~Location();

	void Deserialize(Archive & rArchive);
	void Serialize(Archive & rArchive) const;
private:
	Location(const Location &);	// copy not allowed
	Location &operator=(const Location &);
public:
	std::string mName;
	std::string mPath;
	std::auto_ptr<BackupClientDirectoryRecord> mpDirectoryRecord;
	int mIDMapIndex;
	ExcludeList *mpExcludeFiles;
	ExcludeList *mpExcludeDirs;

#ifdef ENABLE_VSS
	bool mIsSnapshotCreated;
	VSS_ID mSnapshotVolumeId;
	std::string mSnapshotPath;
#endif
};

#endif // BACKUPCLIENTDIRECTORYRECORD__H


