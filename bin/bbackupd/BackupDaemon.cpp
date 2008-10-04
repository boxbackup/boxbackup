// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemon.cpp
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif
#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif
#ifdef HAVE_SYS_PARAM_H
	#include <sys/param.h>
#endif
#ifdef HAVE_SYS_WAIT_H
	#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
	#include <sys/mount.h>
#endif
#ifdef HAVE_MNTENT_H
	#include <mntent.h>
#endif 
#ifdef HAVE_SYS_MNTTAB_H
	#include <cstdio>
	#include <sys/mnttab.h>
#endif
#ifdef HAVE_PROCESS_H
	#include <process.h>
#endif

#include <iostream>

#include "Configuration.h"
#include "IOStream.h"
#include "MemBlockStream.h"
#include "CommonException.h"
#include "BoxPortsAndFiles.h"

#include "SSLLib.h"

#include "autogen_BackupProtocolClient.h"
#include "autogen_ClientException.h"
#include "autogen_ConversionException.h"
#include "Archive.h"
#include "BackupClientContext.h"
#include "BackupClientCryptoKeys.h"
#include "BackupClientDirectoryRecord.h"
#include "BackupClientFileAttributes.h"
#include "BackupClientInodeToIDMap.h"
#include "BackupClientMakeExcludeList.h"
#include "BackupDaemon.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFilenameClear.h"
#include "BannerText.h"
#include "Conversion.h"
#include "ExcludeList.h"
#include "FileStream.h"
#include "IOStreamGetLine.h"
#include "LocalProcessStream.h"
#include "Logging.h"
#include "Random.h"
#include "Timer.h"
#include "Utils.h"

#ifdef WIN32
	#include "Win32ServiceFunctions.h"
	#include "Win32BackupService.h"

	extern Win32BackupService* gpDaemonService;
#endif

#include "MemLeakFindOn.h"

static const time_t MAX_SLEEP_TIME = 1024;

// Make the actual sync period have a little bit of extra time, up to a 64th of the main sync period.
// This prevents repetative cycles of load on the server
#define		SYNC_PERIOD_RANDOM_EXTRA_TIME_SHIFT_BY	6

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::BackupDaemon()
//		Purpose: constructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupDaemon::BackupDaemon()
	: mState(BackupDaemon::State_Initialising),
	  mDeleteRedundantLocationsAfter(0),
	  mLastNotifiedEvent(SysadminNotifier::MAX),
	  mDeleteUnusedRootDirEntriesAfter(0),
	  mClientStoreMarker(BackupClientContext::ClientStoreMarker_NotKnown),
	  mStorageLimitExceeded(false),
	  mReadErrorsOnFilesystemObjects(false),
	  mLastSyncTime(0),
	  mNextSyncTime(0),
	  mCurrentSyncStartTime(0),
	  mUpdateStoreInterval(0),
	  mDeleteStoreObjectInfoFile(false),
	  mDoSyncForcedByPreviousSyncError(false),
	  mLogAllFileAccess(false),
	  mpProgressNotifier(this),
	  mpLocationResolver(this),
	  mpRunStatusProvider(this),
	  mpSysadminNotifier(this)
	#ifdef WIN32
	, mInstallService(false),
	  mRemoveService(false),
	  mRunAsService(false),
	  mServiceName("bbackupd")
	#endif
{
	// Only ever one instance of a daemon
	SSLLib::Initialise();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::~BackupDaemon()
//		Purpose: Destructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupDaemon::~BackupDaemon()
{
	DeleteAllLocations();
	DeleteAllIDMaps();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DaemonName()
//		Purpose: Get name of daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
const char *BackupDaemon::DaemonName() const
{
	return "bbackupd";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DaemonBanner()
//		Purpose: Daemon banner
//		Created: 1/1/04
//
// --------------------------------------------------------------------------
std::string BackupDaemon::DaemonBanner() const
{
	return BANNER_TEXT("Backup Client");
}

void BackupDaemon::Usage()
{
	this->Daemon::Usage();

#ifdef WIN32
	std::cout <<
	"  -s         Run as a Windows Service, for internal use only\n"
	"  -i         Install Windows Service (you may want to specify a config file)\n"
	"  -r         Remove Windows Service\n"
	"  -S <name>  Service name for -i and -r options\n";
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::GetConfigVerify()
//		Purpose: Get configuration specification
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
const ConfigurationVerify *BackupDaemon::GetConfigVerify() const
{
	// Defined elsewhere
	return &BackupDaemonConfigVerify;
}

#ifdef PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::SetupInInitialProcess()
//		Purpose: Platforms with non-checkable credentials on 
//			local sockets only.
//			Prints a warning if the command socket is used.
//		Created: 25/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::SetupInInitialProcess()
{
	// Print a warning on this platform if the CommandSocket is used.
	if(GetConfiguration().KeyExists("CommandSocket"))
	{
		BOX_WARNING(
			"==============================================================================\n"
			"SECURITY WARNING: This platform cannot check the credentials of connections to\n"
			"the command socket. This is a potential DoS security problem.\n"
			"Remove the CommandSocket directive from the bbackupd.conf file if bbackupctl\n"
			"is not used.\n"
			"==============================================================================\n"
			);
	}
}
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DeleteAllLocations()
//		Purpose: Deletes all records stored
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
void BackupDaemon::DeleteAllLocations()
{
	// Run through, and delete everything
	for(std::vector<Location *>::iterator i = mLocations.begin();
		i != mLocations.end(); ++i)
	{
		delete *i;
	}

	// Clear the contents of the map, so it is empty
	mLocations.clear();
	
	// And delete everything from the associated mount vector
	mIDMapMounts.clear();
}

#ifdef WIN32
std::string BackupDaemon::GetOptionString()
{
	std::string oldOpts = this->Daemon::GetOptionString();
	ASSERT(oldOpts.find("s") == std::string::npos);
	ASSERT(oldOpts.find("S") == std::string::npos);
	ASSERT(oldOpts.find("i") == std::string::npos);
	ASSERT(oldOpts.find("r") == std::string::npos);
	return oldOpts + "sS:ir";
}

int BackupDaemon::ProcessOption(signed int option)
{
	switch(option)
	{
		case 's':
		{
			mRunAsService = true;
			return 0;
		}

		case 'S':
		{
			mServiceName = optarg;
			Logging::SetProgramName(mServiceName);
			return 0;
		}

		case 'i':
		{
			mInstallService = true;
			return 0;
		}

		case 'r':
		{
			mRemoveService = true;
			return 0;
		}

		default:
		{
			return this->Daemon::ProcessOption(option);
		}
	}
}

int BackupDaemon::Main(const std::string &rConfigFileName)
{
	if (mInstallService)
	{
		return InstallService(rConfigFileName.c_str(), mServiceName);
	}

	if (mRemoveService)
	{
		return RemoveService(mServiceName);
	}

	int returnCode;

	if (mRunAsService)
	{
		// We will be called reentrantly by the Service Control
		// Manager, and we had better not call OurService again!
		mRunAsService = false;

		BOX_INFO("Box Backup service starting");
		returnCode = OurService(rConfigFileName.c_str());
		BOX_INFO("Box Backup service shut down");
	}
	else
	{
		returnCode = this->Daemon::Main(rConfigFileName);
	}
	
	return returnCode;
}
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::Run()
//		Purpose: Run function for daemon
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::Run()
{
	// initialise global timer mechanism
	Timers::Init();
	
	#ifndef WIN32
		// Ignore SIGPIPE so that if a command connection is broken,
		// the daemon doesn't terminate.
		::signal(SIGPIPE, SIG_IGN);
	#endif

	// Create a command socket?
	const Configuration &conf(GetConfiguration());
	if(conf.KeyExists("CommandSocket"))
	{
		// Yes, create a local UNIX socket
		mapCommandSocketInfo.reset(new CommandSocketInfo);
		const char *socketName =
			conf.GetKeyValue("CommandSocket").c_str();
		#ifdef WIN32
			mapCommandSocketInfo->mListeningSocket.Listen(
				socketName);
		#else
			::unlink(socketName);
			mapCommandSocketInfo->mListeningSocket.Listen(
				Socket::TypeUNIX, socketName);
		#endif
	}

	// Handle things nicely on exceptions
	try
	{
		Run2();
	}
	catch(...)
	{
		if(mapCommandSocketInfo.get())
		{
			try 
			{
				mapCommandSocketInfo.reset();
			}
			catch(std::exception &e)
			{
				BOX_WARNING("Internal error while "
					"closing command socket after "
					"another exception: " << e.what());
			}
			catch(...)
			{
				BOX_WARNING("Error closing command socket "
					"after exception, ignored.");
			}
		}

		Timers::Cleanup();
		
		throw;
	}

	// Clean up
	mapCommandSocketInfo.reset();
	Timers::Cleanup();
}

void BackupDaemon::InitCrypto()
{
	// Read in the certificates creating a TLS context
	const Configuration &conf(GetConfiguration());
	std::string certFile(conf.GetKeyValue("CertificateFile"));
	std::string keyFile(conf.GetKeyValue("PrivateKeyFile"));
	std::string caFile(conf.GetKeyValue("TrustedCAsFile"));
	mTlsContext.Initialise(false /* as client */, certFile.c_str(),
		keyFile.c_str(), caFile.c_str());
	
	// Set up the keys for various things
	BackupClientCryptoKeys_Setup(conf.GetKeyValue("KeysFile").c_str());
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::Run2()
//		Purpose: Run function for daemon (second stage)
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
void BackupDaemon::Run2()
{
	InitCrypto();

	const Configuration &conf(GetConfiguration());

	// How often to connect to the store (approximate)
	mUpdateStoreInterval = SecondsToBoxTime(
		conf.GetKeyValueInt("UpdateStoreInterval"));

	// But are we connecting automatically?
	bool automaticBackup = conf.GetKeyValueBool("AutomaticBackup");
	
	// When the next sync should take place -- which is ASAP
	mNextSyncTime = 0;

	// When the last sync started (only updated if the store was not full when the sync ended)
	mLastSyncTime = 0;

 	// --------------------------------------------------------------------------------------------
 
 	mDeleteStoreObjectInfoFile = DeserializeStoreObjectInfo(
		mLastSyncTime, mNextSyncTime);
 
	// --------------------------------------------------------------------------------------------
	

	// Set state
	SetState(State_Idle);

	mDoSyncForcedByPreviousSyncError = false;

	// Loop around doing backups
	do
	{
		// Flags used below
		bool storageLimitExceeded = false;
		bool doSync = false;
		bool mDoSyncForcedByCommand = false;

		// Is a delay necessary?
		box_time_t currentTime;

		do
		{
			// Check whether we should be stopping,
			// and don't run a sync if so.
			if(StopRun()) break;
			
			currentTime = GetCurrentBoxTime();

			// Pause a while, but no more than 
			// MAX_SLEEP_TIME seconds (use the conditional
			// because times are unsigned)
			box_time_t requiredDelay = 
				(mNextSyncTime < currentTime)
				? (0)
				: (mNextSyncTime - currentTime);

			// If there isn't automatic backup happening, 
			// set a long delay. And limit delays at the 
			// same time.
			if(!automaticBackup && !mDoSyncForcedByPreviousSyncError)
			{
				requiredDelay = SecondsToBoxTime(MAX_SLEEP_TIME);
			}
			else if(requiredDelay > SecondsToBoxTime(MAX_SLEEP_TIME))
			{
				requiredDelay = SecondsToBoxTime(MAX_SLEEP_TIME);
			}

			// Only delay if necessary
			if(requiredDelay > 0)
			{
				// Sleep somehow. There are choices 
				// on how this should be done,
				// depending on the state of the 
				// control connection
				if(mapCommandSocketInfo.get() != 0)
				{
					// A command socket exists, 
					// so sleep by waiting on it
					WaitOnCommandSocket(requiredDelay,
						doSync, mDoSyncForcedByCommand);
				}
				else
				{
					// No command socket or 
					// connection, just do a 
					// normal sleep
					time_t sleepSeconds = 
						BoxTimeToSeconds(requiredDelay);
					::sleep((sleepSeconds <= 0)
						? 1 : sleepSeconds);
				}
			}
			
			if ((automaticBackup || mDoSyncForcedByPreviousSyncError)
				&& currentTime >= mNextSyncTime)
			{
				doSync = true;
			}
		}
		while(!doSync && !StopRun());

		// Time of sync start, and if it's time for another sync 
		// (and we're doing automatic syncs), set the flag
		mCurrentSyncStartTime = GetCurrentBoxTime();
		if((automaticBackup || mDoSyncForcedByPreviousSyncError) &&
			mCurrentSyncStartTime >= mNextSyncTime)
		{
			doSync = true;
		}
		
		// Use a script to see if sync is allowed now?
		if(!mDoSyncForcedByCommand && doSync && !StopRun())
		{
			int d = UseScriptToSeeIfSyncAllowed();
			if(d > 0)
			{
				// Script has asked for a delay
				mNextSyncTime = GetCurrentBoxTime() + 
					SecondsToBoxTime(d);
				doSync = false;
			}
		}

		// Ready to sync? (but only if we're not supposed 
		// to be stopping)
		if(doSync && !StopRun())
		{
			RunSyncNowWithExceptionHandling();
		}
		
		// Set state
		SetState(storageLimitExceeded?State_StorageLimitExceeded:State_Idle);

	} while(!StopRun());
	
	// Make sure we have a clean start next time round (if restart)
	DeleteAllLocations();
	DeleteAllIDMaps();
}

void BackupDaemon::RunSyncNowWithExceptionHandling()
{
	OnBackupStart();

	// Do sync
	bool errorOccurred = false;
	int errorCode = 0, errorSubCode = 0;
	const char* errorString = "unknown";

	try
	{
		RunSyncNow();
	}
	catch(BoxException &e)
	{
		errorOccurred = true;
		errorString = e.what();
		errorCode = e.GetType();
		errorSubCode = e.GetSubType();
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Internal error during backup run: " << e.what());
		errorOccurred = true;
		errorString = e.what();
	}
	catch(...)
	{
		// TODO: better handling of exceptions here...
		// need to be very careful
		errorOccurred = true;
	}

	// do not retry immediately without a good reason
	mDoSyncForcedByPreviousSyncError = false;
	
	if(errorOccurred)
	{
		// Is it a berkely db failure?
		bool isBerkelyDbFailure = false;

		if (errorCode == BackupStoreException::ExceptionType
			&& errorSubCode == BackupStoreException::BerkelyDBFailure)
		{
			isBerkelyDbFailure = true;
		}

		if(isBerkelyDbFailure)
		{
			// Delete corrupt files
			DeleteCorruptBerkelyDbFiles();
		}

		// Clear state data
		// Go back to beginning of time
		mLastSyncTime = 0;
		mClientStoreMarker = BackupClientContext::ClientStoreMarker_NotKnown;	// no store marker, so download everything
		DeleteAllLocations();
		DeleteAllIDMaps();

		// Handle restart?
		if(StopRun())
		{
			BOX_NOTICE("Exception (" << errorCode
				<< "/" << errorSubCode 
				<< ") due to signal");
			OnBackupFinish();
			return;
		}

		NotifySysadmin(SysadminNotifier::BackupError);

		// If the Berkely db files get corrupted,
		// delete them and try again immediately.
		if(isBerkelyDbFailure)
		{
			BOX_ERROR("Berkely db inode map files corrupted, "
				"deleting and restarting scan. Renamed files "
				"and directories will not be tracked until "
				"after this scan.");
			::sleep(1);
		}
		else
		{
			// Not restart/terminate, pause and retry
			// Notify administrator
			SetState(State_Error);
			BOX_ERROR("Exception caught (" << errorString <<
				" " << errorCode << "/" << errorSubCode <<
				"), reset state and waiting to retry...");
			::sleep(10);
			mNextSyncTime = mCurrentSyncStartTime + 
				SecondsToBoxTime(100) +
				Random::RandomInt(mUpdateStoreInterval >> 
					SYNC_PERIOD_RANDOM_EXTRA_TIME_SHIFT_BY);
		}
	}
	// Notify system administrator about the final state of the backup
	else if(mReadErrorsOnFilesystemObjects)
	{
		NotifySysadmin(SysadminNotifier::ReadError);
	}
	else if(mStorageLimitExceeded)
	{
		NotifySysadmin(SysadminNotifier::StoreFull);
	}
	else
	{
		NotifySysadmin(SysadminNotifier::BackupOK);
	}
	
	// If we were retrying after an error, and this backup succeeded,
	// then now would be a good time to stop :-)
	mDoSyncForcedByPreviousSyncError = errorOccurred;

	OnBackupFinish();
}

void BackupDaemon::RunSyncNow()
{
	// Delete the serialised store object file,
	// so that we don't try to reload it after a
	// partially completed backup
	if(mDeleteStoreObjectInfoFile && 
		!DeleteStoreObjectInfo())
	{
		BOX_ERROR("Failed to delete the "
			"StoreObjectInfoFile, backup cannot "
			"continue safely.");
		THROW_EXCEPTION(ClientException, 
			FailedToDeleteStoreObjectInfoFile);
	}

	// In case the backup throws an exception,
	// we should not try to delete the store info
	// object file again.
	mDeleteStoreObjectInfoFile = false;

	const Configuration &conf(GetConfiguration());

	std::auto_ptr<FileLogger> fileLogger;

	if (conf.KeyExists("LogFile"))
	{
		Log::Level level = Log::INFO;
		if (conf.KeyExists("LogFileLevel"))
		{
			level = Logging::GetNamedLevel(
				conf.GetKeyValue("LogFileLevel"));
		}
		fileLogger.reset(new FileLogger(conf.GetKeyValue("LogFile"),
			level));
	}

	std::string extendedLogFile;
	if (conf.KeyExists("ExtendedLogFile"))
	{
		extendedLogFile = conf.GetKeyValue("ExtendedLogFile");
	}
	
	if (conf.KeyExists("LogAllFileAccess"))
	{
		mLogAllFileAccess = conf.GetKeyValueBool("LogAllFileAccess");
	}
	
	// Then create a client context object (don't 
	// just connect, as this may be unnecessary)
	BackupClientContext clientContext
	(
		*mpLocationResolver, 
		mTlsContext, 
		conf.GetKeyValue("StoreHostname"),
		conf.GetKeyValueInt("StorePort"),
		conf.GetKeyValueInt("AccountNumber"), 
		conf.GetKeyValueBool("ExtendedLogging"),
		conf.KeyExists("ExtendedLogFile"),
		extendedLogFile, *mpProgressNotifier
	);
		
	// The minimum age a file needs to be before it will be
	// considered for uploading
	box_time_t minimumFileAge = SecondsToBoxTime(
		conf.GetKeyValueInt("MinimumFileAge"));

	// The maximum time we'll wait to upload a file, regardless
	// of how often it's modified
	box_time_t maxUploadWait = SecondsToBoxTime(
		conf.GetKeyValueInt("MaxUploadWait"));
	// Adjust by subtracting the minimum file age, so is relative
	// to sync period end in comparisons
	if (maxUploadWait > minimumFileAge)
	{
		maxUploadWait -= minimumFileAge;
	}
	else
	{
		maxUploadWait = 0;
	}

	// Calculate the sync period of files to examine
	box_time_t syncPeriodStart = mLastSyncTime;
	box_time_t syncPeriodEnd = GetCurrentBoxTime() - minimumFileAge;

	if(syncPeriodStart >= syncPeriodEnd &&
		syncPeriodStart - syncPeriodEnd < minimumFileAge)
	{
		// This can happen if we receive a force-sync
		// command less than minimumFileAge after
		// the last sync. Deal with it by moving back
		// syncPeriodStart, which should not do any
		// damage.
		syncPeriodStart = syncPeriodEnd -
			SecondsToBoxTime(1);
	}

	if(syncPeriodStart >= syncPeriodEnd)
	{
		BOX_ERROR("Invalid (negative) sync period: "
			"perhaps your clock is going "
			"backwards (" << syncPeriodStart <<
			" to " << syncPeriodEnd << ")");
		THROW_EXCEPTION(ClientException,
			ClockWentBackwards);
	}

	// Check logic
	ASSERT(syncPeriodEnd > syncPeriodStart);
	// Paranoid check on sync times
	if(syncPeriodStart >= syncPeriodEnd) return;
	
	// Adjust syncPeriodEnd to emulate snapshot 
	// behaviour properly
	box_time_t syncPeriodEndExtended = syncPeriodEnd;

	// Using zero min file age?
	if(minimumFileAge == 0)
	{
		// Add a year on to the end of the end time,
		// to make sure we sync files which are 
		// modified after the scan run started.
		// Of course, they may be eligible to be 
		// synced again the next time round,
		// but this should be OK, because the changes 
		// only upload should upload no data.
		syncPeriodEndExtended += SecondsToBoxTime(
			(time_t)(356*24*3600));
	}

	// Set up the sync parameters
	BackupClientDirectoryRecord::SyncParams params(*mpRunStatusProvider,
		*mpSysadminNotifier, *mpProgressNotifier, clientContext);
	params.mSyncPeriodStart = syncPeriodStart;
	params.mSyncPeriodEnd = syncPeriodEndExtended;
	// use potentially extended end time
	params.mMaxUploadWait = maxUploadWait;
	params.mFileTrackingSizeThreshold = 
		conf.GetKeyValueInt("FileTrackingSizeThreshold");
	params.mDiffingUploadSizeThreshold = 
		conf.GetKeyValueInt("DiffingUploadSizeThreshold");
	params.mMaxFileTimeInFuture = 
		SecondsToBoxTime(conf.GetKeyValueInt("MaxFileTimeInFuture"));
	mDeleteRedundantLocationsAfter =
		conf.GetKeyValueInt("DeleteRedundantLocationsAfter");
	mStorageLimitExceeded = false;
	mReadErrorsOnFilesystemObjects = false;

	// Setup various timings
	int maximumDiffingTime = 600;
	int keepAliveTime = 60;

	// max diffing time, keep-alive time
	if(conf.KeyExists("MaximumDiffingTime"))
	{
		maximumDiffingTime = conf.GetKeyValueInt("MaximumDiffingTime");
	}
	if(conf.KeyExists("KeepAliveTime"))
	{
		keepAliveTime = conf.GetKeyValueInt("KeepAliveTime");
	}

	clientContext.SetMaximumDiffingTime(maximumDiffingTime);
	clientContext.SetKeepAliveTime(keepAliveTime);
	
	// Set store marker
	clientContext.SetClientStoreMarker(mClientStoreMarker);
	
	// Set up the locations, if necessary -- 
	// need to do it here so we have a 
	// (potential) connection to use
	if(mLocations.empty())
	{
		const Configuration &locations(
			conf.GetSubConfiguration(
				"BackupLocations"));
		
		// Make sure all the directory records
		// are set up
		SetupLocations(clientContext, locations);
	}
	
	mpProgressNotifier->NotifyIDMapsSetup(clientContext);
	
	// Get some ID maps going
	SetupIDMapsForSync();

	// Delete any unused directories?
	DeleteUnusedRootDirEntries(clientContext);
					
	// Go through the records, syncing them
	for(std::vector<Location *>::const_iterator 
		i(mLocations.begin()); 
		i != mLocations.end(); ++i)
	{
		// Set current and new ID map pointers
		// in the context
		clientContext.SetIDMaps(mCurrentIDMaps[(*i)->mIDMapIndex],
			mNewIDMaps[(*i)->mIDMapIndex]);
	
		// Set exclude lists (context doesn't
		// take ownership)
		clientContext.SetExcludeLists(
			(*i)->mpExcludeFiles,
			(*i)->mpExcludeDirs);

		// Sync the directory
		(*i)->mpDirectoryRecord->SyncDirectory(
			params,
			BackupProtocolClientListDirectory::RootDirectory,
			(*i)->mPath, std::string("/") + (*i)->mName);

		// Unset exclude lists (just in case)
		clientContext.SetExcludeLists(0, 0);
	}
	
	// Perform any deletions required -- these are
	// delayed until the end to allow renaming to 
	// happen neatly.
	clientContext.PerformDeletions();

	// Close any open connection
	clientContext.CloseAnyOpenConnection();
	
	// Get the new store marker
	mClientStoreMarker = clientContext.GetClientStoreMarker();
	mStorageLimitExceeded = clientContext.StorageLimitExceeded();
	mReadErrorsOnFilesystemObjects =
		params.mReadErrorsOnFilesystemObjects;

	if(!mStorageLimitExceeded)
	{
		// The start time of the next run is the end time of this
		// run. This is only done if the storage limit wasn't
		// exceeded (as things won't have been done properly if
		// it was)
		mLastSyncTime = syncPeriodEnd;
	}

	// Commit the ID Maps
	CommitIDMapsAfterSync();

	// Calculate when the next sync run should be
	mNextSyncTime = mCurrentSyncStartTime + 
		mUpdateStoreInterval + 
		Random::RandomInt(mUpdateStoreInterval >>
		SYNC_PERIOD_RANDOM_EXTRA_TIME_SHIFT_BY);

	// --------------------------------------------------------------------------------------------

	// We had a successful backup, save the store 
	// info. If we save successfully, we must 
	// delete the file next time we start a backup

	mDeleteStoreObjectInfoFile = 
		SerializeStoreObjectInfo(mLastSyncTime,
			mNextSyncTime);

	// --------------------------------------------------------------------------------------------
}

void BackupDaemon::OnBackupStart()
{
	// Touch a file to record times in filesystem
	TouchFileInWorkingDir("last_sync_start");

	// Reset statistics on uploads
	BackupStoreFile::ResetStats();
	
	// Tell anything connected to the command socket
	SendSyncStartOrFinish(true /* start */);
	
	// Notify administrator
	NotifySysadmin(SysadminNotifier::BackupStart);

	// Set state and log start
	SetState(State_Connected);
	BOX_NOTICE("Beginning scan of local files");
}

void BackupDaemon::OnBackupFinish()
{
	// Log
	BOX_NOTICE("Finished scan of local files");

	// Log the stats
	BOX_NOTICE("File statistics: total file size uploaded "
		<< BackupStoreFile::msStats.mBytesInEncodedFiles
		<< ", bytes already on server "
		<< BackupStoreFile::msStats.mBytesAlreadyOnServer
		<< ", encoded size "
		<< BackupStoreFile::msStats.mTotalFileStreamSize);

	// Reset statistics again
	BackupStoreFile::ResetStats();

	// Notify administrator
	NotifySysadmin(SysadminNotifier::BackupFinish);

	// Tell anything connected to the command socket
	SendSyncStartOrFinish(false /* finish */);

	// Touch a file to record times in filesystem
	TouchFileInWorkingDir("last_sync_finish");
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::UseScriptToSeeIfSyncAllowed()
//		Purpose: Private. Use a script to see if the sync should be allowed (if configured)
//				 Returns -1 if it's allowed, time in seconds to wait otherwise.
//		Created: 21/6/04
//
// --------------------------------------------------------------------------
int BackupDaemon::UseScriptToSeeIfSyncAllowed()
{
	const Configuration &conf(GetConfiguration());

	// Got a script to run?
	if(!conf.KeyExists("SyncAllowScript"))
	{
		// No. Do sync.
		return -1;
	}

	// If there's no result, try again in five minutes
	int waitInSeconds = (60*5);

	// Run it?
	pid_t pid = 0;
	try
	{
		std::auto_ptr<IOStream> pscript(LocalProcessStream(conf.GetKeyValue("SyncAllowScript").c_str(), pid));

		// Read in the result
		IOStreamGetLine getLine(*pscript);
		std::string line;
		if(getLine.GetLine(line, true, 30000)) // 30 seconds should be enough
		{
			// Got a string, interpret
			if(line == "now")
			{
				// Script says do it now. Obey.
				waitInSeconds = -1;
			}
			else
			{
				try
				{
					// How many seconds to wait?
					waitInSeconds = BoxConvert::Convert<int32_t, const std::string&>(line);
				}
				catch(ConversionException &e)
				{
					BOX_ERROR("Invalid output "
						"from SyncAllowScript '"
						<< conf.GetKeyValue("SyncAllowScript")
						<< "': '" << line << "'");
					throw;
				}

				BOX_NOTICE("Delaying sync by " << waitInSeconds
					<< " seconds (SyncAllowScript '"
					<< conf.GetKeyValue("SyncAllowScript")
					<< "')");
			}
		}
		
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Internal error running SyncAllowScript: "
			<< e.what());
	}
	catch(...)
	{
		// Ignore any exceptions
		// Log that something bad happened
		BOX_ERROR("Error running SyncAllowScript '"
			<< conf.GetKeyValue("SyncAllowScript") << "'");
	}

	// Wait and then cleanup child process, if any
	if(pid != 0)
	{
		int status = 0;
		::waitpid(pid, &status, 0);
	}

	return waitInSeconds;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::WaitOnCommandSocket(box_time_t, bool &, bool &)
//		Purpose: Waits on a the command socket for a time of UP TO the required time
//				 but may be much less, and handles a command if necessary.
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::WaitOnCommandSocket(box_time_t RequiredDelay, bool &DoSyncFlagOut, bool &SyncIsForcedOut)
{
	ASSERT(mapCommandSocketInfo.get());
	if(!mapCommandSocketInfo.get())
	{
		// failure case isn't too bad
		::sleep(1);
		return;
	}
	
	BOX_TRACE("Wait on command socket, delay = " << RequiredDelay);
	
	try
	{
		// Timeout value for connections and things
		int timeout = ((int)BoxTimeToMilliSeconds(RequiredDelay)) + 1;
		// Handle bad boundary cases
		if(timeout <= 0) timeout = 1;
		if(timeout == INFTIM) timeout = 100000;

		// Wait for socket connection, or handle a command?
		if(mapCommandSocketInfo->mpConnectedSocket.get() == 0)
		{
			// No connection, listen for a new one
			mapCommandSocketInfo->mpConnectedSocket.reset(mapCommandSocketInfo->mListeningSocket.Accept(timeout).release());
			
			if(mapCommandSocketInfo->mpConnectedSocket.get() == 0)
			{
				// If a connection didn't arrive, there was a timeout, which means we've
				// waited long enough and it's time to go.
				return;
			}
			else
			{
#ifdef PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
				bool uidOK = true;
				BOX_WARNING("On this platform, no security check can be made on the credentials of peers connecting to the command socket. (bbackupctl)");
#else
				// Security check -- does the process connecting to this socket have
				// the same UID as this process?
				bool uidOK = false;
				// BLOCK
				{
					uid_t remoteEUID = 0xffff;
					gid_t remoteEGID = 0xffff;
					if(mapCommandSocketInfo->mpConnectedSocket->GetPeerCredentials(remoteEUID, remoteEGID))
					{
						// Credentials are available -- check UID
						if(remoteEUID == ::getuid())
						{
							// Acceptable
							uidOK = true;
						}
					}
				}
#endif // PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
				
				// Is this an acceptable connection?
				if(!uidOK)
				{
					// Dump the connection
					BOX_ERROR("Incoming command connection from peer had different user ID than this process, or security check could not be completed.");
					mapCommandSocketInfo->mpConnectedSocket.reset();
					return;
				}
				else
				{
					// Log
					BOX_INFO("Connection from command socket");
					
					// Send a header line summarising the configuration and current state
					const Configuration &conf(GetConfiguration());
					char summary[256];
					int summarySize = sprintf(summary, "bbackupd: %d %d %d %d\nstate %d\n",
						conf.GetKeyValueBool("AutomaticBackup"),
						conf.GetKeyValueInt("UpdateStoreInterval"),
						conf.GetKeyValueInt("MinimumFileAge"),
						conf.GetKeyValueInt("MaxUploadWait"),
						mState);
					mapCommandSocketInfo->mpConnectedSocket->Write(summary, summarySize);
					
					// Set the timeout to something very small, so we don't wait too long on waiting
					// for any incoming data
					timeout = 10; // milliseconds
				}
			}
		}

		// So there must be a connection now.
		ASSERT(mapCommandSocketInfo->mpConnectedSocket.get() != 0);
		
		// Is there a getline object ready?
		if(mapCommandSocketInfo->mpGetLine == 0)
		{
			// Create a new one
			mapCommandSocketInfo->mpGetLine = new IOStreamGetLine(*(mapCommandSocketInfo->mpConnectedSocket.get()));
		}
		
		// Ping the remote side, to provide errors which will mean the socket gets closed
		mapCommandSocketInfo->mpConnectedSocket->Write("ping\n", 5);
		
		// Wait for a command or something on the socket
		std::string command;
		while(mapCommandSocketInfo->mpGetLine != 0 && !mapCommandSocketInfo->mpGetLine->IsEOF()
			&& mapCommandSocketInfo->mpGetLine->GetLine(command, false /* no preprocessing */, timeout))
		{
			BOX_TRACE("Receiving command '" << command 
				<< "' over command socket");
			
			bool sendOK = false;
			bool sendResponse = true;
		
			// Command to process!
			if(command == "quit" || command == "")
			{
				// Close the socket.
				CloseCommandConnection();
				sendResponse = false;
			}
			else if(command == "sync")
			{
				// Sync now!
				DoSyncFlagOut = true;
				SyncIsForcedOut = false;
				sendOK = true;
			}
			else if(command == "force-sync")
			{
				// Sync now (forced -- overrides any SyncAllowScript)
				DoSyncFlagOut = true;
				SyncIsForcedOut = true;
				sendOK = true;
			}
			else if(command == "reload")
			{
				// Reload the configuration
				SetReloadConfigWanted();
				sendOK = true;
			}
			else if(command == "terminate")
			{
				// Terminate the daemon cleanly
				SetTerminateWanted();
				sendOK = true;
			}
			
			// Send a response back?
			if(sendResponse)
			{
				mapCommandSocketInfo->mpConnectedSocket->Write(sendOK?"ok\n":"error\n", sendOK?3:6);
			}
			
			// Set timeout to something very small, so this just checks for data which is waiting
			timeout = 1;
		}
		
		// Close on EOF?
		if(mapCommandSocketInfo->mpGetLine != 0 && mapCommandSocketInfo->mpGetLine->IsEOF())
		{
			CloseCommandConnection();
		}
	}
	catch(ConnectionException &ce)
	{
		BOX_NOTICE("Failed to write to command socket: " << ce.what());

		// If an error occurs, and there is a connection active,
		// just close that connection and continue. Otherwise,
		// let the error propagate.

		if(mapCommandSocketInfo->mpConnectedSocket.get() == 0)
		{
			throw; // thread will die
		}
		else
		{
			// Close socket and ignore error
			CloseCommandConnection();
		}
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to write to command socket: " <<
			e.what());

		// If an error occurs, and there is a connection active,
		// just close that connection and continue. Otherwise,
		// let the error propagate.

		if(mapCommandSocketInfo->mpConnectedSocket.get() == 0)
		{
			throw; // thread will die
		}
		else
		{
			// Close socket and ignore error
			CloseCommandConnection();
		}
	}
	catch(...)
	{
		BOX_ERROR("Failed to write to command socket: unknown error");

		// If an error occurs, and there is a connection active,
		// just close that connection and continue. Otherwise,
		// let the error propagate.

		if(mapCommandSocketInfo->mpConnectedSocket.get() == 0)
		{
			throw; // thread will die
		}
		else
		{
			// Close socket and ignore error
			CloseCommandConnection();
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::CloseCommandConnection()
//		Purpose: Close the command connection, ignoring any errors
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::CloseCommandConnection()
{
	try
	{
		BOX_TRACE("Closing command connection");
		
		if(mapCommandSocketInfo->mpGetLine)
		{
			delete mapCommandSocketInfo->mpGetLine;
			mapCommandSocketInfo->mpGetLine = 0;
		}
		mapCommandSocketInfo->mpConnectedSocket.reset();
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Internal error while closing command "
			"socket: " << e.what());
	}
	catch(...)
	{
		// Ignore any errors
	}
}


// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemon.cpp
//		Purpose: Send a start or finish sync message to the command socket, if it's connected.
//				 
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::SendSyncStartOrFinish(bool SendStart)
{
	// The bbackupctl program can't rely on a state change, because it 
	// may never change if the server doesn't need to be contacted.

	if(mapCommandSocketInfo.get() &&
		mapCommandSocketInfo->mpConnectedSocket.get() != 0)
	{
		std::string message = SendStart ? "start-sync" : "finish-sync";
		try
		{
			message += "\n";
			mapCommandSocketInfo->mpConnectedSocket->Write(
				message.c_str(), message.size());
		}
		catch(std::exception &e)
		{
			BOX_ERROR("Internal error while sending to "
				"command socket client: " << e.what());
			CloseCommandConnection();
		}
		catch(...)
		{
			CloseCommandConnection();
		}
	}
}




#if !defined(HAVE_STRUCT_STATFS_F_MNTONNAME) && !defined(HAVE_STRUCT_STATVFS_F_NMTONNAME)
	// string comparison ordering for when mount points are handled
	// by code, rather than the OS.
	typedef struct
	{
		bool operator()(const std::string &s1, const std::string &s2)
		{
			if(s1.size() == s2.size())
			{
				// Equal size, sort according to natural sort order
				return s1 < s2;
			}
			else
			{
				// Make sure longer strings go first
				return s1.size() > s2.size();
			}
		}
	} mntLenCompare;
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::SetupLocations(BackupClientContext &, const Configuration &)
//		Purpose: Makes sure that the list of directories records is correctly set up
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
void BackupDaemon::SetupLocations(BackupClientContext &rClientContext, const Configuration &rLocationsConf)
{
	if(!mLocations.empty())
	{
		// Looks correctly set up
		return;
	}

	// Make sure that if a directory is reinstated, then it doesn't get deleted	
	mDeleteUnusedRootDirEntriesAfter = 0;
	mUnusedRootDirEntries.clear();

	// Just a check to make sure it's right.
	DeleteAllLocations();
	
	// Going to need a copy of the root directory. Get a connection,
	// and fetch it.
	BackupProtocolClient &connection(rClientContext.GetConnection());
	
	// Ask server for a list of everything in the root directory,
	// which is a directory itself
	std::auto_ptr<BackupProtocolClientSuccess> dirreply(
		connection.QueryListDirectory(
			BackupProtocolClientListDirectory::RootDirectory,
			// only directories
			BackupProtocolClientListDirectory::Flags_Dir,
			// exclude old/deleted stuff
			BackupProtocolClientListDirectory::Flags_Deleted |
			BackupProtocolClientListDirectory::Flags_OldVersion,
			false /* no attributes */));

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(connection.ReceiveStream());
	dir.ReadFromStream(*dirstream, connection.GetTimeout());
	
	// Map of mount names to ID map index
	std::map<std::string, int> mounts;
	int numIDMaps = 0;

#ifdef HAVE_MOUNTS
#if !defined(HAVE_STRUCT_STATFS_F_MNTONNAME) && !defined(HAVE_STRUCT_STATVFS_F_MNTONNAME)
	// Linux and others can't tell you where a directory is mounted. So we
	// have to read the mount entries from /etc/mtab! Bizarre that the OS
	// itself can't tell you, but there you go.
	std::set<std::string, mntLenCompare> mountPoints;
	// BLOCK
	FILE *mountPointsFile = 0;

#ifdef HAVE_STRUCT_MNTENT_MNT_DIR
	// Open mounts file
	mountPointsFile = ::setmntent("/proc/mounts", "r");
	if(mountPointsFile == 0)
	{
		mountPointsFile = ::setmntent("/etc/mtab", "r");
	}
	if(mountPointsFile == 0)
	{
		THROW_EXCEPTION(CommonException, OSFileError);
	}

	try
	{
		// Read all the entries, and put them in the set
		struct mntent *entry = 0;
		while((entry = ::getmntent(mountPointsFile)) != 0)
		{
			BOX_TRACE("Found mount point at " << entry->mnt_dir);
			mountPoints.insert(std::string(entry->mnt_dir));
		}

		// Close mounts file
		::endmntent(mountPointsFile);
	}
	catch(...)
	{
		::endmntent(mountPointsFile);
		throw;
	}
#else // ! HAVE_STRUCT_MNTENT_MNT_DIR
	// Open mounts file
	mountPointsFile = ::fopen("/etc/mnttab", "r");
	if(mountPointsFile == 0)
	{
		THROW_EXCEPTION(CommonException, OSFileError);
	}

	try
	{
		// Read all the entries, and put them in the set
		struct mnttab entry;
		while(getmntent(mountPointsFile, &entry) == 0)
		{
			BOX_TRACE("Found mount point at " << entry.mnt_mountp);
			mountPoints.insert(std::string(entry.mnt_mountp));
		}

		// Close mounts file
		::fclose(mountPointsFile);
	}
	catch(...)
	{
		::fclose(mountPointsFile);
		throw;
	}
#endif // HAVE_STRUCT_MNTENT_MNT_DIR
	// Check sorting and that things are as we expect
	ASSERT(mountPoints.size() > 0);
#ifndef NDEBUG
	{
		std::set<std::string, mntLenCompare>::reverse_iterator i(mountPoints.rbegin());
		ASSERT(*i == "/");
	}
#endif // n NDEBUG
#endif // n HAVE_STRUCT_STATFS_F_MNTONNAME || n HAVE_STRUCT_STATVFS_F_MNTONNAME
#endif // HAVE_MOUNTS

	// Then... go through each of the entries in the configuration,
	// making sure there's a directory created for it.
	std::vector<std::string> locNames =
		rLocationsConf.GetSubConfigurationNames();
	
	for(std::vector<std::string>::iterator
		pLocName  = locNames.begin();
		pLocName != locNames.end();
		pLocName++)
	{
		const Configuration& rConfig(
			rLocationsConf.GetSubConfiguration(*pLocName));
		BOX_TRACE("new location: " << *pLocName);
		
		// Create a record for it
		std::auto_ptr<Location> apLoc(new Location);

		try
		{
			// Setup names in the location record
			apLoc->mName = *pLocName;
			apLoc->mPath = rConfig.GetKeyValue("Path");
			
			// Read the exclude lists from the Configuration
			apLoc->mpExcludeFiles = BackupClientMakeExcludeList_Files(rConfig);
			apLoc->mpExcludeDirs = BackupClientMakeExcludeList_Dirs(rConfig);

			// Does this exist on the server?
			// Remove from dir object early, so that if we fail
			// to stat the local directory, we still don't 
			// consider to remote one for deletion.
			BackupStoreDirectory::Iterator iter(dir);
			BackupStoreFilenameClear dirname(apLoc->mName);	// generate the filename
			BackupStoreDirectory::Entry *en = iter.FindMatchingClearName(dirname);
			int64_t oid = 0;
			if(en != 0)
			{
				oid = en->GetObjectID();
				
				// Delete the entry from the directory, so we get a list of
				// unused root directories at the end of this.
				dir.DeleteEntry(oid);
			}
		
			// Do a fsstat on the pathname to find out which mount it's on
			{

#if defined HAVE_STRUCT_STATFS_F_MNTONNAME || defined HAVE_STRUCT_STATVFS_F_MNTONNAME || defined WIN32

				// BSD style statfs -- includes mount point, which is nice.
#ifdef HAVE_STRUCT_STATVFS_F_MNTONNAME
				struct statvfs s;
				if(::statvfs(apLoc->mPath.c_str(), &s) != 0)
#else // HAVE_STRUCT_STATVFS_F_MNTONNAME
				struct statfs s;
				if(::statfs(apLoc->mPath.c_str(), &s) != 0)
#endif // HAVE_STRUCT_STATVFS_F_MNTONNAME
				{
					BOX_LOG_SYS_WARNING("Failed to stat location "
						"path '" << apLoc->mPath <<
						"', skipping location '" <<
						apLoc->mName << "'");
					continue;
				}

				// Where the filesystem is mounted
				std::string mountName(s.f_mntonname);

#else // !HAVE_STRUCT_STATFS_F_MNTONNAME && !WIN32

				// Warn in logs if the directory isn't absolute
				if(apLoc->mPath[0] != '/')
				{
					BOX_WARNING("Location path '"
						<< apLoc->mPath 
						<< "' is not absolute");
				}
				// Go through the mount points found, and find a suitable one
				std::string mountName("/");
				{
					std::set<std::string, mntLenCompare>::const_iterator i(mountPoints.begin());
					BOX_TRACE(mountPoints.size() 
						<< " potential mount points");
					for(; i != mountPoints.end(); ++i)
					{
						// Compare first n characters with the filename
						// If it matches, the file belongs in that mount point
						// (sorting order ensures this)
						BOX_TRACE("checking against mount point " << *i);
						if(::strncmp(i->c_str(), apLoc->mPath.c_str(), i->size()) == 0)
						{
							// Match
							mountName = *i;
							break;
						}
					}
					BOX_TRACE("mount point chosen for "
						<< apLoc->mPath << " is "
						<< mountName);
				}

#endif
				
				// Got it?
				std::map<std::string, int>::iterator f(mounts.find(mountName));
				if(f != mounts.end())
				{
					// Yes -- store the index
					apLoc->mIDMapIndex = f->second;
				}
				else
				{
					// No -- new index
					apLoc->mIDMapIndex = numIDMaps;
					mounts[mountName] = numIDMaps;
					
					// Store the mount name
					mIDMapMounts.push_back(mountName);
					
					// Increment number of maps
					++numIDMaps;
				}
			}
		
			// Does this exist on the server?
			if(en == 0)
			{
				// Doesn't exist, so it has to be created on the server. Let's go!
				// First, get the directory's attributes and modification time
				box_time_t attrModTime = 0;
				BackupClientFileAttributes attr;
				try
				{
					attr.ReadAttributes(apLoc->mPath.c_str(), 
						true /* directories have zero mod times */,
						0 /* not interested in mod time */, 
						&attrModTime /* get the attribute modification time */);
				}
				catch (BoxException &e)
				{
					BOX_ERROR("Failed to get attributes "
						"for path '" << apLoc->mPath
						<< "', skipping location '" <<
						apLoc->mName << "'");
					continue;
				}
				
				// Execute create directory command
				try
				{
					MemBlockStream attrStream(attr);
					std::auto_ptr<BackupProtocolClientSuccess>
						dirCreate(connection.QueryCreateDirectory(
						BackupProtocolClientListDirectory::RootDirectory,
						attrModTime, dirname, attrStream));
						
					// Object ID for later creation
					oid = dirCreate->GetObjectID();
				}
				catch (BoxException &e)
				{
					BOX_ERROR("Failed to create remote "
						"directory '/" << apLoc->mName <<
						"', skipping location '" <<
						apLoc->mName << "'");
					continue;
				}

			}

			// Create and store the directory object for the root of this location
			ASSERT(oid != 0);
			BackupClientDirectoryRecord *precord =
				new BackupClientDirectoryRecord(oid, *pLocName);
			apLoc->mpDirectoryRecord.reset(precord);
			
			// Push it back on the vector of locations
			mLocations.push_back(apLoc.release());
		}
		catch (std::exception &e)
		{
			BOX_ERROR("Failed to configure location '"
				<< apLoc->mName << "' path '"
				<< apLoc->mPath << "': " << e.what() <<
				": please check for previous errors");
			throw;
		}
		catch(...)
		{
			BOX_ERROR("Failed to configure location '"
				<< apLoc->mName << "' path '"
				<< apLoc->mPath << "': please check for "
				"previous errors");
			throw;
		}
	}
	
	// Any entries in the root directory which need deleting?
	if(dir.GetNumberOfEntries() > 0)
	{
		box_time_t now = GetCurrentBoxTime();

		// This should reset the timer if the list of unused
		// locations changes, but it will not if the number of
		// unused locations does not change, but the locations
		// do change, e.g. one mysteriously appears and another
		// mysteriously appears. (FIXME)
		if (dir.GetNumberOfEntries() != mUnusedRootDirEntries.size() ||
			mDeleteUnusedRootDirEntriesAfter == 0)
		{
			mDeleteUnusedRootDirEntriesAfter = now + 
				SecondsToBoxTime(mDeleteRedundantLocationsAfter);
		}

		int secs = BoxTimeToSeconds(mDeleteUnusedRootDirEntriesAfter
			- now);

		BOX_NOTICE(dir.GetNumberOfEntries() << " redundant locations "
			"in root directory found, will delete from store "
			"after " << secs << " seconds.");

		// Store directories in list of things to delete
		mUnusedRootDirEntries.clear();
		BackupStoreDirectory::Iterator iter(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = iter.Next()) != 0)
		{
			// Add name to list
			BackupStoreFilenameClear clear(en->GetName());
			const std::string &name(clear.GetClearFilename());
			mUnusedRootDirEntries.push_back(
				std::pair<int64_t,std::string>
				(en->GetObjectID(), name));
			// Log this
			BOX_INFO("Unused location in root: " << name);
		}
		ASSERT(mUnusedRootDirEntries.size() > 0);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::SetupIDMapsForSync()
//		Purpose: Sets up ID maps for the sync process -- make sure they're all there
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
void BackupDaemon::SetupIDMapsForSync()
{
	// Need to do different things depending on whether it's an
	// in memory implementation, or whether it's all stored on disc.
	
#ifdef BACKIPCLIENTINODETOIDMAP_IN_MEMORY_IMPLEMENTATION

	// Make sure we have some blank, empty ID maps
	DeleteIDMapVector(mNewIDMaps);
	FillIDMapVector(mNewIDMaps, true /* new maps */);

	// Then make sure that the current maps have objects,
	// even if they are empty (for the very first run)
	if(mCurrentIDMaps.empty())
	{
		FillIDMapVector(mCurrentIDMaps, false /* current maps */);
	}

#else

	// Make sure we have some blank, empty ID maps
	DeleteIDMapVector(mNewIDMaps);
	FillIDMapVector(mNewIDMaps, true /* new maps */);
	DeleteIDMapVector(mCurrentIDMaps);
	FillIDMapVector(mCurrentIDMaps, false /* new maps */);

#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::FillIDMapVector(std::vector<BackupClientInodeToIDMap *> &)
//		Purpose: Fills the vector with the right number of empty ID maps
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
void BackupDaemon::FillIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector, bool NewMaps)
{
	ASSERT(rVector.size() == 0);
	rVector.reserve(mIDMapMounts.size());
	
	for(unsigned int l = 0; l < mIDMapMounts.size(); ++l)
	{
		// Create the object
		BackupClientInodeToIDMap *pmap = new BackupClientInodeToIDMap();
		try
		{
			// Get the base filename of this map
			std::string filename;
			MakeMapBaseName(l, filename);
			
			// If it's a new one, add a suffix
			if(NewMaps)
			{
				filename += ".n";
			}

			// If it's not a new map, it may not exist in which case an empty map should be created
			if(!NewMaps && !FileExists(filename.c_str()))
			{
				pmap->OpenEmpty();
			}
			else
			{
				// Open the map
				pmap->Open(filename.c_str(), !NewMaps /* read only */, NewMaps /* create new */);
			}
			
			// Store on vector
			rVector.push_back(pmap);
		}
		catch(...)
		{
			delete pmap;
			throw;
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DeleteCorruptBerkelyDbFiles()
//		Purpose: Delete the Berkely db files from disc after they have been corrupted.
//		Created: 14/9/04
//
// --------------------------------------------------------------------------
void BackupDaemon::DeleteCorruptBerkelyDbFiles()
{
	for(unsigned int l = 0; l < mIDMapMounts.size(); ++l)
	{
		// Get the base filename of this map
		std::string filename;
		MakeMapBaseName(l, filename);
		
		// Delete the file
		BOX_TRACE("Deleting " << filename);
		::unlink(filename.c_str());
		
		// Add a suffix for the new map
		filename += ".n";

		// Delete that too
		BOX_TRACE("Deleting " << filename);
		::unlink(filename.c_str());
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    MakeMapBaseName(unsigned int, std::string &)
//		Purpose: Makes the base name for a inode map
//		Created: 20/11/03
//
// --------------------------------------------------------------------------
void BackupDaemon::MakeMapBaseName(unsigned int MountNumber, std::string &rNameOut) const
{
	// Get the directory for the maps
	const Configuration &config(GetConfiguration());
	std::string dir(config.GetKeyValue("DataDirectory"));

	// Make a leafname
	std::string leaf(mIDMapMounts[MountNumber]);
	for(unsigned int z = 0; z < leaf.size(); ++z)
	{
		if(leaf[z] == DIRECTORY_SEPARATOR_ASCHAR)
		{
			leaf[z] = '_';
		}
	}

	// Build the final filename
	rNameOut = dir + DIRECTORY_SEPARATOR "mnt" + leaf;
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::CommitIDMapsAfterSync()
//		Purpose: Commits the new ID maps, so the 'new' maps are now the 'current' maps.
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
void BackupDaemon::CommitIDMapsAfterSync()
{
	// Need to do different things depending on whether it's an in memory implementation,
	// or whether it's all stored on disc.
	
#ifdef BACKIPCLIENTINODETOIDMAP_IN_MEMORY_IMPLEMENTATION
	// Remove the current ID maps
	DeleteIDMapVector(mCurrentIDMaps);

	// Copy the (pointers to) "new" maps over to be the new "current" maps
	mCurrentIDMaps = mNewIDMaps;
	
	// Clear the new ID maps vector (not delete them!)
	mNewIDMaps.clear();

#else

	// Get rid of the maps in memory (leaving them on disc of course)
	DeleteIDMapVector(mCurrentIDMaps);
	DeleteIDMapVector(mNewIDMaps);

	// Then move the old maps into the new places
	for(unsigned int l = 0; l < mIDMapMounts.size(); ++l)
	{
		std::string target;
		MakeMapBaseName(l, target);
		std::string newmap(target + ".n");
		
		// Try to rename
#ifdef WIN32
		// win32 rename doesn't overwrite existing files
		::remove(target.c_str());
#endif
		if(::rename(newmap.c_str(), target.c_str()) != 0)
		{
			BOX_LOG_SYS_ERROR("Failed to rename ID map: " <<
				newmap << " to " << target);
			THROW_EXCEPTION(CommonException, OSFileError)
		}
	}

#endif
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DeleteIDMapVector(std::vector<BackupClientInodeToIDMap *> &)
//		Purpose: Deletes the contents of a vector of ID maps
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
void BackupDaemon::DeleteIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector)
{
	while(!rVector.empty())
	{
		// Pop off list
		BackupClientInodeToIDMap *toDel = rVector.back();
		rVector.pop_back();
		
		// Close and delete
		toDel->Close();
		delete toDel;
	}
	ASSERT(rVector.size() == 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::FindLocationPathName(const std::string &, std::string &) const
//		Purpose: Tries to find the path of the root of a backup location. Returns true (and path in rPathOut)
//				 if it can be found, false otherwise.
//		Created: 12/11/03
//
// --------------------------------------------------------------------------
bool BackupDaemon::FindLocationPathName(const std::string &rLocationName, std::string &rPathOut) const
{
	// Search for the location
	for(std::vector<Location *>::const_iterator i(mLocations.begin()); i != mLocations.end(); ++i)
	{
		if((*i)->mName == rLocationName)
		{
			rPathOut = (*i)->mPath;
			return true;
		}
	}
	
	// Didn't find it
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::SetState(int)
//		Purpose: Record current action of daemon, and update process title to reflect this
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void BackupDaemon::SetState(int State)
{
	// Two little checks
	if(State == mState) return;
	if(State < 0) return;

	// Update
	mState = State;
	
	// Set process title
	const static char *stateText[] = {"idle", "connected", "error -- waiting for retry", "over limit on server -- not backing up"};
	SetProcessTitle(stateText[State]);

	// If there's a command socket connected, then inform it -- disconnecting from the
	// command socket if there's an error

	char newState[64];
	sprintf(newState, "state %d", State);
	std::string message = newState;

	message += "\n";

	if(!mapCommandSocketInfo.get())
	{
		return;
	}

	if(mapCommandSocketInfo->mpConnectedSocket.get() == 0)
	{
		return;
	}

	// Something connected to the command socket, tell it about the new state
	try
	{
		mapCommandSocketInfo->mpConnectedSocket->Write(message.c_str(),
			message.length());
	}
	catch(ConnectionException &ce)
	{
		BOX_NOTICE("Failed to write state to command socket: " <<
			ce.what());
		CloseCommandConnection();
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to write state to command socket: " <<
			e.what());
		CloseCommandConnection();
	}
	catch(...)
	{
		BOX_ERROR("Failed to write state to command socket: "
			"unknown error");
		CloseCommandConnection();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::TouchFileInWorkingDir(const char *)
//		Purpose: Make sure a zero length file of the name exists in the working directory.
//				 Use for marking times of events in the filesystem.
//		Created: 21/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::TouchFileInWorkingDir(const char *Filename)
{
	// Filename
	const Configuration &config(GetConfiguration());
	std::string fn(config.GetKeyValue("DataDirectory") + DIRECTORY_SEPARATOR_ASCHAR);
	fn += Filename;
	
	// Open and close it to update the timestamp
	FileStream touch(fn.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::NotifySysadmin(int)
//		Purpose: Run the script to tell the sysadmin about events
//			 which need attention.
//		Created: 25/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::NotifySysadmin(SysadminNotifier::EventCode Event)
{
	static const char *sEventNames[] = 
	{
		"store-full",
		"read-error", 
		"backup-error",
		"backup-start",
		"backup-finish",
		"backup-ok",
		0
	};

	// BOX_TRACE("sizeof(sEventNames)  == " << sizeof(sEventNames));
	// BOX_TRACE("sizeof(*sEventNames) == " << sizeof(*sEventNames));
	// BOX_TRACE("NotifyEvent__MAX == " << NotifyEvent__MAX);
	ASSERT((sizeof(sEventNames)/sizeof(*sEventNames)) == SysadminNotifier::MAX + 1);

	if(Event < 0 || Event >= SysadminNotifier::MAX)
	{
		BOX_ERROR("BackupDaemon::NotifySysadmin() called for "
			"invalid event code " << Event);
		THROW_EXCEPTION(BackupStoreException,
			BadNotifySysadminEventCode);
	}

	BOX_TRACE("BackupDaemon::NotifySysadmin() called, event = " << 
		sEventNames[Event]);

	if(!GetConfiguration().KeyExists("NotifyAlways") ||
		!GetConfiguration().GetKeyValueBool("NotifyAlways"))
	{
		// Don't send lots of repeated messages
		if(mLastNotifiedEvent == Event)
		{
			BOX_WARNING("Suppressing duplicate notification about " <<
				sEventNames[Event]);
			return;
		}
	}

	// Is there a notification script?
	const Configuration &conf(GetConfiguration());
	if(!conf.KeyExists("NotifyScript"))
	{
		// Log, and then return
		if(Event != SysadminNotifier::BackupStart &&
			Event != SysadminNotifier::BackupFinish)
		{
			BOX_ERROR("Not notifying administrator about event "
				<< sEventNames[Event] << " -- set NotifyScript "
				"to do this in future");
		}
		return;
	}

	// Script to run
	std::string script(conf.GetKeyValue("NotifyScript") + ' ' +
		sEventNames[Event]);
	
	// Log what we're about to do
	BOX_NOTICE("About to notify administrator about event "
		<< sEventNames[Event] << ", running script '"
		<< script << "'");
	
	// Then do it
	int returnCode = ::system(script.c_str());
	if(returnCode != 0)
	{
		BOX_ERROR("Notify script returned error code: " <<
			returnCode << " ('" << script << "')");
	}
	else if(Event != SysadminNotifier::BackupStart &&
		Event != SysadminNotifier::BackupFinish)
	{
		mLastNotifiedEvent = Event;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DeleteUnusedRootDirEntries(BackupClientContext &)
//		Purpose: Deletes any unused entries in the root directory, if they're scheduled to be deleted.
//		Created: 13/5/04
//
// --------------------------------------------------------------------------
void BackupDaemon::DeleteUnusedRootDirEntries(BackupClientContext &rContext)
{
	if(mUnusedRootDirEntries.empty())
	{
		BOX_INFO("Not deleting unused entries - none in list");
		return;
	}
	
	if(mDeleteUnusedRootDirEntriesAfter == 0)
	{
		BOX_INFO("Not deleting unused entries - "
			"zero delete time (bad)");
		return;
	}

	// Check time
	box_time_t now = GetCurrentBoxTime();
	if(now < mDeleteUnusedRootDirEntriesAfter)
	{
		int secs = BoxTimeToSeconds(mDeleteUnusedRootDirEntriesAfter
			- now);
		BOX_INFO("Not deleting unused entries - too early ("
			<< secs << " seconds remaining)");
		return;
	}

	// Entries to delete, and it's the right time to do so...
	BOX_NOTICE("Deleting unused locations from store root...");
	BackupProtocolClient &connection(rContext.GetConnection());
	for(std::vector<std::pair<int64_t,std::string> >::iterator
		i(mUnusedRootDirEntries.begin());
		i != mUnusedRootDirEntries.end(); ++i)
	{
		connection.QueryDeleteDirectory(i->first);
		rContext.GetProgressNotifier().NotifyFileDeleted(
			i->first, i->second);
	}

	// Reset state
	mDeleteUnusedRootDirEntriesAfter = 0;
	mUnusedRootDirEntries.clear();
}

// --------------------------------------------------------------------------

typedef struct
{
	int32_t mMagicValue;	// also the version number
	int32_t mNumEntries;
	int64_t mObjectID;		// this object ID
	int64_t mContainerID;	// ID of container
	uint64_t mAttributesModTime;
	int32_t mOptionsPresent;	// bit mask of optional sections / features present

} loc_StreamFormat;

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::Location::Location()
//		Purpose: Constructor
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
BackupDaemon::Location::Location()
	: mIDMapIndex(0),
	  mpExcludeFiles(0),
	  mpExcludeDirs(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::Location::~Location()
//		Purpose: Destructor
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
BackupDaemon::Location::~Location()
{
	// Clean up exclude locations
	if(mpExcludeDirs != 0)
	{
		delete mpExcludeDirs;
		mpExcludeDirs = 0;
	}
	if(mpExcludeFiles != 0)
	{
		delete mpExcludeFiles;
		mpExcludeFiles = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::Location::Deserialize(Archive & rArchive)
//		Purpose: Deserializes this object instance from a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void BackupDaemon::Location::Deserialize(Archive &rArchive)
{
	//
	//
	//
	mpDirectoryRecord.reset(NULL);
	if(mpExcludeFiles)
	{
		delete mpExcludeFiles;
		mpExcludeFiles = NULL;
	}
	if(mpExcludeDirs)
	{
		delete mpExcludeDirs;
		mpExcludeDirs = NULL;
	}

	//
	//
	//
	rArchive.Read(mName);
	rArchive.Read(mPath);
	rArchive.Read(mIDMapIndex);

	//
	//
	//
	int64_t aMagicMarker = 0;
	rArchive.Read(aMagicMarker);

	if(aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if(aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		BackupClientDirectoryRecord *pSubRecord = new BackupClientDirectoryRecord(0, "");
		if(!pSubRecord)
		{
			throw std::bad_alloc();
		}

		mpDirectoryRecord.reset(pSubRecord);
		mpDirectoryRecord->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(ClientException, CorruptStoreObjectInfoFile);
	}

	//
	//
	//
	rArchive.Read(aMagicMarker);

	if(aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if(aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		mpExcludeFiles = new ExcludeList;
		if(!mpExcludeFiles)
		{
			throw std::bad_alloc();
		}

		mpExcludeFiles->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(ClientException, CorruptStoreObjectInfoFile);
	}

	//
	//
	//
	rArchive.Read(aMagicMarker);

	if(aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if(aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		mpExcludeDirs = new ExcludeList;
		if(!mpExcludeDirs)
		{
			throw std::bad_alloc();
		}

		mpExcludeDirs->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(ClientException, CorruptStoreObjectInfoFile);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::Location::Serialize(Archive & rArchive)
//		Purpose: Serializes this object instance into a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void BackupDaemon::Location::Serialize(Archive & rArchive) const
{
	//
	//
	//
	rArchive.Write(mName);
	rArchive.Write(mPath);
	rArchive.Write(mIDMapIndex);

	//
	//
	//
	if(mpDirectoryRecord.get() == NULL)
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_NOOP;
		rArchive.Write(aMagicMarker);
	}
	else
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_RECURSE; // be explicit about whether recursion follows
		rArchive.Write(aMagicMarker);

		mpDirectoryRecord->Serialize(rArchive);
	}

	//
	//
	//
	if(!mpExcludeFiles)
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_NOOP;
		rArchive.Write(aMagicMarker);
	}
	else
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_RECURSE; // be explicit about whether recursion follows
		rArchive.Write(aMagicMarker);

		mpExcludeFiles->Serialize(rArchive);
	}

	//
	//
	//
	if(!mpExcludeDirs)
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_NOOP;
		rArchive.Write(aMagicMarker);
	}
	else
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_RECURSE; // be explicit about whether recursion follows
		rArchive.Write(aMagicMarker);

		mpExcludeDirs->Serialize(rArchive);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::CommandSocketInfo::CommandSocketInfo()
//		Purpose: Constructor
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
BackupDaemon::CommandSocketInfo::CommandSocketInfo()
	: mpGetLine(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::CommandSocketInfo::~CommandSocketInfo()
//		Purpose: Destructor
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
BackupDaemon::CommandSocketInfo::~CommandSocketInfo()
{
	if(mpGetLine)
	{
		delete mpGetLine;
		mpGetLine = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::SerializeStoreObjectInfo(
//			 box_time_t theLastSyncTime,
//			 box_time_t theNextSyncTime)
//		Purpose: Serializes remote directory and file information
//			 into a stream of bytes, using an Archive
//			 abstraction.
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------

static const int STOREOBJECTINFO_MAGIC_ID_VALUE = 0x7777525F;
static const std::string STOREOBJECTINFO_MAGIC_ID_STRING = "BBACKUPD-STATE";
static const int STOREOBJECTINFO_VERSION = 2;

bool BackupDaemon::SerializeStoreObjectInfo(box_time_t theLastSyncTime,
	box_time_t theNextSyncTime) const
{
	if(!GetConfiguration().KeyExists("StoreObjectInfoFile"))
	{
		return false;
	}

	std::string StoreObjectInfoFile = 
		GetConfiguration().GetKeyValue("StoreObjectInfoFile");

	if(StoreObjectInfoFile.size() <= 0)
	{
		return false;
	}

	bool created = false;

	try
	{
		FileStream aFile(StoreObjectInfoFile.c_str(), 
			O_WRONLY | O_CREAT | O_TRUNC);
		created = true;

		Archive anArchive(aFile, 0);

		anArchive.Write(STOREOBJECTINFO_MAGIC_ID_VALUE);
		anArchive.Write(STOREOBJECTINFO_MAGIC_ID_STRING); 
		anArchive.Write(STOREOBJECTINFO_VERSION);
		anArchive.Write(GetLoadedConfigModifiedTime());
		anArchive.Write(mClientStoreMarker);
		anArchive.Write(theLastSyncTime);
		anArchive.Write(theNextSyncTime);

		//
		//
		//
		int64_t iCount = mLocations.size();
		anArchive.Write(iCount);

		for(int v = 0; v < iCount; v++)
		{
			ASSERT(mLocations[v]);
			mLocations[v]->Serialize(anArchive);
		}

		//
		//
		//
		iCount = mIDMapMounts.size();
		anArchive.Write(iCount);

		for(int v = 0; v < iCount; v++)
			anArchive.Write(mIDMapMounts[v]);

		//
		//
		//
		iCount = mUnusedRootDirEntries.size();
		anArchive.Write(iCount);

		for(int v = 0; v < iCount; v++)
		{
			anArchive.Write(mUnusedRootDirEntries[v].first);
			anArchive.Write(mUnusedRootDirEntries[v].second);
		}

		if (iCount > 0)
		{
			anArchive.Write(mDeleteUnusedRootDirEntriesAfter);
		}

		//
		//
		//
		aFile.Close();
		BOX_INFO("Saved store object info file version " <<
			STOREOBJECTINFO_VERSION << " (" <<
			StoreObjectInfoFile << ")");
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to write StoreObjectInfoFile: " <<
			StoreObjectInfoFile << ": " << e.what());
	}
	catch(...)
	{
		BOX_ERROR("Failed to write StoreObjectInfoFile: " <<
			StoreObjectInfoFile << ": unknown error");
	}

	return created;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DeserializeStoreObjectInfo(
//			 box_time_t & theLastSyncTime,
//			 box_time_t & theNextSyncTime)
//		Purpose: Deserializes remote directory and file information
//			 from a stream of bytes, using an Archive
//			 abstraction.
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
bool BackupDaemon::DeserializeStoreObjectInfo(box_time_t & theLastSyncTime,
	box_time_t & theNextSyncTime)
{
	//
	//
	//
	DeleteAllLocations();

	//
	//
	//
	if(!GetConfiguration().KeyExists("StoreObjectInfoFile"))
	{
		return false;
	}

	std::string StoreObjectInfoFile = 
		GetConfiguration().GetKeyValue("StoreObjectInfoFile");

	if(StoreObjectInfoFile.size() <= 0)
	{
		return false;
	}

	try
	{
		FileStream aFile(StoreObjectInfoFile.c_str(), O_RDONLY);
		Archive anArchive(aFile, 0);

		//
		// see if the content looks like a valid serialised archive
		//
		int iMagicValue = 0;
		anArchive.Read(iMagicValue);

		if(iMagicValue != STOREOBJECTINFO_MAGIC_ID_VALUE)
		{
			BOX_WARNING("Store object info file "
				"is not a valid or compatible serialised "
				"archive. Will re-cache from store. "
				"(" << StoreObjectInfoFile << ")");
			return false;
		}

		//
		// get a bit optimistic and read in a string identifier
		//
		std::string strMagicValue;
		anArchive.Read(strMagicValue);

		if(strMagicValue != STOREOBJECTINFO_MAGIC_ID_STRING)
		{
			BOX_WARNING("Store object info file "
				"is not a valid or compatible serialised "
				"archive. Will re-cache from store. "
				"(" << StoreObjectInfoFile << ")");
			return false;
		}

		//
		// check if we are loading some future format
		// version by mistake
		//
		int iVersion = 0;
		anArchive.Read(iVersion);

		if(iVersion != STOREOBJECTINFO_VERSION)
		{
			BOX_WARNING("Store object info file "
				"version " << iVersion << " unsupported. "
				"Will re-cache from store. "
				"(" << StoreObjectInfoFile << ")");
			return false;
		}

		//
		// check if this state file is even valid 
		// for the loaded bbackupd.conf file
		//
		box_time_t lastKnownConfigModTime;
		anArchive.Read(lastKnownConfigModTime);

		if(lastKnownConfigModTime != GetLoadedConfigModifiedTime())
		{
			BOX_WARNING("Store object info file "
				"out of date. Will re-cache from store. "
				"(" << StoreObjectInfoFile << ")");
			return false;
		}

		//
		// this is it, go at it
		//
		anArchive.Read(mClientStoreMarker);
		anArchive.Read(theLastSyncTime);
		anArchive.Read(theNextSyncTime);

		//
		//
		//
		int64_t iCount = 0;
		anArchive.Read(iCount);

		for(int v = 0; v < iCount; v++)
		{
			Location* pLocation = new Location;
			if(!pLocation)
			{
				throw std::bad_alloc();
			}

			pLocation->Deserialize(anArchive);
			mLocations.push_back(pLocation);
		}

		//
		//
		//
		iCount = 0;
		anArchive.Read(iCount);

		for(int v = 0; v < iCount; v++)
		{
			std::string strItem;
			anArchive.Read(strItem);

			mIDMapMounts.push_back(strItem);
		}

		//
		//
		//
		iCount = 0;
		anArchive.Read(iCount);

		for(int v = 0; v < iCount; v++)
		{
			int64_t anId;
			anArchive.Read(anId);

			std::string aName;
			anArchive.Read(aName);

			mUnusedRootDirEntries.push_back(std::pair<int64_t, std::string>(anId, aName));
		}

		if (iCount > 0)
			anArchive.Read(mDeleteUnusedRootDirEntriesAfter);

		//
		//
		//
		aFile.Close();
		BOX_INFO("Loaded store object info file version " << iVersion
			<< " (" << StoreObjectInfoFile << ")");
		
		return true;
	} 
	catch(std::exception &e)
	{
		BOX_ERROR("Internal error reading store object info file: "
			<< StoreObjectInfoFile << ": " << e.what());
	}
	catch(...)
	{
		BOX_ERROR("Internal error reading store object info file: "
			<< StoreObjectInfoFile << ": unknown error");
	}

	DeleteAllLocations();

	mClientStoreMarker = BackupClientContext::ClientStoreMarker_NotKnown;
	theLastSyncTime = 0;
	theNextSyncTime = 0;

	BOX_WARNING("Store object info file is missing, not accessible, "
		"or inconsistent. Will re-cache from store. "
		"(" << StoreObjectInfoFile << ")");
	
	return false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DeleteStoreObjectInfo()
//		Purpose: Deletes the serialised state file, to prevent us
//			 from using it again if a backup is interrupted.
//
//		Created: 2006/02/12
//
// --------------------------------------------------------------------------

bool BackupDaemon::DeleteStoreObjectInfo() const
{
	if(!GetConfiguration().KeyExists("StoreObjectInfoFile"))
	{
		return false;
	}

	std::string storeObjectInfoFile(GetConfiguration().GetKeyValue("StoreObjectInfoFile"));

	// Check to see if the file exists
	if(!FileExists(storeObjectInfoFile.c_str()))
	{
		// File doesn't exist -- so can't be deleted. But something
		// isn't quite right, so log a message
		BOX_WARNING("StoreObjectInfoFile did not exist when it "
			"was supposed to: " << storeObjectInfoFile);

		// Return true to stop things going around in a loop
		return true;
	}

	// Actually delete it
	if(::unlink(storeObjectInfoFile.c_str()) != 0)
	{
		BOX_LOG_SYS_ERROR("Failed to delete the old "
			"StoreObjectInfoFile: " << storeObjectInfoFile);
		return false;
	}

	return true;
}
