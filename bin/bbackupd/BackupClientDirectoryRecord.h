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

#include "BoxTime.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreDirectory.h"
#include "MD5Digest.h"

class BackupClientContext;
class BackupDaemon;
class CommandSocketManager;


// --------------------------------------------------------------------------
//
// Class
//		Name:    RunStatusProvider
//		Purpose: Provides a StopRun() method which returns true if the current
//		         backup should be halted.
//		Created: 2005/11/15
//
// --------------------------------------------------------------------------
class RunStatusProvider
{
	public:
	virtual ~RunStatusProvider() { }
	virtual bool StopRun() = 0;
};

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
	virtual void NotifySysadmin(int Event) = 0;
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
class BackupClientDirectoryRecord;
	
class ProgressNotifier
{
	public:
	virtual ~ProgressNotifier() { }
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
};

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
	class SyncParams
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
		RunStatusProvider &mrRunStatusProvider;
		SysadminNotifier &mrSysadminNotifier;
		ProgressNotifier &mrProgressNotifier;
		
	public:
		// Data members are public, as accessors are not justified here
		box_time_t mSyncPeriodStart;
		box_time_t mSyncPeriodEnd;
		box_time_t mMaxUploadWait;
		box_time_t mMaxFileTimeInFuture;
		int32_t mFileTrackingSizeThreshold;
		int32_t mDiffingUploadSizeThreshold;
		BackupClientContext &mrContext;
		bool mReadErrorsOnFilesystemObjects;
		CommandSocketManager* mpCommandSocket;
	
		// Member variables modified by syncing process
		box_time_t mUploadAfterThisTimeInTheFuture;
		bool mHaveLoggedWarningAboutFutureFileTimes;
	
		bool StopRun() { return mrRunStatusProvider.StopRun(); }
		void NotifySysadmin(int Event) 
		{ 
			mrSysadminNotifier.NotifySysadmin(Event); 
		}
		ProgressNotifier& GetProgressNotifier() const 
		{ 
			return mrProgressNotifier;
		}
	};

	void SyncDirectory(SyncParams &rParams, int64_t ContainingDirectoryID, const std::string &rLocalPath,
		bool ThisDirHasJustBeenCreated = false);

private:
	void DeleteSubDirectories();
	BackupStoreDirectory *FetchDirectoryListing(SyncParams &rParams);
	void UpdateAttributes(SyncParams &rParams, BackupStoreDirectory *pDirOnStore, const std::string &rLocalPath);
	bool UpdateItems(SyncParams &rParams, const std::string &rLocalPath, BackupStoreDirectory *pDirOnStore,
		std::vector<BackupStoreDirectory::Entry *> &rEntriesLeftOver,
		std::vector<std::string> &rFiles, const std::vector<std::string> &rDirs);
	int64_t UploadFile(SyncParams &rParams, const std::string &rFilename, const BackupStoreFilename &rStoreFilename,
		int64_t FileSize, box_time_t ModificationTime, box_time_t AttributesHash, bool NoPreviousVersionOnServer);
	void SetErrorWhenReadingFilesystemObject(SyncParams &rParams, const char *Filename);
	void RemoveDirectoryInPlaceOfFile(SyncParams &rParams, BackupStoreDirectory *pDirOnStore, int64_t ObjectID, const std::string &rFilename);

private:
	int64_t 		mObjectID;
	std::string 	mSubDirName;
	bool 			mInitialSyncDone;
	bool 			mSyncDone;

	// Checksum of directory contents and attributes, used to detect changes
	uint8_t mStateChecksum[MD5Digest::DigestLength];

	std::map<std::string, box_time_t>						*mpPendingEntries;
	std::map<std::string, BackupClientDirectoryRecord *>	mSubDirectories;
	// mpPendingEntries is a pointer rather than simple a member
	// variables, because most of the time it'll be empty. This would waste a lot
	// of memory because of STL allocation policies.
};

#endif // BACKUPCLIENTDIRECTORYRECORD__H
