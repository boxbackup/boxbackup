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
#include <set>
#include <sstream>

#include "Configuration.h"
#include "IOStream.h"
#include "MemBlockStream.h"
#include "CommonException.h"
#include "BoxPortsAndFiles.h"

#include "SSLLib.h"

#include "autogen_BackupProtocol.h"
#include "autogen_ClientException.h"
#include "autogen_CommonException.h"
#include "autogen_ConversionException.h"
#include "Archive.h"
#include "BackupClientContext.h"
#include "BackupClientCryptoKeys.h"
#include "BackupClientDirectoryRecord.h"
#include "BackupClientFileAttributes.h"
#include "BackupClientInodeToIDMap.h"
#include "BackupClientMakeExcludeList.h"
#include "BackupConstants.h"
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

#	ifdef ENABLE_VSS
#		include <comdef.h>
#		include <Vss.h>
#		include <VsWriter.h>
#		include <VsBackup.h>
		
		// http://www.flounder.com/cstring.htm
		std::string GetMsgForHresult(HRESULT hr)
		{
			std::ostringstream buf;

			if(hr == VSS_S_ASYNC_CANCELLED)
			{
				buf << "VSS async operation cancelled";
			}
			else if(hr == VSS_S_ASYNC_FINISHED)
			{
				buf << "VSS async operation finished";
			}
			else if(hr == VSS_S_ASYNC_PENDING)
			{
				buf << "VSS async operation pending";
			}
			else
			{
				buf << _com_error(hr).ErrorMessage();
			}
			
			buf << " (" << BOX_FORMAT_HEX32(hr) << ")";
			return buf.str();
		}

		std::string WideStringToString(WCHAR *buf)
		{
			if (buf == NULL)
			{
				return "(null)";
			}

			char* pStr = ConvertFromWideString(buf, CP_UTF8);
			
			if(pStr == NULL)
			{
				return "(conversion failed)";
			}
			
			std::string result(pStr);
			free(pStr);
			return result;
		}

		std::string GuidToString(GUID guid)
		{
			wchar_t buf[64];
			StringFromGUID2(guid, buf, sizeof(buf));
			return WideStringToString(buf);
		}

		std::string BstrToString(const BSTR arg)
		{
			if(arg == NULL)
			{
				return std::string("(null)");
			}
			else
			{
				// Extract the *long* before where the arg points to
				long len = ((long *)arg)[-1] / 2;
				std::wstring wstr((WCHAR *)arg, len);
				std::string str;
				if(!ConvertFromWideString(wstr, &str, CP_UTF8))
				{
					throw std::exception("string conversion failed");
				}
				return str;
			}
		}
#	endif

	// Mutex support by Achim: see https://www.boxbackup.org/ticket/67

	// Creates the two mutexes checked for by the installer/uninstaller to
	// see if the program is still running.  One of the mutexes is created
	// in the global name space (which makes it possible to access the
	// mutex across user sessions in Windows XP); the other is created in
	// the session name space (because versions of Windows NT prior to
	// 4.0 TSE don't have a global name space and don't support the
	// 'Global\' prefix).

	void CreateMutexes(const std::string& rName)
	{
		SECURITY_DESCRIPTOR SecurityDesc;
		SECURITY_ATTRIBUTES SecurityAttr;

		/* By default on Windows NT, created mutexes are accessible only by the user
		   running the process. We need our mutexes to be accessible to all users, so
		   that the mutex detection can work across user sessions in Windows XP. To
		   do this we use a security descriptor with a null DACL.
		*/

		InitializeSecurityDescriptor(&SecurityDesc, SECURITY_DESCRIPTOR_REVISION);
		SetSecurityDescriptorDacl(&SecurityDesc, TRUE, NULL, FALSE);
		SecurityAttr.nLength = sizeof(SecurityAttr);
		SecurityAttr.lpSecurityDescriptor = &SecurityDesc;
		SecurityAttr.bInheritHandle = FALSE;
		// We don't care if this succeeds or fails. It's only used to
		// ensure that an installer can detect if Box Backup is running.
		CreateMutexA(&SecurityAttr, FALSE, rName.c_str());
		std::string global_name = "Global\\" + rName;
		CreateMutexA(&SecurityAttr, FALSE, global_name.c_str());
	}
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
	  mNumFilesUploaded(-1),
	  mNumDirsCreated(-1),
	  mMaxBandwidthFromSyncAllowScript(0),
	  mLogAllFileAccess(false),
	  mpProgressNotifier(this),
	  mpLocationResolver(this),
	  mpRunStatusProvider(this),
	  mpSysadminNotifier(this),
	  mapCommandSocketPollTimer(NULL)
	#ifdef WIN32
	, mInstallService(false),
	  mRemoveService(false),
	  mRunAsService(false),
	  mServiceName("bbackupd")
	#endif
#ifdef ENABLE_VSS
	, mpVssBackupComponents(NULL)
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
	const Configuration& config(GetConfiguration());

	// These keys may or may not be required, depending on the configured
	// store type (e.g. not when using Amazon S3 stores), so they can't be
	// verified by BackupDaemonConfigVerify.
	std::vector<std::string> requiredKeys;
	requiredKeys.push_back("StoreHostname");
	requiredKeys.push_back("AccountNumber");
	requiredKeys.push_back("CertificateFile");
	requiredKeys.push_back("PrivateKeyFile");
	requiredKeys.push_back("TrustedCAsFile");
	bool missingRequiredKeys = false;

	for(std::vector<std::string>::const_iterator i = requiredKeys.begin();
		i != requiredKeys.end(); i++)
	{
		if(!config.KeyExists(*i))
		{
			BOX_ERROR("Missing required configuration key: " << *i);
			missingRequiredKeys = true;
		}
	}

	if(missingRequiredKeys)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, InvalidConfiguration,
			"Some required configuration keys are missing in " <<
			GetConfigFileName());
	}

#ifdef PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
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
#endif
}


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
	for(Locations::iterator i = mLocations.begin();
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

#ifdef ENABLE_VSS
	HRESULT result = CoInitialize(NULL);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to initialize COM: " << 
			GetMsgForHresult(result));
		return 1;
	}
#endif

	CreateMutexes("__boxbackup_mutex__");

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
#endif // WIN32

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
	
	mapCommandSocketPollTimer.reset(new Timer(COMMAND_SOCKET_POLL_INTERVAL,
		"CommandSocketPollTimer"));

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
			EMU_UNLINK(socketName);
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
		try 
		{
			mapCommandSocketInfo.reset();
		}
		catch(std::exception &e)
		{
			BOX_WARNING("Internal error while closing command "
				"socket after another exception, ignored: " <<
				e.what());
		}
		catch(...)
		{
			BOX_WARNING("Error closing command socket after "
				"exception, ignored.");
		}

		mapCommandSocketPollTimer.reset();
		Timers::Cleanup();
		
		throw;
	}

	// Clean up
	mapCommandSocketInfo.reset();
	mapCommandSocketPollTimer.reset();
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
	BackupClientCryptoKeys_Setup(conf.GetKeyValue("KeysFile"));
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
	mBackupErrorDelay = conf.GetKeyValueInt("BackupErrorDelay");

	// But are we connecting automatically?
	bool automaticBackup = conf.GetKeyValueBool("AutomaticBackup");
	
	// When the next sync should take place -- which is ASAP
	mNextSyncTime = 0;

	// When the last sync started (only updated if the store was not full when the sync ended)
	mLastSyncTime = 0;

	// --------------------------------------------------------------------------------------------
 
	mDeleteStoreObjectInfoFile = DeserializeStoreObjectInfo(mLastSyncTime,
		mNextSyncTime);
 
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

		// Check whether we should be stopping, and if so,
		// don't hang around waiting on the command socket.
		if(StopRun())
		{
			BOX_INFO("Skipping command socket polling "
				"due to shutdown request");
			break;
		}

		// Is a delay necessary?
		box_time_t currentTime = GetCurrentBoxTime();
		box_time_t requiredDelay = (mNextSyncTime < currentTime)
				? (0) : (mNextSyncTime - currentTime);
		mNextSyncTime = currentTime + requiredDelay;

		if (mDoSyncForcedByPreviousSyncError)
		{
			BOX_INFO("Last backup was not successful, "
				"next one starting at " <<
				FormatTime(mNextSyncTime, false, true));
		}
		else if (automaticBackup)
		{
			BOX_INFO("Automatic backups are enabled, "
				"next one starting at " <<
				FormatTime(mNextSyncTime, false, true));
		}
		else
		{
			BOX_INFO("No automatic backups, waiting for "
				"bbackupctl snapshot command");
			requiredDelay = SecondsToBoxTime(MAX_SLEEP_TIME);
		}

		if(requiredDelay > SecondsToBoxTime(MAX_SLEEP_TIME))
		{
			requiredDelay = SecondsToBoxTime(MAX_SLEEP_TIME);
		}

		// Only delay if necessary
		if(requiredDelay == 0)
		{
			// No sleep necessary, so don't listen on the command
			// socket at all right now.
		}
		else if(mapCommandSocketInfo.get() != 0)
		{
			// A command socket exists, so sleep by waiting for a
			// connection or command on it.
			WaitOnCommandSocket(requiredDelay, doSync,
				mDoSyncForcedByCommand);
		}
		else
		{
			// No command socket or connection, just do a normal
			// sleep.
			time_t sleepSeconds = 
				BoxTimeToSeconds(requiredDelay);
			::sleep((sleepSeconds <= 0)
				? 1 : sleepSeconds);
		}

		// We have now slept, so if automaticBackup is enabled then
		// it's time for a backup now.

		if(StopRun())
		{
			BOX_INFO("Stopping idle loop due to shutdown request");
			break;
		}
		else if(doSync)
		{
			BOX_INFO("Starting a backup immediately due to "
				"bbackupctl sync command");
		}
		else if(GetCurrentBoxTime() < mNextSyncTime)
		{
			BOX_TRACE("Deadline not reached, sleeping again");
			continue;
		}
		else if(mDoSyncForcedByPreviousSyncError)
		{
			BOX_INFO("Last backup was not successful, next one "
				"starting now");
		}
		else if(!automaticBackup)
		{
			BOX_TRACE("Sleeping again because automatic backups "
				"are not enabled");
			continue;
		}
		else
		{
			BOX_INFO("Automatic backups are enabled, next one "
				"starting now");
		}

		// If we pass this point, or exit the loop, we should have
		// logged something at INFO level or higher to explain why.

		// Use a script to see if sync is allowed now?
		if(mDoSyncForcedByCommand)
		{
			BOX_INFO("Skipping SyncAllowScript due to bbackupctl "
				"force-sync command");
		}
		else
		{
			int d = UseScriptToSeeIfSyncAllowed();
			if(d > 0)
			{
				// Script has asked for a delay
				mNextSyncTime = GetCurrentBoxTime() +
					SecondsToBoxTime(d);
				BOX_INFO("Impending backup stopped by "
					"SyncAllowScript, next attempt "
					"scheduled for " <<
					FormatTime(mNextSyncTime, false));
				continue;
			}
		}

		mCurrentSyncStartTime = GetCurrentBoxTime();
		RunSyncNowWithExceptionHandling();
		
		// Set state
		SetState(storageLimitExceeded?State_StorageLimitExceeded:State_Idle);
	}
	while(!StopRun());
	
	// Make sure we have a clean start next time round (if restart)
	DeleteAllLocations();
	DeleteAllIDMaps();
}

std::auto_ptr<BackupClientContext> BackupDaemon::RunSyncNowWithExceptionHandling()
{
	bool errorOccurred = false;
	int errorCode = 0, errorSubCode = 0;
	std::string errorString = "unknown";

	try
	{
		OnBackupStart();
		// Do sync
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

	// Is it a berkely db failure?
	bool isBerkelyDbFailure = false;

	// Notify system administrator about the final state of the backup
	if(errorOccurred)
	{
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

		ResetCachedState();

		// Handle restart?
		if(StopRun())
		{
			BOX_NOTICE("Exception (" << errorCode << "/" <<
				errorSubCode << ") due to signal");
			OnBackupFinish();
			return mapClientContext; // releases mapClientContext
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
			mNextSyncTime = GetCurrentBoxTime() +
				SecondsToBoxTime(mBackupErrorDelay) +
				Random::RandomInt(mUpdateStoreInterval >>
					SYNC_PERIOD_RANDOM_EXTRA_TIME_SHIFT_BY);
		}
	}

	if(mReadErrorsOnFilesystemObjects)
	{
		NotifySysadmin(SysadminNotifier::ReadError);
	}

	if(mStorageLimitExceeded)
	{
		NotifySysadmin(SysadminNotifier::StoreFull);
	}

	if (!errorOccurred && !mReadErrorsOnFilesystemObjects &&
		!mStorageLimitExceeded)
	{
		NotifySysadmin(SysadminNotifier::BackupOK);
	}
	
	// If we were retrying after an error, and this backup succeeded,
	// then now would be a good time to stop :-)
	mDoSyncForcedByPreviousSyncError = errorOccurred && !isBerkelyDbFailure;

	OnBackupFinish();
	return mapClientContext; // releases mapClientContext
}

void BackupDaemon::ResetCachedState()
{
	// Clear state data
	// Go back to beginning of time
	mLastSyncTime = 0;
	mClientStoreMarker = BackupClientContext::ClientStoreMarker_NotKnown;	// no store marker, so download everything
	DeleteAllLocations();
	DeleteAllIDMaps();
}

std::auto_ptr<BackupClientContext> BackupDaemon::GetNewContext
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
)
{
	std::auto_ptr<BackupClientContext> context(new BackupClientContext(
		rResolver, rTLSContext, rHostname, Port, AccountNumber,
		ExtendedLogging, ExtendedLogToFile, ExtendedLogFile,
		rProgressNotifier, TcpNiceMode));
	return context;
}

// Returns the BackupClientContext so that tests can use it to hold the
// connection open and prevent housekeeping from running. Otherwise don't use
// it, let it be destroyed and close the connection.
std::auto_ptr<BackupClientContext> BackupDaemon::RunSyncNow()
{
	Timers::AssertInitialised();

	// Delete the serialised store object file,
	// so that we don't try to reload it after a
	// partially completed backup
	if(mDeleteStoreObjectInfoFile && !DeleteStoreObjectInfo())
	{
		BOX_ERROR("Failed to delete the StoreObjectInfoFile, "
			"backup cannot continue safely.");
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
		bool overwrite = false;
		if (conf.KeyExists("LogFileOverwrite"))
		{
			overwrite = conf.GetKeyValueBool("LogFileOverwrite");
		}

		Log::Level level = Log::INFO;
		if (conf.KeyExists("LogFileLevel"))
		{
			level = Logging::GetNamedLevel(
				conf.GetKeyValue("LogFileLevel"));
		}

		fileLogger.reset(new FileLogger(conf.GetKeyValue("LogFile"),
			level, !overwrite));
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
	mapClientContext = GetNewContext(
		*mpLocationResolver,
		mTlsContext,
		conf.GetKeyValue("StoreHostname"),
		conf.GetKeyValueInt("StorePort"),
		conf.GetKeyValueUint32("AccountNumber"),
		conf.GetKeyValueBool("ExtendedLogging"),
		conf.KeyExists("ExtendedLogFile"),
		extendedLogFile,
		*mpProgressNotifier,
		conf.GetKeyValueBool("TcpNice")
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
		// This can happen if we receive a force-sync command less
		// than minimumFileAge after the last sync. Deal with it by
		// moving back syncPeriodStart, which should not do any
		// damage.
		syncPeriodStart = syncPeriodEnd - SecondsToBoxTime(1);
	}

	if(syncPeriodStart >= syncPeriodEnd)
	{
		BOX_ERROR("Invalid (negative) sync period: perhaps your clock "
			"is going backwards? (" << syncPeriodStart << " to " <<
			syncPeriodEnd << ")");
		THROW_EXCEPTION(ClientException, ClockWentBackwards);
	}

	// Check logic
	ASSERT(syncPeriodEnd > syncPeriodStart);
	// Paranoid check on sync times
	if(syncPeriodStart >= syncPeriodEnd)
	{
		return mapClientContext; // releases mapClientContext
	}
	
	// Adjust syncPeriodEnd to emulate snapshot behaviour properly
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
		*mpSysadminNotifier, *mpProgressNotifier, *mapClientContext, this);
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
	mNumFilesUploaded = 0;
	mNumDirsCreated = 0;

	if(conf.KeyExists("MaxUploadRate"))
	{
		params.mMaxUploadRate = conf.GetKeyValueInt("MaxUploadRate");
	}

	if(mMaxBandwidthFromSyncAllowScript != 0)
	{
		params.mMaxUploadRate = mMaxBandwidthFromSyncAllowScript;
	}

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

	mapClientContext->SetMaximumDiffingTime(maximumDiffingTime);
	mapClientContext->SetKeepAliveTime(keepAliveTime);

	// Set store marker
	mapClientContext->SetClientStoreMarker(mClientStoreMarker);

	// Set up the locations, if necessary -- need to do it here so we have
	// a (potential) connection to use.
	{
		const Configuration &locations(
			conf.GetSubConfiguration(
				"BackupLocations"));

		// Make sure all the directory records
		// are set up
		SetupLocations(*mapClientContext, locations);
	}

	mpProgressNotifier->NotifyIDMapsSetup(*mapClientContext);

	// Get some ID maps going
	SetupIDMapsForSync();

	// Delete any unused directories?
	DeleteUnusedRootDirEntries(*mapClientContext);

#ifdef ENABLE_VSS
	CreateVssBackupComponents();
#endif

	// Go through the records, syncing them
	for(Locations::const_iterator 
		i(mLocations.begin()); 
		i != mLocations.end(); ++i)
	{
		// Set current and new ID map pointers
		// in the context
		mapClientContext->SetIDMaps(mCurrentIDMaps[(*i)->mIDMapIndex],
			mNewIDMaps[(*i)->mIDMapIndex]);
	
		// Set exclude lists (context doesn't
		// take ownership)
		mapClientContext->SetExcludeLists(
			(*i)->mapExcludeFiles.get(),
			(*i)->mapExcludeDirs.get());

		// Sync the directory
		std::string locationPath = (*i)->mPath;
#ifdef ENABLE_VSS
		if((*i)->mIsSnapshotCreated)
		{
			locationPath = (*i)->mSnapshotPath;
		}
#endif

		(*i)->mapDirectoryRecord->SyncDirectory(params,
			BackupProtocolListDirectory::RootDirectory,
			locationPath, std::string("/") + (*i)->mName, **i);

		// Unset exclude lists (just in case)
		mapClientContext->SetExcludeLists(0, 0);
	}

	// Perform any deletions required -- these are
	// delayed until the end to allow renaming to 
	// happen neatly.
	mapClientContext->PerformDeletions();

#ifdef ENABLE_VSS
	CleanupVssBackupComponents();
#endif

	// Get the new store marker
	mClientStoreMarker = mapClientContext->GetClientStoreMarker();
	mStorageLimitExceeded = mapClientContext->StorageLimitExceeded();
	mReadErrorsOnFilesystemObjects |=
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
		SerializeStoreObjectInfo(mLastSyncTime, mNextSyncTime);

	// --------------------------------------------------------------------------------------------

	return mapClientContext; // releases mapClientContext
}

#ifdef ENABLE_VSS
bool BackupDaemon::WaitForAsync(IVssAsync *pAsync,
	const std::string& description)
{
	BOX_INFO("VSS: waiting for " << description << " to complete");
	HRESULT result;

	do
	{
		result = pAsync->Wait(1000);
		if(result != S_OK)
		{
			BOX_ERROR("VSS: Failed to wait for " << description <<
				" to complete: " << GetMsgForHresult(result));
			break;
		}

		HRESULT result2;
		result = pAsync->QueryStatus(&result2, NULL);
		if(result != S_OK)
		{
			BOX_ERROR("VSS: Failed to query " << description <<
				" status: " << GetMsgForHresult(result));
			break;
		}

		result = result2;
		BOX_INFO("VSS: " << description << " status: " <<
			GetMsgForHresult(result));
	}
	while(result == VSS_S_ASYNC_PENDING);

	pAsync->Release();

	return (result == VSS_S_ASYNC_FINISHED);
}

#define CALL_MEMBER_FN(object, method) ((object).*(method))

bool BackupDaemon::CallAndWaitForAsync(AsyncMethod method,
	const std::string& description)
{
	IVssAsync *pAsync;
	HRESULT result = CALL_MEMBER_FN(*mpVssBackupComponents, method)(&pAsync);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: " << description << " failed: " <<
			GetMsgForHresult(result));
		return false;
	}

	return WaitForAsync(pAsync, description);
}

void BackupDaemon::CreateVssBackupComponents()
{
	std::map<char, VSS_ID> volumesIncluded;

	HRESULT result = ::CreateVssBackupComponents(&mpVssBackupComponents);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to create backup components: " << 
			GetMsgForHresult(result));
		return;
	}

	result = mpVssBackupComponents->InitializeForBackup(NULL);
	if(result != S_OK)
	{
		std::string message = GetMsgForHresult(result);

		if (result == VSS_E_UNEXPECTED)
		{
			message = "Check the Application Log for details, and ensure "
				"that the Volume Shadow Copy, COM+ System Application, "
				"and Distributed Transaction Coordinator services "
				"are running";
		}

		BOX_ERROR("VSS: Failed to initialize for backup: " << message);
		return;
	}

	result = mpVssBackupComponents->SetContext(VSS_CTX_BACKUP);
	if(result == E_NOTIMPL)
	{
		BOX_INFO("VSS: Failed to set context to VSS_CTX_BACKUP: "
			"not implemented, probably Windows XP, ignored.");
	}
	else if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to set context to VSS_CTX_BACKUP: " <<
			GetMsgForHresult(result));
		return;
	}

	result = mpVssBackupComponents->SetBackupState(
		false, /* no components for now */
		true, /* might as well ask for a bootable backup */
		VSS_BT_FULL,
		false /* what is Partial File Support? */);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to set backup state: " <<
			GetMsgForHresult(result));
		return;
	}

	if(!CallAndWaitForAsync(&IVssBackupComponents::GatherWriterMetadata,
		"GatherWriterMetadata()"))
	{
		goto CreateVssBackupComponents_cleanup_WriterMetadata;
	}

	UINT writerCount;
	result = mpVssBackupComponents->GetWriterMetadataCount(&writerCount);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to get writer count: " <<
			GetMsgForHresult(result));
		goto CreateVssBackupComponents_cleanup_WriterMetadata;
	}

	for(UINT iWriter = 0; iWriter < writerCount; iWriter++)
	{
		BOX_INFO("VSS: Getting metadata from writer " << iWriter);
		VSS_ID writerInstance;
		IVssExamineWriterMetadata* pMetadata;
		result = mpVssBackupComponents->GetWriterMetadata(iWriter,
			&writerInstance, &pMetadata);
		if(result != S_OK)
		{
			BOX_ERROR("Failed to get VSS metadata from writer " << iWriter <<
				": " << GetMsgForHresult(result));
			continue;
		}

		UINT includeFiles, excludeFiles, numComponents;
		result = pMetadata->GetFileCounts(&includeFiles, &excludeFiles,
			&numComponents);
		if(result != S_OK)
		{
			BOX_ERROR("VSS: Failed to get metadata file counts from "
				"writer " << iWriter <<	": " << 
				GetMsgForHresult(result));
			pMetadata->Release();
			continue;
		}

		for(UINT iComponent = 0; iComponent < numComponents; iComponent++)
		{
			IVssWMComponent* pComponent;
			result = pMetadata->GetComponent(iComponent, &pComponent);
			if(result != S_OK)
			{
				BOX_ERROR("VSS: Failed to get metadata component " <<
					iComponent << " from writer " << iWriter << ": " << 
					GetMsgForHresult(result));
				continue;
			}

			PVSSCOMPONENTINFO pComponentInfo;
			result = pComponent->GetComponentInfo(&pComponentInfo);
			if(result != S_OK)
			{
				BOX_ERROR("VSS: Failed to get metadata component " <<
					iComponent << " info from writer " << iWriter << ": " << 
					GetMsgForHresult(result));
				pComponent->Release();
				continue;
			}

			BOX_TRACE("VSS: writer " << iWriter << " component " << 
				iComponent << " info:");
			switch(pComponentInfo->type)
			{
			case VSS_CT_UNDEFINED: BOX_TRACE("VSS: type: undefined"); break;
			case VSS_CT_DATABASE:  BOX_TRACE("VSS: type: database"); break;
			case VSS_CT_FILEGROUP: BOX_TRACE("VSS: type: filegroup"); break;
			default:
				BOX_WARNING("VSS: type: unknown (" << pComponentInfo->type << ")");
			}

			BOX_TRACE("VSS: logical path: " << 
				BstrToString(pComponentInfo->bstrLogicalPath));
			BOX_TRACE("VSS: component name: " << 
				BstrToString(pComponentInfo->bstrComponentName));
			BOX_TRACE("VSS: caption: " << 
				BstrToString(pComponentInfo->bstrCaption));
			BOX_TRACE("VSS: restore metadata: " << 
				pComponentInfo->bRestoreMetadata);
			BOX_TRACE("VSS: notify on complete: " << 
				pComponentInfo->bRestoreMetadata);
			BOX_TRACE("VSS: selectable: " << 
				pComponentInfo->bSelectable);
			BOX_TRACE("VSS: selectable for restore: " << 
				pComponentInfo->bSelectableForRestore);
			BOX_TRACE("VSS: component flags: " << 
				BOX_FORMAT_HEX32(pComponentInfo->dwComponentFlags));
			BOX_TRACE("VSS: file count: " << 
				pComponentInfo->cFileCount);
			BOX_TRACE("VSS: databases: " << 
				pComponentInfo->cDatabases);
			BOX_TRACE("VSS: log files: " << 
				pComponentInfo->cLogFiles);
			BOX_TRACE("VSS: dependencies: " << 
				pComponentInfo->cDependencies);

			pComponent->FreeComponentInfo(pComponentInfo);
			pComponent->Release();
		}

		pMetadata->Release();
	}

	VSS_ID snapshotSetId;
	result = mpVssBackupComponents->StartSnapshotSet(&snapshotSetId);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to start snapshot set: " <<
			GetMsgForHresult(result));
		goto CreateVssBackupComponents_cleanup_WriterMetadata;
	}

	// Add all volumes included as backup locations to the snapshot set
	for(Locations::iterator
		iLocation  = mLocations.begin();
		iLocation != mLocations.end();
		iLocation++)
	{
		Location& rLocation(**iLocation);
		std::string path = rLocation.mPath;
		// convert to absolute and remove Unicode prefix
		path = ConvertPathToAbsoluteUnicode(path.c_str()).substr(4);

		if(path.length() >= 3 && path[1] == ':' && path[2] == '\\')
		{
			std::string volumeRoot = path.substr(0, 3);

			std::map<char, VSS_ID>::iterator i = 
				volumesIncluded.find(path[0]);

			if(i == volumesIncluded.end())
			{
				std::wstring volumeRootWide;
				volumeRootWide.push_back((WCHAR) path[0]);
				volumeRootWide.push_back((WCHAR) ':');
				volumeRootWide.push_back((WCHAR) '\\');
				VSS_ID newVolumeId;
				result = mpVssBackupComponents->AddToSnapshotSet(
					(VSS_PWSZ)(volumeRootWide.c_str()), GUID_NULL,
					&newVolumeId);
				if(result == S_OK)
				{
					BOX_TRACE("VSS: Added volume " << volumeRoot <<
						" for backup location " << path <<
						" to snapshot set");
					volumesIncluded[path[0]] = newVolumeId;
					rLocation.mSnapshotVolumeId = newVolumeId;
				}
				else
				{
					BOX_ERROR("VSS: Failed to add volume " <<
						volumeRoot << " to snapshot set: " <<
						GetMsgForHresult(result));
					goto CreateVssBackupComponents_cleanup_WriterMetadata;
				}
			}
			else
			{
				BOX_TRACE("VSS: Skipping already included volume " <<
					volumeRoot << " for backup location " << path);
				rLocation.mSnapshotVolumeId = i->second;
			}

			rLocation.mIsSnapshotCreated = true;

			// If the snapshot path starts with the volume root
			// (drive letter), because the path is absolute (as it
			// should be), then remove it so that the resulting
			// snapshot path can be appended to the snapshot device
			// object to make a real path, without a spurious drive
			// letter in it.

			if (path.substr(0, volumeRoot.length()) == volumeRoot)
			{
				path = path.substr(volumeRoot.length());
			}

			rLocation.mSnapshotPath = path;
		}
		else
		{
			BOX_WARNING("VSS: Skipping backup location " << path <<
				" which does not start with a volume specification");
		}
	}

	if(!CallAndWaitForAsync(&IVssBackupComponents::PrepareForBackup,
		"PrepareForBackup()"))
	{
		goto CreateVssBackupComponents_cleanup_WriterMetadata;
	}

	if(!CallAndWaitForAsync(&IVssBackupComponents::DoSnapshotSet,
		"DoSnapshotSet()"))
	{
		goto CreateVssBackupComponents_cleanup_WriterMetadata;
	}

	if(!CallAndWaitForAsync(&IVssBackupComponents::GatherWriterStatus,
		"GatherWriterStatus()"))
	{
		goto CreateVssBackupComponents_cleanup_WriterStatus;
	}

	result = mpVssBackupComponents->GetWriterStatusCount(&writerCount);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to get writer status count: " << 
			GetMsgForHresult(result));
		goto CreateVssBackupComponents_cleanup_WriterStatus;
	}

	for(UINT iWriter = 0; iWriter < writerCount; iWriter++)
	{
		VSS_ID instance, writer;
		BSTR writerNameBstr;
		VSS_WRITER_STATE writerState;
		HRESULT writerResult;

		result = mpVssBackupComponents->GetWriterStatus(iWriter,
			&instance, &writer, &writerNameBstr, &writerState,
			&writerResult);
		if(result != S_OK)
		{
			BOX_ERROR("VSS: Failed to query writer " << iWriter <<
				" status: " << GetMsgForHresult(result));
			goto CreateVssBackupComponents_cleanup_WriterStatus;
		}

		std::string writerName = BstrToString(writerNameBstr);
		::SysFreeString(writerNameBstr);

		if(writerResult != S_OK)
		{
			BOX_ERROR("VSS: Writer " << iWriter << " (" <<
				writerName << ") failed: " <<
				GetMsgForHresult(writerResult));
			continue;
		}

		std::string stateName;

		switch(writerState)
		{
#define WRITER_STATE(code) \
		case code: stateName = #code; break;
		WRITER_STATE(VSS_WS_UNKNOWN);
		WRITER_STATE(VSS_WS_STABLE);
		WRITER_STATE(VSS_WS_WAITING_FOR_FREEZE);
		WRITER_STATE(VSS_WS_WAITING_FOR_THAW);
		WRITER_STATE(VSS_WS_WAITING_FOR_POST_SNAPSHOT);
		WRITER_STATE(VSS_WS_WAITING_FOR_BACKUP_COMPLETE);
		WRITER_STATE(VSS_WS_FAILED_AT_IDENTIFY);
		WRITER_STATE(VSS_WS_FAILED_AT_PREPARE_BACKUP);
		WRITER_STATE(VSS_WS_FAILED_AT_PREPARE_SNAPSHOT);
		WRITER_STATE(VSS_WS_FAILED_AT_FREEZE);
		WRITER_STATE(VSS_WS_FAILED_AT_THAW);
		WRITER_STATE(VSS_WS_FAILED_AT_POST_SNAPSHOT);
		WRITER_STATE(VSS_WS_FAILED_AT_BACKUP_COMPLETE);
		WRITER_STATE(VSS_WS_FAILED_AT_PRE_RESTORE);
		WRITER_STATE(VSS_WS_FAILED_AT_POST_RESTORE);
		WRITER_STATE(VSS_WS_FAILED_AT_BACKUPSHUTDOWN);
#undef WRITER_STATE
		default:
			std::ostringstream o;
			o << "unknown (" << writerState << ")";
			stateName = o.str();
		}

		BOX_TRACE("VSS: Writer " << iWriter << " (" <<
			writerName << ") is in state " << stateName);
	}

	// lookup new snapshot volume for each location that has a snapshot
	for(Locations::iterator
		iLocation  = mLocations.begin();
		iLocation != mLocations.end();
		iLocation++)
	{
		Location& rLocation(**iLocation);
		if(rLocation.mIsSnapshotCreated)
		{
			VSS_SNAPSHOT_PROP prop;
			result = mpVssBackupComponents->GetSnapshotProperties(
				rLocation.mSnapshotVolumeId, &prop);
			if(result != S_OK)
			{
				BOX_ERROR("VSS: Failed to get snapshot properties "
					"for volume " << GuidToString(rLocation.mSnapshotVolumeId) <<
					" for location " << rLocation.mPath << ": " <<
					GetMsgForHresult(result));
				rLocation.mIsSnapshotCreated = false;
				continue;
			}

			rLocation.mSnapshotPath =
				WideStringToString(prop.m_pwszSnapshotDeviceObject) +
				DIRECTORY_SEPARATOR + rLocation.mSnapshotPath;
			VssFreeSnapshotProperties(&prop);

			BOX_INFO("VSS: Location " << rLocation.mPath << " using "
				"snapshot path " << rLocation.mSnapshotPath);
		}
	}

	IVssEnumObject *pEnum;
	result = mpVssBackupComponents->Query(GUID_NULL, VSS_OBJECT_NONE,
		VSS_OBJECT_SNAPSHOT, &pEnum);
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to query snapshot list: " << 
			GetMsgForHresult(result));
		goto CreateVssBackupComponents_cleanup_WriterStatus;
	}

	while(result == S_OK)
	{
		VSS_OBJECT_PROP rgelt;
		ULONG count;
		result = pEnum->Next(1, &rgelt, &count);

		if(result == S_FALSE)
		{
			// end of list, break out of the loop
			break;
		}
		else if(result != S_OK)
		{
			BOX_ERROR("VSS: Failed to enumerate snapshot: " << 
				GetMsgForHresult(result));
		}
		else if(count != 1)
		{
			BOX_ERROR("VSS: Failed to enumerate snapshot: " <<
				"Next() returned " << count << " objects instead of 1");
		}
		else if(rgelt.Type != VSS_OBJECT_SNAPSHOT)
		{
			BOX_ERROR("VSS: Failed to enumerate snapshot: " <<
				"Next() returned a type " << rgelt.Type << " object "
				"instead of VSS_OBJECT_SNAPSHOT");
		}
		else
		{
			VSS_SNAPSHOT_PROP *pSnap = &rgelt.Obj.Snap;
			BOX_TRACE("VSS: Snapshot ID: " << 
				GuidToString(pSnap->m_SnapshotId));
			BOX_TRACE("VSS: Snapshot set ID: " << 
				GuidToString(pSnap->m_SnapshotSetId));
			BOX_TRACE("VSS: Number of volumes: " << 
				pSnap->m_lSnapshotsCount);
			BOX_TRACE("VSS: Snapshot device object: " << 
				WideStringToString(pSnap->m_pwszSnapshotDeviceObject));
			BOX_TRACE("VSS: Original volume name: " << 
				WideStringToString(pSnap->m_pwszOriginalVolumeName));
			BOX_TRACE("VSS: Originating machine: " << 
				WideStringToString(pSnap->m_pwszOriginatingMachine));
			BOX_TRACE("VSS: Service machine: " << 
				WideStringToString(pSnap->m_pwszServiceMachine));
			BOX_TRACE("VSS: Exposed name: " << 
				WideStringToString(pSnap->m_pwszExposedName));
			BOX_TRACE("VSS: Exposed path: " << 
				WideStringToString(pSnap->m_pwszExposedPath));
			BOX_TRACE("VSS: Provider ID: " << 
				GuidToString(pSnap->m_ProviderId));
			BOX_TRACE("VSS: Snapshot attributes: " << 
				BOX_FORMAT_HEX32(pSnap->m_lSnapshotAttributes));
			BOX_TRACE("VSS: Snapshot creation time: " << 
				BOX_FORMAT_HEX32(pSnap->m_tsCreationTimestamp));

			std::string status;
			switch(pSnap->m_eStatus)
			{
			case VSS_SS_UNKNOWN:                     status = "Unknown (error)"; break;
			case VSS_SS_PREPARING:                   status = "Preparing"; break;
			case VSS_SS_PROCESSING_PREPARE:          status = "Preparing (processing)"; break;
			case VSS_SS_PREPARED:                    status = "Prepared"; break;
			case VSS_SS_PROCESSING_PRECOMMIT:        status = "Precommitting"; break;
			case VSS_SS_PRECOMMITTED:                status = "Precommitted"; break;
			case VSS_SS_PROCESSING_COMMIT:           status = "Commiting"; break;
			case VSS_SS_COMMITTED:                   status = "Committed"; break;
			case VSS_SS_PROCESSING_POSTCOMMIT:       status = "Postcommitting"; break;
			case VSS_SS_PROCESSING_PREFINALCOMMIT:   status = "Pre final committing"; break;
			case VSS_SS_PREFINALCOMMITTED:           status = "Pre final committed"; break;
			case VSS_SS_PROCESSING_POSTFINALCOMMIT:  status = "Post final committing"; break;
			case VSS_SS_CREATED:                     status = "Created"; break;
			case VSS_SS_ABORTED:                     status = "Aborted"; break;
			case VSS_SS_DELETED:                     status = "Deleted"; break;
			case VSS_SS_POSTCOMMITTED:               status = "Postcommitted"; break;
			default:
				std::ostringstream buf;
				buf << "Unknown code: " << pSnap->m_eStatus;
				status = buf.str();
			}

			BOX_TRACE("VSS: Snapshot status: " << status);
			VssFreeSnapshotProperties(pSnap);
		}
	}

	pEnum->Release();

CreateVssBackupComponents_cleanup_WriterStatus:
	result = mpVssBackupComponents->FreeWriterStatus();
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to free writer status: " <<
			GetMsgForHresult(result));
	}

CreateVssBackupComponents_cleanup_WriterMetadata:
	result = mpVssBackupComponents->FreeWriterMetadata();
	if(result != S_OK)
	{
		BOX_ERROR("VSS: Failed to free writer metadata: " <<
			GetMsgForHresult(result));
	}
}

void BackupDaemon::CleanupVssBackupComponents()
{
	if(mpVssBackupComponents == NULL)
	{
		return;
	}

	CallAndWaitForAsync(&IVssBackupComponents::BackupComplete,
		"BackupComplete()");

	mpVssBackupComponents->Release();
	mpVssBackupComponents = NULL;
}
#endif

void BackupDaemon::OnBackupStart()
{
	ResetLogFile();

	// Touch a file to record times in filesystem
	TouchFileInWorkingDir("last_sync_start");

	// Reset statistics on uploads
	BackupStoreFile::ResetStats();
	
	// Tell anything connected to the command socket
	SendSyncStartOrFinish(true /* start */);
	
	// Notify administrator
	NotifySysadmin(SysadminNotifier::BackupStart);

	// Setup timer for polling the command socket
	mapCommandSocketPollTimer.reset(new Timer(COMMAND_SOCKET_POLL_INTERVAL,
		"CommandSocketPollTimer"));

	// Set state and log start
	SetState(State_Connected);
	BOX_NOTICE("Beginning scan of local files");
}

void BackupDaemon::OnBackupFinish()
{
	try
	{
		// Log
		BOX_NOTICE("Finished scan of local files");

		// Log the stats
		BOX_NOTICE("File statistics: total file size uploaded "
			<< BackupStoreFile::msStats.mBytesInEncodedFiles
			<< ", bytes already on server "
			<< BackupStoreFile::msStats.mBytesAlreadyOnServer
			<< ", encoded size "
			<< BackupStoreFile::msStats.mTotalFileStreamSize
			<< ", " << mNumFilesUploaded << " files uploaded, "
			<< mNumDirsCreated << " dirs created");

		// Reset statistics again
		BackupStoreFile::ResetStats();

		// Notify administrator
		NotifySysadmin(SysadminNotifier::BackupFinish);

		// Stop the timer for polling the command socket,
		// to prevent needless alarms while sleeping.
		mapCommandSocketPollTimer.reset();

		// Tell anything connected to the command socket
		SendSyncStartOrFinish(false /* finish */);

		// Touch a file to record times in filesystem
		TouchFileInWorkingDir("last_sync_finish");
	}
	catch (std::exception &e)
	{
		BOX_ERROR("Failed to perform backup finish actions: " << e.what());
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::UseScriptToSeeIfSyncAllowed()
//		Purpose: Private. Use a script to see if the sync should be
//			 allowed now (if configured). Returns -1 if it's
//			 allowed, time in seconds to wait otherwise.
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

	std::string script(conf.GetKeyValue("SyncAllowScript") + 
		" \"" + GetConfigFileName() + "\"");

	// Run it?
	pid_t pid = 0;
	try
	{
		std::auto_ptr<IOStream> pscript(LocalProcessStream(script,
			pid));

		// Read in the result
		IOStreamGetLine getLine(*pscript);
		std::string line;
		if(getLine.GetLine(line, true, 30000)) // 30 seconds should be enough
		{
			waitInSeconds = BackupDaemon::ParseSyncAllowScriptOutput(script, line);
		}
		else
		{
			BOX_ERROR("SyncAllowScript output nothing within "
				"30 seconds, waiting 5 minutes to try again"
				" (" << script << ")");
		}
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Internal error running SyncAllowScript: "
			<< e.what() << " (" << script << ")");
	}
	catch(...)
	{
		// Ignore any exceptions
		// Log that something bad happened
		BOX_ERROR("Unknown error running SyncAllowScript (" <<
			script << ")");
	}

	// Wait and then cleanup child process, if any
	if(pid != 0)
	{
		int status = 0;
		::waitpid(pid, &status, 0);
	}

	return waitInSeconds;
}

int BackupDaemon::ParseSyncAllowScriptOutput(const std::string& script,
	const std::string& output)
{
	int waitInSeconds = (60*5);
	std::istringstream iss(output);

	std::string delay;
	iss >> delay;

	if(delay == "")
	{
		BOX_ERROR("SyncAllowScript output an empty line, sleeping for "
			<< waitInSeconds << " seconds (" << script << ")");
		return waitInSeconds;
	}

	// Got a string, interpret
	if(delay == "now")
	{
		// Script says do it now. Obey.
		waitInSeconds = -1;

		BOX_NOTICE("SyncAllowScript requested a backup now "
			"(" << script << ")");
	}
	else
	{
		try
		{
			// How many seconds to wait?
			waitInSeconds = BoxConvert::Convert<int32_t, const std::string&>(delay);
		}
		catch(ConversionException &e)
		{
			BOX_ERROR("SyncAllowScript output an invalid "
				"number: '" << output << "' (" <<
				script << ")");
			throw;
		}

		BOX_NOTICE("SyncAllowScript requested a delay of " <<
			waitInSeconds << " seconds (" << script << ")");
	}

	if(iss.eof())
	{
		// No bandwidth limit requested
		mMaxBandwidthFromSyncAllowScript = 0;
		BOX_NOTICE("SyncAllowScript did not set a maximum bandwidth "
			"(" << script << ")");
	}
	else
	{
		std::string maxBandwidth;
		iss >> maxBandwidth;

		try
		{
			// How many seconds to wait?
			mMaxBandwidthFromSyncAllowScript =
				BoxConvert::Convert<int32_t, const std::string&>(maxBandwidth);
		}
		catch(ConversionException &e)
		{
			BOX_ERROR("Invalid maximum bandwidth from "
				"SyncAllowScript: '" <<
				output << "' (" << script << ")");
			throw;
		}

		BOX_NOTICE("SyncAllowScript set maximum bandwidth to " <<
			mMaxBandwidthFromSyncAllowScript << " kB/s (" <<
			script << ")");
	}

	return waitInSeconds;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::RunBackgroundTask()
//		Purpose: Checks for connections or commands on the command
//			 socket and handles them with minimal delay. Polled
//			 during lengthy operations such as file uploads.
//		Created: 07/04/14
//
// --------------------------------------------------------------------------
bool BackupDaemon::RunBackgroundTask(State state, uint64_t progress,
	uint64_t maximum)
{
	BOX_TRACE("BackupDaemon::RunBackgroundTask: state = " << state <<
		", progress = " << progress << "/" << maximum);

	if(!mapCommandSocketPollTimer.get())
	{
		return true; // no background task
	}

	if(mapCommandSocketPollTimer->HasExpired())
	{
		mapCommandSocketPollTimer->Reset(COMMAND_SOCKET_POLL_INTERVAL);
	}
	else
	{
		// Do no more work right now
		return true;
	}

	if(mapCommandSocketInfo.get())
	{
		BOX_TRACE("BackupDaemon::RunBackgroundTask: polling command socket");

		bool sync_flag_out, sync_is_forced_out;

		WaitOnCommandSocket(0, // RequiredDelay
			sync_flag_out, sync_is_forced_out);

		if(sync_flag_out)
		{
			BOX_WARNING("Ignoring request to sync while "
				"already syncing.");
		}
	}

	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::WaitOnCommandSocket(box_time_t, bool &, bool &)
//		Purpose: Waits on a the command socket for a time of UP TO
//			 the required time but may be much less, and handles
//			 a command if necessary.
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::WaitOnCommandSocket(box_time_t RequiredDelay, bool &DoSyncFlagOut, bool &SyncIsForcedOut)
{
	DoSyncFlagOut = false;
	SyncIsForcedOut = false;

	ASSERT(mapCommandSocketInfo.get());
	if(!mapCommandSocketInfo.get())
	{
		// failure case isn't too bad
		::sleep(1);
		return;
	}
	
	BOX_TRACE("Wait on command socket, delay = " <<
		BOX_FORMAT_MICROSECONDS(RequiredDelay));
	
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
			// There should be no GetLine, as it would be holding onto a
			// pointer to a dead mpConnectedSocket.
			ASSERT(!mapCommandSocketInfo->mapGetLine.get());

			// No connection, listen for a new one
			mapCommandSocketInfo->mpConnectedSocket.reset(
				mapCommandSocketInfo->mListeningSocket.Accept(timeout).release());
			
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
					std::ostringstream hello;
					hello << "bbackupd: " << 
						(conf.GetKeyValueBool("AutomaticBackup") ? 1 : 0) 
						<< " " <<
						conf.GetKeyValueInt("UpdateStoreInterval") 
						<< " " <<
						conf.GetKeyValueInt("MinimumFileAge") 
						<< " " <<
						conf.GetKeyValueInt("MaxUploadWait") 
						<< "\nstate " << mState << "\n";
					mapCommandSocketInfo->mpConnectedSocket->Write(
						hello.str(), timeout);
					
					// Set the timeout to something very small, so we don't wait too long on waiting
					// for any incoming data
					timeout = 10; // milliseconds
				}
			}

			mapCommandSocketInfo->mapGetLine.reset(
				new IOStreamGetLine(
					*(mapCommandSocketInfo->mpConnectedSocket.get())));
		}

		// So there must be a connection now.
		ASSERT(mapCommandSocketInfo->mpConnectedSocket.get() != 0);
		ASSERT(mapCommandSocketInfo->mapGetLine.get() != 0);
		
		// Ping the remote side, to provide errors which will mean the socket gets closed
		mapCommandSocketInfo->mpConnectedSocket->Write("ping\n", 5,
			timeout);
		
		// Wait for a command or something on the socket
		std::string command;
		while(mapCommandSocketInfo->mapGetLine.get() != 0
			&& !mapCommandSocketInfo->mapGetLine->IsEOF()
			&& mapCommandSocketInfo->mapGetLine->GetLine(command, false /* no preprocessing */, timeout))
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
				std::string response = sendOK ? "ok\n" : "error\n";
				mapCommandSocketInfo->mpConnectedSocket->Write(
					response, timeout);
			}
			
			// Set timeout to something very small, so this just checks for data which is waiting
			timeout = 1;
		}
		
		// Close on EOF?
		if(mapCommandSocketInfo->mapGetLine.get() != 0 &&
			mapCommandSocketInfo->mapGetLine->IsEOF())
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
		mapCommandSocketInfo->mapGetLine.reset();
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
			mapCommandSocketInfo->mpConnectedSocket->Write(message,
				1); // short timeout, it's overlapped
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
	// Going to need a copy of the root directory. Get a connection,
	// and fetch it.
	BackupProtocolCallable& connection(rClientContext.GetConnection());
	
	// Ask server for a list of everything in the root directory,
	// which is a directory itself
	std::auto_ptr<BackupProtocolSuccess> dirreply(
		connection.QueryListDirectory(
			BackupProtocolListDirectory::RootDirectory,
			// only directories
			BackupProtocolListDirectory::Flags_Dir,
			// exclude old/deleted stuff
			BackupProtocolListDirectory::Flags_Deleted |
			BackupProtocolListDirectory::Flags_OldVersion,
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
#ifndef BOX_RELEASE_BUILD
	{
		std::set<std::string, mntLenCompare>::reverse_iterator i(mountPoints.rbegin());
		ASSERT(*i == "/");
	}
#endif // n BOX_RELEASE_BUILD
#endif // n HAVE_STRUCT_STATFS_F_MNTONNAME || n HAVE_STRUCT_STATVFS_F_MNTONNAME
#endif // HAVE_MOUNTS

	// Then... go through each of the entries in the configuration,
	// making sure there's a directory created for it.
	std::vector<std::string> locNames =
		rLocationsConf.GetSubConfigurationNames();

	// We only want completely configured locations to be in the list
	// when this function exits, so move them all to a temporary list.
	// Entries matching a properly configured location will be moved
	// back to mLocations. Anything left in this list after the loop
	// finishes will be deleted.
	Locations tmpLocations = mLocations;
	mLocations.clear();

	// The ID map list will be repopulated automatically by this loop
	mIDMapMounts.clear();

	for(std::vector<std::string>::iterator
		pLocName  = locNames.begin();
		pLocName != locNames.end();
		pLocName++)
	{
		Location* pLoc = NULL;

		// Try to find and reuse an existing Location object
		for(Locations::const_iterator
			i  = tmpLocations.begin();
			i != tmpLocations.end(); i++)
		{
			if ((*i)->mName == *pLocName)
			{
				BOX_TRACE("Location already configured: " << *pLocName);
				pLoc = *i;
				break;
			}
		}
			
		const Configuration& rConfig(
			rLocationsConf.GetSubConfiguration(*pLocName));
		std::auto_ptr<Location> apLoc;

		try
		{
			if(pLoc == NULL)
			{
				// Create a record for it
				BOX_TRACE("New location: " << *pLocName);
				pLoc = new Location;

				// ensure deletion if setup fails
				apLoc.reset(pLoc);

				// Setup names in the location record
				pLoc->mName = *pLocName;
				pLoc->mPath = rConfig.GetKeyValue("Path");
			}

			// Read the exclude lists from the Configuration
			pLoc->mapExcludeFiles.reset(BackupClientMakeExcludeList_Files(rConfig));
			pLoc->mapExcludeDirs.reset(BackupClientMakeExcludeList_Dirs(rConfig));

			// Does this exist on the server?
			// Remove from dir object early, so that if we fail
			// to stat the local directory, we still don't
			// consider to remote one for deletion.
			BackupStoreDirectory::Iterator iter(dir);
			BackupStoreFilenameClear dirname(pLoc->mName);	// generate the filename
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
				if(::statvfs(pLoc->mPath.c_str(), &s) != 0)
#else // HAVE_STRUCT_STATVFS_F_MNTONNAME
				struct statfs s;
				if(::statfs(pLoc->mPath.c_str(), &s) != 0)
#endif // HAVE_STRUCT_STATVFS_F_MNTONNAME
				{
					THROW_SYS_ERROR("Failed to stat path "
						"'" << pLoc->mPath << "' "
						"for location "
						"'" << pLoc->mName << "'",
						CommonException, OSFileError);
				}

				// Where the filesystem is mounted
				std::string mountName(s.f_mntonname);

#else // !HAVE_STRUCT_STATFS_F_MNTONNAME && !WIN32

				// Warn in logs if the directory isn't absolute
				if(pLoc->mPath[0] != '/')
				{
					BOX_WARNING("Location path '"
						<< pLoc->mPath 
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
						if(::strncmp(i->c_str(), pLoc->mPath.c_str(), i->size()) == 0)
						{
							// Match
							mountName = *i;
							break;
						}
					}
					BOX_TRACE("mount point chosen for "
						<< pLoc->mPath << " is "
						<< mountName);
				}

#endif
				
				// Got it?
				std::map<std::string, int>::iterator f(mounts.find(mountName));
				if(f != mounts.end())
				{
					// Yes -- store the index
					pLoc->mIDMapIndex = f->second;
				}
				else
				{
					// No -- new index
					pLoc->mIDMapIndex = numIDMaps;
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
					attr.ReadAttributes(pLoc->mPath.c_str(), 
						true /* directories have zero mod times */,
						0 /* not interested in mod time */, 
						&attrModTime /* get the attribute modification time */);
				}
				catch (BoxException &e)
				{
					BOX_ERROR("Failed to get attributes "
						"for path '" << pLoc->mPath
						<< "', skipping location '" <<
						pLoc->mName << "'");
					throw;
				}
				
				// Execute create directory command
				try
				{
					std::auto_ptr<IOStream> attrStream(
						new MemBlockStream(attr));
					std::auto_ptr<BackupProtocolSuccess>
						dirCreate(connection.QueryCreateDirectory(
						BACKUPSTORE_ROOT_DIRECTORY_ID, // containing directory
						attrModTime, dirname, attrStream));
						
					// Object ID for later creation
					oid = dirCreate->GetObjectID();
				}
				catch (BoxException &e)
				{
					BOX_ERROR("Failed to create remote "
						"directory '/" << pLoc->mName <<
						"', skipping location '" <<
						pLoc->mName << "'");
					throw;
				}

			}

			// Create and store the directory object for the root of this location
			ASSERT(oid != 0);
			if(pLoc->mapDirectoryRecord.get() == NULL)
			{
				pLoc->mapDirectoryRecord.reset(
					new BackupClientDirectoryRecord(oid, *pLocName));
			}
			
			// Remove it from the temporary list to avoid deletion
			tmpLocations.remove(pLoc);

			// Push it back on the vector of locations
			mLocations.push_back(pLoc);

			if(apLoc.get() != NULL)
			{
				// Don't delete it now!
				apLoc.release();
			}
		}
		catch (std::exception &e)
		{
			BOX_ERROR("Failed to configure location '"
				<< pLoc->mName << "' path '"
				<< pLoc->mPath << "': " << e.what() <<
				": please check for previous errors");
			mReadErrorsOnFilesystemObjects = true;
		}
		catch(...)
		{
			BOX_ERROR("Failed to configure location '"
				<< pLoc->mName << "' path '"
				<< pLoc->mPath << "': please check for "
				"previous errors");
			mReadErrorsOnFilesystemObjects = true;
		}
	}

	// Now remove any leftovers
	for(BackupDaemon::Locations::iterator
		i  = tmpLocations.begin();
		i != tmpLocations.end(); i++)
	{
		BOX_INFO("Removing obsolete location from memory: " <<
			(*i)->mName);
		delete *i;
	}

	tmpLocations.clear();
	
	// Any entries in the root directory which need deleting?
	if(dir.GetNumberOfEntries() > 0 &&
		mDeleteRedundantLocationsAfter == 0)
	{
		BOX_NOTICE(dir.GetNumberOfEntries() << " redundant locations "
			"in root directory found, but will not delete because "
			"DeleteRedundantLocationsAfter = 0");
	}
	else if(dir.GetNumberOfEntries() > 0)
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
	// Make sure we have some blank, empty ID maps
	DeleteIDMapVector(mNewIDMaps);
	FillIDMapVector(mNewIDMaps, true /* new maps */);
	DeleteIDMapVector(mCurrentIDMaps);
	FillIDMapVector(mCurrentIDMaps, false /* new maps */);
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

			// The new map file should not exist yet. If there's
			// one left over from a previous failed run, it's not
			// useful to us because we never read from it and will
			// overwrite the entries of all files that still
			// exist, so we should just delete it and start afresh.
			if(NewMaps && FileExists(filename.c_str()))
			{
				BOX_NOTICE("Found an incomplete ID map "
					"database, deleting it to start "
					"afresh: " << filename);
				if(EMU_UNLINK(filename.c_str()) != 0)
				{
					BOX_LOG_NATIVE_ERROR(BOX_FILE_MESSAGE(
						filename, "Failed to delete "
						"incomplete ID map database"));
				}
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
		EMU_UNLINK(filename.c_str());
		
		// Add a suffix for the new map
		filename += ".n";

		// Delete that too
		BOX_TRACE("Deleting " << filename);
		EMU_UNLINK(filename.c_str());
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
	for(Locations::const_iterator i(mLocations.begin()); i != mLocations.end(); ++i)
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

	std::ostringstream msg;
	msg << "state " << State << "\n";

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
		mapCommandSocketInfo->mpConnectedSocket->Write(msg.str(),
			1); // very short timeout, it's overlapped anyway
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
	try
	{
		FileStream touch(fn, O_WRONLY | O_CREAT | O_TRUNC,
			S_IRUSR | S_IWUSR);
	}
	catch (std::exception &e)
	{
		BOX_ERROR("Failed to write to timestamp file: " << fn << ": " <<
			e.what());
	}
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
		THROW_EXCEPTION_MESSAGE(BackupStoreException,
			BadNotifySysadminEventCode, "NotifySysadmin() called "
			"for unknown event code " << Event);
	}

	BOX_TRACE("BackupDaemon::NotifySysadmin() called, event = " << 
		sEventNames[Event]);

	if(!GetConfiguration().KeyExists("NotifyAlways") ||
		!GetConfiguration().GetKeyValueBool("NotifyAlways"))
	{
		// Don't send lots of repeated messages
		// Note: backup-start and backup-finish will always be
		// logged, because mLastNotifiedEvent is never set to
		// these values and therefore they are never "duplicates".
		if(mLastNotifiedEvent == Event)
		{
			if(Event == SysadminNotifier::BackupOK)
			{
				BOX_INFO("Suppressing duplicate notification "
					"about " << sEventNames[Event]);
			}
			else
			{
				BOX_WARNING("Suppressing duplicate notification "
					"about " << sEventNames[Event]);
			}
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
			BOX_INFO("Not notifying administrator about event "
				<< sEventNames[Event] << ", set NotifyScript "
				"to do this in future");
		}
		return;
	}

	// Script to run
	std::string script(conf.GetKeyValue("NotifyScript") + " " +
		sEventNames[Event] + " \"" + GetConfigFileName() + "\"");
	
	// Log what we're about to do
	BOX_INFO("About to notify administrator about event "
		<< sEventNames[Event] << ", running script '" << script << "'");
	
	// Then do it
	int returnCode = ::system(script.c_str());
	if(returnCode != 0)
	{
		BOX_WARNING("Notify script returned error code: " <<
			returnCode << " (" << script << ")");
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
	BackupProtocolCallable &connection(rContext.GetConnection());
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
//		Name:    BackupDaemon::CommandSocketInfo::CommandSocketInfo()
//		Purpose: Constructor
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
BackupDaemon::CommandSocketInfo::CommandSocketInfo()
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

		for(Locations::const_iterator i = mLocations.begin();
			i != mLocations.end(); i++)
		{
			ASSERT(*i);
			(*i)->Serialize(anArchive);
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
		BOX_NOTICE("Store object info file is not enabled. Will "
			"download directory listings from store.");
		return false;
	}

	std::string StoreObjectInfoFile =
		GetConfiguration().GetKeyValue("StoreObjectInfoFile");

	if(StoreObjectInfoFile.size() <= 0)
	{
		return false;
	}

	int64_t fileSize;
	if (!FileExists(StoreObjectInfoFile, &fileSize) || fileSize == 0)
	{
		BOX_NOTICE(BOX_FILE_MESSAGE(StoreObjectInfoFile,
			"Store object info file does not exist or is empty"));
	}
	else
	{
		try
		{
			FileStream aFile(StoreObjectInfoFile, O_RDONLY);
			Archive anArchive(aFile, 0);

			//
			// see if the content looks like a valid serialised archive
			//
			int iMagicValue = 0;
			anArchive.Read(iMagicValue);

			if(iMagicValue != STOREOBJECTINFO_MAGIC_ID_VALUE)
			{
				BOX_WARNING(BOX_FILE_MESSAGE(StoreObjectInfoFile,
					"Store object info file is not a valid "
					"or compatible serialised archive"));
				return false;
			}

			//
			// get a bit optimistic and read in a string identifier
			//
			std::string strMagicValue;
			anArchive.Read(strMagicValue);

			if(strMagicValue != STOREOBJECTINFO_MAGIC_ID_STRING)
			{
				BOX_WARNING(BOX_FILE_MESSAGE(StoreObjectInfoFile,
					"Store object info file is not a valid "
					"or compatible serialised archive"));
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
				BOX_WARNING(BOX_FILE_MESSAGE(StoreObjectInfoFile,
					"Store object info file version " <<
					iVersion << " is not supported"));
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
				BOX_WARNING(BOX_FILE_MESSAGE(StoreObjectInfoFile,
					"Store object info file is older than "
					"configuration file"));
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

			BOX_INFO(BOX_FILE_MESSAGE(StoreObjectInfoFile,
				"Loaded store object info file version " << iVersion));
			return true;
		} 
		catch(std::exception &e)
		{
			BOX_ERROR(BOX_FILE_MESSAGE(StoreObjectInfoFile,
				"Internal error reading store object info "
				"file: " << e.what()));
		}
		catch(...)
		{
			BOX_ERROR(BOX_FILE_MESSAGE(StoreObjectInfoFile,
				"Internal error reading store object info "
				"file: unknown error"));
		}
	}

	BOX_NOTICE("No usable cache, will download directory listings from "
		"server.");

	DeleteAllLocations();

	mClientStoreMarker = BackupClientContext::ClientStoreMarker_NotKnown;
	theLastSyncTime = 0;
	theNextSyncTime = 0;

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
	if(EMU_UNLINK(storeObjectInfoFile.c_str()) != 0)
	{
		BOX_LOG_SYS_ERROR("Failed to delete the old "
			"StoreObjectInfoFile: " << storeObjectInfoFile);
		return false;
	}

	return true;
}
