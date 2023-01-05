// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemon.h
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef BACKUPDAEMON__H
#define BACKUPDAEMON__H

#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <iostream>

#include "BackupClientContext.h"
#include "BackupClientDirectoryRecord.h"
#include "BoxTime.h"
#include "Utils.h"
#include "Daemon.h"
#include "Logging.h"
#include "Socket.h"
#include "SocketListen.h"
#include "SocketStream.h"
#include "TLSContext.h"

#include "autogen_BackupProtocol.h"
#include "autogen_BackupStoreException.h"

#ifdef WIN32
	#include "WinNamedPipeListener.h"
	#include "WinNamedPipeStream.h"
#endif

#ifdef ENABLE_VSS
#	include <comdef.h>
#	include <Vss.h>
#	include <VsWriter.h>
#	include <VsBackup.h>
#endif

#define COMMAND_SOCKET_POLL_INTERVAL 1000

class BackupClientDirectoryRecord;
class BackupClientContext;
class Configuration;
class BackupClientInodeToIDMap;
class ExcludeList;
class IOStreamGetLine;
class Archive;
// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupDaemon
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class BackupDaemon : public Daemon, public ProgressNotifier, public LocationResolver,
public RunStatusProvider, public SysadminNotifier, public BackgroundTask
{
public:
	BackupDaemon();
	~BackupDaemon();

private:
	// methods below do partial (specialized) serialization of 
	// client state only
	bool SerializeStoreObjectInfo(box_time_t theLastSyncTime,
		box_time_t theNextSyncTime) const;
	bool DeserializeStoreObjectInfo(box_time_t & theLastSyncTime,
		box_time_t & theNextSyncTime);
	bool DeleteStoreObjectInfo() const;
	BackupDaemon(const BackupDaemon &);

    class SyncStats  {

		

		public:
			SyncStats(): state(0), startTime(0), endTime(0), TotalSizeUploaded(0), NumFilesUploaded(0), NumDirsCreated(0) {}

			int state;
			box_time_t startTime;
			box_time_t endTime;
			uint64_t TotalSizeUploaded;
			uint64_t NumFilesUploaded;
			uint64_t NumDirsCreated;

			std::string ToJson() const {
				std::stringstream ss;
				if( startTime!=0 ) {
					ss << "{ \"operation\": \"backup\""
						<< ", \"status\": "<< this->state 
						<< ", \"start\": " << this->startTime 
						<< ", \"end\": " << this->endTime 
						<< ", \"size\": " << this->TotalSizeUploaded 
						<< ", \"files\": " << this->NumFilesUploaded 
						<< ", \"dirs_created\": " << this->NumDirsCreated 
						<< "}";
				}
				return ss.str();
			}

			void reset() {
				this->state = 0;
				this->startTime = 0;
				this->endTime = 0;
				this->TotalSizeUploaded = 0;
				this->NumFilesUploaded = 0;
				this->NumDirsCreated = 0;
			}
			

			bool isRunning() {
				return this->startTime!=0 && this->endTime==0;
			}

			void setStart(SysadminNotifier::EventCode state) {
				this->reset();
        		this->startTime=GetCurrentBoxTime();
        		this->state=(int)state;
			}


			void setEnd(SysadminNotifier::EventCode state) {
				this->endTime=GetCurrentBoxTime();
				this->state=(int)state;
			}


    };

    void setStartSync(SysadminNotifier::EventCode state) {
        mCurrentOperationStats.setStart(state);
    }

    void setEndSync(SysadminNotifier::EventCode state) {
        mCurrentOperationStats.setEnd(state);

		if(GetConfiguration().KeyExists("OperationHistoryFile")) {
	
			std::string statsFile = GetConfiguration().GetKeyValue("OperationHistoryFile");

			// create the directory tree for the stats file
			std::string statsDir = statsFile.substr(0, statsFile.find_last_of("/"));
			CreatePath(statsDir);

			std::ofstream statsStream(statsFile.c_str(), std::ios_base::out | std::ios_base::app);

			if (statsStream.is_open())
			{
				statsStream << mCurrentOperationStats.ToJson()<< std::endl;
				
			}
			statsStream.close();

	
		}

		// reset
		mCurrentOperationStats.reset();

    }


public:
	#ifdef WIN32
		// add command-line options to handle Windows services
		std::string GetOptionString();
		int ProcessOption(signed int option);
		int Main(const std::string &rConfigFileName);

		// This shouldn't be here, but apparently gcc on
		// Windows has no idea about inherited methods...
		virtual int Main(const char *DefaultConfigFile, int argc,
			const char *argv[])
		{
			return Daemon::Main(DefaultConfigFile, argc, argv);
		}
	#endif

	void Run();
	virtual const char *DaemonName() const;
	virtual std::string DaemonBanner() const;
	virtual void Usage();
	const ConfigurationVerify *GetConfigVerify() const;

	bool FindLocationPathName(const std::string &rLocationName, std::string &rPathOut) const;

	enum
	{
		// Add stuff to this, make sure the textual equivalents in SetState() are changed too.
		State_Initialising = -1,
		State_Idle = 0,
		State_Connected = 1,
		State_Error = 2,
		State_StorageLimitExceeded = 3
	};

	int GetState() {return mState;}
	static std::string GetStateName(int state)
	{
		std::string stateName;

		#define STATE(x) case BackupDaemon::State_ ## x: stateName = #x; break;
		switch (state)
		{
		STATE(Initialising);
		STATE(Idle);
		STATE(Connected);
		STATE(Error);
		STATE(StorageLimitExceeded);
		default:
			stateName = "unknown";
		}
		#undef STATE

		return stateName;
	}

	// Allow other classes to call this too
	void NotifySysadmin(SysadminNotifier::EventCode Event);



private:
	void Run2();

public:
	void InitCrypto();
	std::auto_ptr<BackupClientContext> RunSyncNowWithExceptionHandling();
	std::auto_ptr<BackupClientContext> RunSyncNow();
	void ResetCachedState();
	void OnBackupStart();
    void OnBackupFinish(SysadminNotifier::EventCode state);
	// TouchFileInWorkingDir is only here for use by Boxi.
	// This does NOT constitute an API!
	void TouchFileInWorkingDir(const char *Filename);

protected:
	virtual std::auto_ptr<BackupClientContext> GetNewContext
	(
		LocationResolver &rResolver,
		TLSContext &rTLSContext,
		const std::string &rHostname,
		int32_t Port,
		uint32_t AccountNumber,
		bool ExtendedLogging,
		bool ExtendedLogToFile,
		std::string ExtendedLogFile,
		ProgressNotifier &rProgressNotifier,
		bool TcpNiceMode
	);

private:
	void DeleteAllLocations();
	void SetupLocations(BackupClientContext &rClientContext, const Configuration &rLocationsConf);

	void DeleteIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector);
	void DeleteAllIDMaps()
	{
		DeleteIDMapVector(mCurrentIDMaps);
		DeleteIDMapVector(mNewIDMaps);
	}
	void FillIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector, bool NewMaps);
	
	void SetupIDMapsForSync();
	void CommitIDMapsAfterSync();
	void DeleteCorruptBerkelyDbFiles();
	
	void MakeMapBaseName(unsigned int MountNumber, std::string &rNameOut) const;

	void SetState(int State);
	
	void WaitOnCommandSocket(box_time_t RequiredDelay, bool &DoSyncFlagOut, bool &SyncIsForcedOut);
	void CloseCommandConnection();
	void SendSyncStartOrFinish(bool SendStart);
	
	void DeleteUnusedRootDirEntries(BackupClientContext &rContext);

	// For warning user about potential security hole
	virtual void SetupInInitialProcess();

	int UseScriptToSeeIfSyncAllowed();

public:
	int ParseSyncAllowScriptOutput(const std::string& script,
		const std::string& output);
	typedef std::list<Location *> Locations;
	Locations GetLocations() { return mLocations; }
	SyncStats GetCurrentOperationStats() { return mCurrentOperationStats; }

private:
	int mState;		// what the daemon is currently doing

	Locations mLocations;
	
	std::vector<std::string> mIDMapMounts;
	std::vector<BackupClientInodeToIDMap *> mCurrentIDMaps;
	std::vector<BackupClientInodeToIDMap *> mNewIDMaps;
	
	int mDeleteRedundantLocationsAfter;

	// For the command socket
	class CommandSocketInfo
	{
	public:
		CommandSocketInfo();
		~CommandSocketInfo();
	private:
		CommandSocketInfo(const CommandSocketInfo &);	// no copying
		CommandSocketInfo &operator=(const CommandSocketInfo &);
	public:
#ifdef WIN32
		WinNamedPipeListener<1 /* listen backlog */> mListeningSocket;
		std::auto_ptr<WinNamedPipeStream> mpConnectedSocket;
#else
		SocketListen<SocketStream, 1 /* listen backlog */> mListeningSocket;
		std::auto_ptr<SocketStream> mpConnectedSocket;
#endif
		std::auto_ptr<IOStreamGetLine> mapGetLine;
	};

	// Using a socket?
	std::auto_ptr<CommandSocketInfo> mapCommandSocketInfo;

	// Stop notifications being repeated.
	SysadminNotifier::EventCode mLastNotifiedEvent;

	// Unused entries in the root directory wait a while before being deleted
	box_time_t mDeleteUnusedRootDirEntriesAfter;	// time to delete them
	std::vector<std::pair<int64_t,std::string> > mUnusedRootDirEntries;

	int64_t mClientStoreMarker;
	bool mStorageLimitExceeded;
	bool mReadErrorsOnFilesystemObjects;
	box_time_t mLastSyncTime, mNextSyncTime;
	box_time_t mCurrentSyncStartTime, mUpdateStoreInterval,
		  mBackupErrorDelay;
	TLSContext mTlsContext;
	bool mDeleteStoreObjectInfoFile;
	bool mDoSyncForcedByPreviousSyncError;
	SyncStats mCurrentOperationStats;
	int mMaxBandwidthFromSyncAllowScript;

public:
	int GetMaxBandwidthFromSyncAllowScript() { return mMaxBandwidthFromSyncAllowScript; }
	bool StopRun() { return this->Daemon::StopRun(); }
	bool StorageLimitExceeded() { return mStorageLimitExceeded; }
 
private:
	bool mLogAllFileAccess;
    uint32_t mStatsHistoryLength;

public:
	ProgressNotifier*  GetProgressNotifier()  { return mpProgressNotifier; }
	LocationResolver*  GetLocationResolver()  { return mpLocationResolver; }
	RunStatusProvider* GetRunStatusProvider() { return mpRunStatusProvider; }
	SysadminNotifier*  GetSysadminNotifier()  { return mpSysadminNotifier; }
	void SetProgressNotifier (ProgressNotifier*  p) { mpProgressNotifier = p; }
	void SetLocationResolver (LocationResolver*  p) { mpLocationResolver = p; }
	void SetRunStatusProvider(RunStatusProvider* p) { mpRunStatusProvider = p; }
	void SetSysadminNotifier (SysadminNotifier*  p) { mpSysadminNotifier = p; }
	virtual bool RunBackgroundTask(State state, uint64_t progress,
		uint64_t maximum);

private:
	ProgressNotifier* mpProgressNotifier;
	LocationResolver* mpLocationResolver;
	RunStatusProvider* mpRunStatusProvider;
	SysadminNotifier* mpSysadminNotifier;
	std::auto_ptr<Timer> mapCommandSocketPollTimer;
	std::auto_ptr<BackupClientContext> mapClientContext;

	/* ProgressNotifier implementation */
public:
	virtual void NotifyIDMapsSetup(BackupClientContext& rContext) { }

	virtual void NotifyScanDirectory(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Scanning directory: " << rLocalPath);
		}

		if (!RunBackgroundTask(BackgroundTask::Scanning_Dirs, 0, 0))
		{
			THROW_EXCEPTION(BackupStoreException,
				CancelledByBackgroundTask);
		}
	}
	virtual void NotifyDirStatFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg)
	{
		BOX_WARNING("Failed to access directory: " << rLocalPath
			<< ": " << rErrorMsg);
	}
	virtual void NotifyFileStatFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg)
	{
		BOX_WARNING("Failed to access file: " << rLocalPath
			<< ": " << rErrorMsg);
	}
	virtual void NotifyDirListFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg)
	{
		BOX_WARNING("Failed to list directory: " << rLocalPath
			<< ": " << rErrorMsg);
	}
	virtual void NotifyMountPointSkipped(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		#ifdef WIN32
			BOX_WARNING("Ignored directory: " << rLocalPath <<
				": is an NTFS junction/reparse point; create "
				"a new location if you want to back it up");
		#else
			BOX_WARNING("Ignored directory: " << rLocalPath <<
				": is a mount point; create a new location "
				"if you want to back it up");
		#endif
	}
	virtual void NotifyFileExcluded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Skipping excluded file: " << rLocalPath);
		}
	}
	virtual void NotifyDirExcluded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Skipping excluded directory: " << rLocalPath);
		}
	}
	virtual void NotifyUnsupportedFileType(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		BOX_WARNING("Ignoring file of unknown type: " << rLocalPath);
	}
	virtual void NotifyFileReadFailed(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const std::string& rErrorMsg)
	{
		BOX_WARNING("Error reading file: " << rLocalPath
			<< ": " << rErrorMsg);
	}
	virtual void NotifyFileModifiedInFuture(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		BOX_WARNING("Some files have modification times excessively "
			"in the future. Check clock synchronisation. "
			"Example file (only one shown): " << rLocalPath);
	}
	virtual void NotifyFileSkippedServerFull(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath) 
	{
		BOX_WARNING("Skipped file: server is full: " << rLocalPath);
	}
	virtual void NotifyFileUploadException(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		const BoxException& rException)
	{
		if (rException.GetType() == CommonException::ExceptionType &&
			rException.GetSubType() == CommonException::AccessDenied)
		{
			BOX_ERROR("Failed to upload file: " << rLocalPath
				<< ": Access denied");
		}
		else
		{
			BOX_ERROR("Failed to upload file: " << rLocalPath
				<< ": caught exception: " << rException.what()
				<< " (" << rException.GetType()
				<< "/"  << rException.GetSubType() << ")");
		}
	}
	virtual void NotifyFileUploadServerError(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		int type, int subtype)
	{
		BOX_ERROR("Failed to upload file: " << rLocalPath <<
			": server error: " <<
			BackupProtocolError::GetMessage(subtype));
	}
	virtual void NotifyFileUploading(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploading complete file: " << rLocalPath);
		}
	}
	virtual void NotifyFileUploadingPatch(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		int64_t EstimatedBytesToUpload)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploading patch to file: " << rLocalPath <<
				", estimated upload size = " <<
				EstimatedBytesToUpload);
		}
	}
	virtual void NotifyFileUploadingAttributes(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploading new file attributes: " <<
				rLocalPath);
		}
	}
	virtual void NotifyFileUploaded(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		int64_t FileSize, int64_t UploadedSize, int64_t ObjectID)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Uploaded file: " << rLocalPath <<
				" (ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
				"): total size = " << FileSize << ", "
				"uploaded size = " << UploadedSize);
		}
        mCurrentOperationStats.TotalSizeUploaded+=FileSize;
        mCurrentOperationStats.NumFilesUploaded++;
	}
	virtual void NotifyFileSynchronised(
		const BackupClientDirectoryRecord* pDirRecord,
		const std::string& rLocalPath,
		int64_t FileSize)
	{
		if (mLogAllFileAccess)
		{
			BOX_INFO("Synchronised file: " << rLocalPath);
		}
	}
	virtual void NotifyDirectoryCreated(
		int64_t ObjectID,
		const std::string& rLocalPath,
		const std::string& rRemotePath)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Created directory: " << rRemotePath <<
				" (ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
				")");
		}
        mCurrentOperationStats.NumDirsCreated++;
	}
	virtual void NotifyDirectoryDeleted(
		int64_t ObjectID,
		const std::string& rRemotePath)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Deleted directory: " << rRemotePath <<
				" (ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
				")");
		}
	}
	virtual void NotifyFileDeleted(
		int64_t ObjectID,
		const std::string& rRemotePath)
	{
		if (mLogAllFileAccess)
		{
			BOX_NOTICE("Deleted file: " << rRemotePath <<
				" (ID " << BOX_FORMAT_OBJECTID(ObjectID) <<
				")");
		}
	}
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset,
		int64_t length, box_time_t elapsed, box_time_t finish)
	{
		BOX_TRACE("Read " << readSize << " bytes at " << offset <<
			", " << (length - offset) << " remain, eta " <<
			BoxTimeToSeconds(finish - elapsed) << "s");
	}
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset,
		int64_t length)
	{
		BOX_TRACE("Read " << readSize << " bytes at " << offset <<
			", " << (length - offset) << " remain");
	}
	virtual void NotifyReadProgress(int64_t readSize, int64_t offset)
	{
		BOX_TRACE("Read " << readSize << " bytes at " << offset <<
			", unknown bytes remaining");
	}

#ifdef WIN32
	private:
	bool mInstallService, mRemoveService, mRunAsService;
	std::string mServiceName;
#endif

#ifdef ENABLE_VSS
	IVssBackupComponents* mpVssBackupComponents;
	void CreateVssBackupComponents();
	bool WaitForAsync(IVssAsync *pAsync, const std::string& description);
	typedef HRESULT (__stdcall IVssBackupComponents::*AsyncMethod)(IVssAsync**);
	bool CallAndWaitForAsync(AsyncMethod method,
		const std::string& description);
	void CleanupVssBackupComponents();
#endif
};

#endif // BACKUPDAEMON__H
