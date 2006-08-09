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
#include <string.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif
#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif
#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
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

#include "Configuration.h"
#include "IOStream.h"
#include "MemBlockStream.h"
#include "CommonException.h"
#include "BoxPortsAndFiles.h"

#include "SSLLib.h"
#include "TLSContext.h"

#include "BackupDaemon.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupClientContext.h"
#include "BackupClientDirectoryRecord.h"
#include "BackupStoreDirectory.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreFilenameClear.h"
#include "BackupClientInodeToIDMap.h"
#include "autogen_BackupProtocolClient.h"
#include "BackupClientCryptoKeys.h"
#include "BannerText.h"
#include "BackupStoreFile.h"
#include "Random.h"
#include "ExcludeList.h"
#include "BackupClientMakeExcludeList.h"
#include "IOStreamGetLine.h"
#include "Utils.h"
#include "FileStream.h"
#include "BackupStoreException.h"
#include "BackupStoreConstants.h"
#include "LocalProcessStream.h"
#include "IOStreamGetLine.h"
#include "Conversion.h"
#include "Archive.h"

#include "MemLeakFindOn.h"

static const time_t MAX_SLEEP_TIME = 1024;

// Make the actual sync period have a little bit of extra time, up to a 64th of the main sync period.
// This prevents repetative cycles of load on the server
#define		SYNC_PERIOD_RANDOM_EXTRA_TIME_SHIFT_BY	6

#ifdef WIN32
// --------------------------------------------------------------------------
//
// Function
//		Name:    HelperThread()
//		Purpose: Background thread function, called by Windows,
//			calls the BackupDaemon's RunHelperThread method
//			to listen for and act on control communications
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
unsigned int WINAPI HelperThread(LPVOID lpParam) 
{ 
	((BackupDaemon *)lpParam)->RunHelperThread();

	return 0;
}
#endif

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
	  mpCommandSocketInfo(0),
	  mDeleteUnusedRootDirEntriesAfter(0)
{
	// Only ever one instance of a daemon
	SSLLib::Initialise();
	
	// Initialise notifcation sent status
	for(int l = 0; l <= NotifyEvent__MAX; ++l)
	{
		mNotificationsSent[l] = false;
	}

#ifdef WIN32
	// Create a thread to handle the named pipe
	HANDLE hThread;
	unsigned int dwThreadId;

	hThread = (HANDLE) _beginthreadex( 
        	NULL,                        // default security attributes 
        	0,                           // use default stack size  
        	HelperThread,                // thread function 
        	this,                        // argument to thread function 
        	0,                           // use default creation flags 
        	&dwThreadId);                // returns the thread identifier 
#endif
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
	
	if(mpCommandSocketInfo != 0)
	{
		delete mpCommandSocketInfo;
		mpCommandSocketInfo = 0;
	}
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
const char *BackupDaemon::DaemonBanner() const
{
#ifndef NDEBUG
	// Don't display banner in debug builds
	return 0;
#else
	return BANNER_TEXT("Backup Client");
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
		printf(
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
	
	// And delete everything from the assoicated mount vector
	mIDMapMounts.clear();
}

#ifdef WIN32
void BackupDaemon::RunHelperThread(void)
{
	mpCommandSocketInfo = new CommandSocketInfo;
	WinNamedPipeStream& rSocket(mpCommandSocketInfo->mListeningSocket);

	// loop until the parent process exits, or we decide
	// to kill the thread ourselves
	while (!IsTerminateWanted())
	{
		try
		{
			rSocket.Accept(BOX_NAMED_PIPE_NAME);
		}
		catch (BoxException &e)
		{
			::syslog(LOG_ERR, "Failed to open command socket: %s",
				e.what());
			SetTerminateWanted();
			break; // this is fatal to listening thread
		}
		catch (...)
		{
			::syslog(LOG_ERR, "Failed to open command socket: "
				"unknown error");
			SetTerminateWanted();
			break; // this is fatal to listening thread
		}

		try
		{
			// Errors here do not kill the thread,
			// only the current connection.

			// This next section comes from Ben's original function
			// Log
			::syslog(LOG_INFO, "Connection from command socket");

			// Send a header line summarising the configuration 
			// and current state
			const Configuration &conf(GetConfiguration());
			char summary[256];
			size_t summarySize = sprintf(summary, 
				"bbackupd: %d %d %d %d\nstate %d\n",
				conf.GetKeyValueBool("AutomaticBackup"),
				conf.GetKeyValueInt("UpdateStoreInterval"),
				conf.GetKeyValueInt("MinimumFileAge"),
				conf.GetKeyValueInt("MaxUploadWait"),
				mState);

			rSocket.Write(summary, summarySize);
			rSocket.Write("ping\n", 5);

			IOStreamGetLine readLine(rSocket);
			std::string command;

			while (rSocket.IsConnected() && 
				readLine.GetLine(command) &&
				!IsTerminateWanted())
			{
				TRACE1("Received command '%s' over "
					"command socket\n", command.c_str());

				bool sendOK = false;
				bool sendResponse = true;
				bool disconnect = false;

				// Command to process!
				if(command == "quit" || command == "")
				{
					// Close the socket.
					disconnect = true;
					sendResponse = false;
				}
				else if(command == "sync")
				{
					// Sync now!
					this->mDoSyncFlagOut = true;
					this->mSyncIsForcedOut = false;
					sendOK = true;
				}
				else if(command == "force-sync")
				{
					// Sync now (forced -- overrides any SyncAllowScript)
					this->mDoSyncFlagOut = true;
					this->mSyncIsForcedOut = true;
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
				else
				{
					::syslog(LOG_ERR, "Received unknown command '%s' from client", command.c_str());
					sendResponse = true;
					sendOK = false;
				}

				// Send a response back?
				if (sendResponse)
				{
					const char* response = sendOK ? "ok\n" : "error\n";
					rSocket.Write(
						response, strlen(response));
				}

				if (disconnect) 
				{
					break;
				}

				this->mReceivedCommandConn = true;
			}

			rSocket.Close();
		}
		catch (BoxException &e)
		{
			::syslog(LOG_ERR, "Communication error with "
				"control client: %s", e.what());
		}
		catch (...)
		{
			::syslog(LOG_ERR, "Communication error with control client");
		}
	}
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
#ifdef WIN32
	// init our own timer for file diff timeouts
	InitTimer();

	try
	{
		Run2();
	}
	catch(...)
	{
		FiniTimer();
		throw;
	}

	FiniTimer();
#else // ! WIN32
	// Ignore SIGPIPE (so that if a command connection is broken, the daemon doesn't terminate)
	::signal(SIGPIPE, SIG_IGN);

	// Create a command socket?
	const Configuration &conf(GetConfiguration());
	if(conf.KeyExists("CommandSocket"))
	{
		// Yes, create a local UNIX socket
		mpCommandSocketInfo = new CommandSocketInfo;
		const char *socketName = conf.GetKeyValue("CommandSocket").c_str();
		::unlink(socketName);
		mpCommandSocketInfo->mListeningSocket.Listen(Socket::TypeUNIX, socketName);
	}

	// Handle things nicely on exceptions
	try
	{
		Run2();
	}
	catch(...)
	{
		if(mpCommandSocketInfo != 0)
		{
			try 
			{
				delete mpCommandSocketInfo;
			}
			catch(...)
			{
				::syslog(LOG_WARNING,
					"Error closing command socket "
					"after exception, ignored.");
			}
			mpCommandSocketInfo = 0;
		}

		throw;
	}

	// Clean up
	if(mpCommandSocketInfo != 0)
	{
		delete mpCommandSocketInfo;
		mpCommandSocketInfo = 0;
	}
#endif
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
	// Read in the certificates creating a TLS context
	TLSContext tlsContext;
	const Configuration &conf(GetConfiguration());
	std::string certFile(conf.GetKeyValue("CertificateFile"));
	std::string keyFile(conf.GetKeyValue("PrivateKeyFile"));
	std::string caFile(conf.GetKeyValue("TrustedCAsFile"));
	tlsContext.Initialise(false /* as client */, certFile.c_str(), keyFile.c_str(), caFile.c_str());
	
	// Set up the keys for various things
	BackupClientCryptoKeys_Setup(conf.GetKeyValue("KeysFile").c_str());

	// max diffing time, keep-alive time
	if(conf.KeyExists("MaximumDiffingTime"))
	{
		BackupClientContext::SetMaximumDiffingTime(conf.GetKeyValueInt("MaximumDiffingTime"));
	}
	if(conf.KeyExists("KeepAliveTime"))
	{
		BackupClientContext::SetKeepAliveTime(conf.GetKeyValueInt("KeepAliveTime"));
	}

	// Setup various timings
	
	// How often to connect to the store (approximate)
	box_time_t updateStoreInterval = SecondsToBoxTime(conf.GetKeyValueInt("UpdateStoreInterval"));

	// But are we connecting automatically?
	bool automaticBackup = conf.GetKeyValueBool("AutomaticBackup");
	
	// The minimum age a file needs to be before it will be considered for uploading
	box_time_t minimumFileAge = SecondsToBoxTime(conf.GetKeyValueInt("MinimumFileAge"));

	// The maximum time we'll wait to upload a file, regardless of how often it's modified
	box_time_t maxUploadWait = SecondsToBoxTime(conf.GetKeyValueInt("MaxUploadWait"));
	// Adjust by subtracting the minimum file age, so is relative to sync period end in comparisons
	maxUploadWait = (maxUploadWait > minimumFileAge)?(maxUploadWait - minimumFileAge):(0);

	// When the next sync should take place -- which is ASAP
	box_time_t nextSyncTime = 0;

	// When the last sync started (only updated if the store was not full when the sync ended)
	box_time_t lastSyncTime = 0;

 	// --------------------------------------------------------------------------------------------
 
	// And what's the current client store marker?
	int64_t clientStoreMarker = 
		BackupClientContext::ClientStoreMarker_NotKnown;
	// haven't contacted the store yet

 	bool deleteStoreObjectInfoFile = DeserializeStoreObjectInfo(
		clientStoreMarker, lastSyncTime, nextSyncTime);
 
	// --------------------------------------------------------------------------------------------
	

	// Set state
	SetState(State_Idle);

	// Loop around doing backups
	do
	{
		// Flags used below
		bool storageLimitExceeded = false;
		bool doSync = false;
		bool doSyncForcedByCommand = false;

		// Is a delay necessary?
		{
			box_time_t currentTime;
			do
			{
				// Need to check the stop run thing here too, so this loop isn't run if we should be stopping
				if(StopRun()) break;
				
				currentTime = GetCurrentBoxTime();
	
				// Pause a while, but no more than MAX_SLEEP_TIME seconds (use the conditional because times are unsigned)
				box_time_t requiredDelay = (nextSyncTime < currentTime)?(0):(nextSyncTime - currentTime);
				// If there isn't automatic backup happening, set a long delay. And limit delays at the same time.
				if(!automaticBackup || requiredDelay > SecondsToBoxTime(MAX_SLEEP_TIME))
					requiredDelay = SecondsToBoxTime(MAX_SLEEP_TIME);

				// Only do the delay if there is a delay required
				if(requiredDelay > 0)
				{
					// Sleep somehow. There are choices on how this should be done, depending on the state of the control connection
					if(mpCommandSocketInfo != 0)
					{
						// A command socket exists, so sleep by handling connections with it
						WaitOnCommandSocket(requiredDelay, doSync, doSyncForcedByCommand);
					}
					else
					{
						// No command socket or connection, just do a normal sleep
						time_t sleepSeconds = BoxTimeToSeconds(requiredDelay);
						::sleep((sleepSeconds <= 0)?1:sleepSeconds);
					}
				}
				
			} while((!automaticBackup || (currentTime < nextSyncTime)) && !doSync && !StopRun());
		}

		// Time of sync start, and if it's time for another sync (and we're doing automatic syncs), set the flag
		box_time_t currentSyncStartTime = GetCurrentBoxTime();
		if(automaticBackup && currentSyncStartTime >= nextSyncTime)
		{
			doSync = true;
		}
		
		// Use a script to see if sync is allowed now?
		if(!doSyncForcedByCommand && doSync && !StopRun())
		{
			int d = UseScriptToSeeIfSyncAllowed();
			if(d > 0)
			{
				// Script has asked for a delay
				nextSyncTime = GetCurrentBoxTime() + SecondsToBoxTime(d);
				doSync = false;
			}
		}

		// Ready to sync? (but only if we're not supposed to be stopping)
		if(doSync && !StopRun())
		{
			// Touch a file to record times in filesystem
			TouchFileInWorkingDir("last_sync_start");
		
			// Tell anything connected to the command socket
			SendSyncStartOrFinish(true /* start */);
			
			// Reset statistics on uploads
			BackupStoreFile::ResetStats();
			
			// Calculate the sync period of files to examine
			box_time_t syncPeriodStart = lastSyncTime;
			box_time_t syncPeriodEnd = currentSyncStartTime - minimumFileAge;
			// Check logic
			ASSERT(syncPeriodEnd > syncPeriodStart);
			// Paranoid check on sync times
			if(syncPeriodStart >= syncPeriodEnd) continue;
			
			// Adjust syncPeriodEnd to emulate snapshot behaviour properly
			box_time_t syncPeriodEndExtended = syncPeriodEnd;
			// Using zero min file age?
			if(minimumFileAge == 0)
			{
				// Add a year on to the end of the end time, to make sure we sync
				// files which are modified after the scan run started.
				// Of course, they may be eligable to be synced again the next time round,
				// but this should be OK, because the changes only upload should upload no data.
				syncPeriodEndExtended += SecondsToBoxTime((time_t)(356*24*3600));
			}

			// Delete the serialised store object file,
			// so that we don't try to reload it after a
			// partially completed backup
			if(deleteStoreObjectInfoFile && 
				!DeleteStoreObjectInfo())
			{
				::syslog(LOG_ERR, "Failed to delete the "
					"StoreObjectInfoFile, backup cannot "
					"continue safely.");
				// prevent runaway process where the logs fill up -- without this
				// the log message will be emitted in a tight loop.
				::sleep(60); 
				continue;
			}

			// In case the backup throws an exception,
			// we should not try to delete the store info
			// object file again.
			deleteStoreObjectInfoFile = false;
			
			// Do sync
			bool errorOccurred = false;
			int errorCode = 0, errorSubCode = 0;
			const char* errorString = "unknown";

			try
			{
				// Set state and log start
				SetState(State_Connected);
				::syslog(LOG_INFO, "Beginning scan of local files");

				// Then create a client context object (don't just connect, as this may be unnecessary)
				BackupClientContext clientContext(*this, tlsContext, conf.GetKeyValue("StoreHostname"),
					conf.GetKeyValueInt("AccountNumber"), conf.GetKeyValueBool("ExtendedLogging"));
					
				// Set up the sync parameters
				BackupClientDirectoryRecord::SyncParams params(*this, clientContext);
				params.mSyncPeriodStart = syncPeriodStart;
				params.mSyncPeriodEnd = syncPeriodEndExtended; // use potentially extended end time
				params.mMaxUploadWait = maxUploadWait;
				params.mFileTrackingSizeThreshold = conf.GetKeyValueInt("FileTrackingSizeThreshold");
				params.mDiffingUploadSizeThreshold = conf.GetKeyValueInt("DiffingUploadSizeThreshold");
				params.mMaxFileTimeInFuture = SecondsToBoxTime(conf.GetKeyValueInt("MaxFileTimeInFuture"));
				
				// Set store marker
				clientContext.SetClientStoreMarker(clientStoreMarker);
				
				// Set up the locations, if necessary -- need to do it here so we have a (potential) connection to use
				if(mLocations.empty())
				{
					const Configuration &locations(conf.GetSubConfiguration("BackupLocations"));
					
					// Make sure all the directory records are set up
					SetupLocations(clientContext, locations);
				}
				
				// Get some ID maps going
				SetupIDMapsForSync();

				// Delete any unused directories?
				DeleteUnusedRootDirEntries(clientContext);
								
				// Go through the records, syncing them
				for(std::vector<Location *>::const_iterator i(mLocations.begin()); i != mLocations.end(); ++i)
				{
					// Set current and new ID map pointers in the context
					clientContext.SetIDMaps(mCurrentIDMaps[(*i)->mIDMapIndex], mNewIDMaps[(*i)->mIDMapIndex]);
				
					// Set exclude lists (context doesn't take ownership)
					clientContext.SetExcludeLists((*i)->mpExcludeFiles, (*i)->mpExcludeDirs);

					// Sync the directory
					(*i)->mpDirectoryRecord->SyncDirectory(params, BackupProtocolClientListDirectory::RootDirectory, (*i)->mPath);

					// Unset exclude lists (just in case)
					clientContext.SetExcludeLists(0, 0);
				}
				
				// Errors reading any files?
				if(params.mReadErrorsOnFilesystemObjects)
				{
					// Notify administrator
					NotifySysadmin(NotifyEvent_ReadError);
				}
				else
				{
					// Unset the read error flag, so the error is
					// reported again in the future
					mNotificationsSent[NotifyEvent_ReadError] = false;
				}
				
				// Perform any deletions required -- these are delayed until the end
				// to allow renaming to happen neatly.
				clientContext.PerformDeletions();

				// Close any open connection
				clientContext.CloseAnyOpenConnection();
				
				// Get the new store marker
				clientStoreMarker = clientContext.GetClientStoreMarker();
				
				// Check the storage limit
				if(clientContext.StorageLimitExceeded())
				{
					// Tell the sysadmin about this
					NotifySysadmin(NotifyEvent_StoreFull);
				}
				else
				{
					// The start time of the next run is the end time of this run
					// This is only done if the storage limit wasn't exceeded (as things won't have been done properly if it was)
					lastSyncTime = syncPeriodEnd;
					// unflag the storage full notify flag so that next time the store is full, and alert will be sent
					mNotificationsSent[NotifyEvent_StoreFull] = false;
				}
				
				// Calculate when the next sync run should be
				nextSyncTime = currentSyncStartTime + updateStoreInterval + Random::RandomInt(updateStoreInterval >> SYNC_PERIOD_RANDOM_EXTRA_TIME_SHIFT_BY);
			
				// Commit the ID Maps
				CommitIDMapsAfterSync();

				// Log
				::syslog(LOG_INFO, "Finished scan of local files");

				// --------------------------------------------------------------------------------------------

				// We had a successful backup, save the store 
				// info. If we save successfully, we must 
				// delete the file next time we start a backup

				deleteStoreObjectInfoFile = 
					SerializeStoreObjectInfo(
						clientStoreMarker, 
						lastSyncTime, nextSyncTime);

				// --------------------------------------------------------------------------------------------
			}
			catch(BoxException &e)
			{
				errorOccurred = true;
				errorString = e.what();
				errorCode = e.GetType();
				errorSubCode = e.GetSubType();
			}
			catch(...)
			{
				// TODO: better handling of exceptions here... need to be very careful
				errorOccurred = true;
			}
			
			if(errorOccurred)
			{
				// Is it a berkely db failure?
				bool isBerkelyDbFailure = (errorCode == BackupStoreException::ExceptionType
					&& errorSubCode == BackupStoreException::BerkelyDBFailure);
				if(isBerkelyDbFailure)
				{
					// Delete corrupt files
					DeleteCorruptBerkelyDbFiles();
				}

				// Clear state data
				syncPeriodStart = 0;	// go back to beginning of time
				clientStoreMarker = BackupClientContext::ClientStoreMarker_NotKnown;	// no store marker, so download everything
				DeleteAllLocations();
				DeleteAllIDMaps();

				// Handle restart?
				if(StopRun())
				{
					::syslog(LOG_INFO, "Exception (%d/%d) due to signal", errorCode, errorSubCode);
					return;
				}

				// If the Berkely db files get corrupted, delete them and try again immediately
				if(isBerkelyDbFailure)
				{
					::syslog(LOG_ERR, "Berkely db inode map files corrupted, deleting and restarting scan. Renamed files and directories will not be tracked until after this scan.\n");
					::sleep(1);
				}
				else
				{
					// Not restart/terminate, pause and retry
					SetState(State_Error);
					::syslog(LOG_ERR, 
						"Exception caught (%s %d/%d), "
						"reset state and waiting "
						"to retry...", 
						errorString, errorCode, 
						errorSubCode);
					::sleep(10);
					nextSyncTime = currentSyncStartTime + 
						SecondsToBoxTime(90) +
						Random::RandomInt(
							updateStoreInterval >> 
							SYNC_PERIOD_RANDOM_EXTRA_TIME_SHIFT_BY);
				}
			}

			// Log the stats
			::syslog(LOG_INFO, "File statistics: total file size uploaded %lld, bytes already on server %lld, encoded size %lld",
				BackupStoreFile::msStats.mBytesInEncodedFiles, BackupStoreFile::msStats.mBytesAlreadyOnServer,
				BackupStoreFile::msStats.mTotalFileStreamSize);
			BackupStoreFile::ResetStats();

			// Tell anything connected to the command socket
			SendSyncStartOrFinish(false /* finish */);

			// Touch a file to record times in filesystem
			TouchFileInWorkingDir("last_sync_finish");
		}
		
		// Set state
		SetState(storageLimitExceeded?State_StorageLimitExceeded:State_Idle);

	} while(!StopRun());
	
	// Make sure we have a clean start next time round (if restart)
	DeleteAllLocations();
	DeleteAllIDMaps();
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
			// Got a string, intepret
			if(line == "now")
			{
				// Script says do it now. Obey.
				waitInSeconds = -1;
			}
			else
			{
				// How many seconds to wait?
				waitInSeconds = BoxConvert::Convert<int32_t, const std::string&>(line);
				::syslog(LOG_INFO, "Delaying sync by %d seconds (SyncAllowScript '%s')", waitInSeconds, conf.GetKeyValue("SyncAllowScript").c_str());
			}
		}
		
		// Wait and then cleanup child process
		int status = 0;
		::waitpid(pid, &status, 0);
	}
	catch(...)
	{
		// Ignore any exceptions
		// Log that something bad happened
		::syslog(LOG_ERR, "Error running SyncAllowScript '%s'", conf.GetKeyValue("SyncAllowScript").c_str());
		// Clean up though
		if(pid != 0)
		{
			int status = 0;
			::waitpid(pid, &status, 0);
		}
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
#ifdef WIN32
	// Really could use some interprocess protection, mutex etc
	// any side effect should be too bad???? :)
	DWORD timeout = (DWORD)BoxTimeToMilliSeconds(RequiredDelay);

	while ( this->mReceivedCommandConn == false )
	{
		Sleep(1);

		if ( timeout == 0 )
		{
			DoSyncFlagOut = false;
			SyncIsForcedOut = false;
			return;
		}
		timeout--;
	}
	this->mReceivedCommandConn = false;
	DoSyncFlagOut = this->mDoSyncFlagOut;
	SyncIsForcedOut = this->mSyncIsForcedOut;

	return;
#else // ! WIN32
	ASSERT(mpCommandSocketInfo != 0);
	if(mpCommandSocketInfo == 0) {::sleep(1); return;} // failure case isn't too bad
	
	TRACE1("Wait on command socket, delay = %lld\n", RequiredDelay);
	
	try
	{
		// Timeout value for connections and things
		int timeout = ((int)BoxTimeToMilliSeconds(RequiredDelay)) + 1;
		// Handle bad boundary cases
		if(timeout <= 0) timeout = 1;
		if(timeout == INFTIM) timeout = 100000;

		// Wait for socket connection, or handle a command?
		if(mpCommandSocketInfo->mpConnectedSocket.get() == 0)
		{
			// No connection, listen for a new one
			mpCommandSocketInfo->mpConnectedSocket.reset(mpCommandSocketInfo->mListeningSocket.Accept(timeout).release());
			
			if(mpCommandSocketInfo->mpConnectedSocket.get() == 0)
			{
				// If a connection didn't arrive, there was a timeout, which means we've
				// waited long enough and it's time to go.
				return;
			}
			else
			{
#ifdef PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
				bool uidOK = true;
				::syslog(LOG_WARNING, "On this platform, no security check can be made on the credentials of peers connecting to the command socket. (bbackupctl)");
#else
				// Security check -- does the process connecting to this socket have
				// the same UID as this process?
				bool uidOK = false;
				// BLOCK
				{
					uid_t remoteEUID = 0xffff;
					gid_t remoteEGID = 0xffff;
					if(mpCommandSocketInfo->mpConnectedSocket->GetPeerCredentials(remoteEUID, remoteEGID))
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
					::syslog(LOG_ERR, "Incoming command connection from peer had different user ID than this process, or security check could not be completed.");
					mpCommandSocketInfo->mpConnectedSocket.reset();
					return;
				}
				else
				{
					// Log
					::syslog(LOG_INFO, "Connection from command socket");
					
					// Send a header line summarising the configuration and current state
					const Configuration &conf(GetConfiguration());
					char summary[256];
					int summarySize = sprintf(summary, "bbackupd: %d %d %d %d\nstate %d\n",
						conf.GetKeyValueBool("AutomaticBackup"),
						conf.GetKeyValueInt("UpdateStoreInterval"),
						conf.GetKeyValueInt("MinimumFileAge"),
						conf.GetKeyValueInt("MaxUploadWait"),
						mState);
					mpCommandSocketInfo->mpConnectedSocket->Write(summary, summarySize);
					
					// Set the timeout to something very small, so we don't wait too long on waiting
					// for any incoming data
					timeout = 10; // milliseconds
				}
			}
		}

		// So there must be a connection now.
		ASSERT(mpCommandSocketInfo->mpConnectedSocket.get() != 0);
		
		// Is there a getline object ready?
		if(mpCommandSocketInfo->mpGetLine == 0)
		{
			// Create a new one
			mpCommandSocketInfo->mpGetLine = new IOStreamGetLine(*(mpCommandSocketInfo->mpConnectedSocket.get()));
		}
		
		// Ping the remote side, to provide errors which will mean the socket gets closed
		mpCommandSocketInfo->mpConnectedSocket->Write("ping\n", 5);
		
		// Wait for a command or something on the socket
		std::string command;
		while(mpCommandSocketInfo->mpGetLine != 0 && !mpCommandSocketInfo->mpGetLine->IsEOF()
			&& mpCommandSocketInfo->mpGetLine->GetLine(command, false /* no preprocessing */, timeout))
		{
			TRACE1("Receiving command '%s' over command socket\n", command.c_str());
			
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
				mpCommandSocketInfo->mpConnectedSocket->Write(sendOK?"ok\n":"error\n", sendOK?3:6);
			}
			
			// Set timeout to something very small, so this just checks for data which is waiting
			timeout = 1;
		}
		
		// Close on EOF?
		if(mpCommandSocketInfo->mpGetLine != 0 && mpCommandSocketInfo->mpGetLine->IsEOF())
		{
			CloseCommandConnection();
		}
	}
	catch(...)
	{
		// If an error occurs, and there is a connection active, just close that
		// connection and continue. Otherwise, let the error propagate.
		if(mpCommandSocketInfo->mpConnectedSocket.get() == 0)
		{
			throw;
		}
		else
		{
			// Close socket and ignore error
			CloseCommandConnection();
		}
	}
#endif // WIN32
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
#ifndef WIN32
	try
	{
		TRACE0("Closing command connection\n");
		
		if(mpCommandSocketInfo->mpGetLine)
		{
			delete mpCommandSocketInfo->mpGetLine;
			mpCommandSocketInfo->mpGetLine = 0;
		}
		mpCommandSocketInfo->mpConnectedSocket.reset();
	}
	catch(...)
	{
		// Ignore any errors
	}
#endif
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

#ifdef __MINGW32__
#warning race condition: what happens if socket is closed?
#endif

	if (mpCommandSocketInfo != NULL &&
#ifdef WIN32
	    mpCommandSocketInfo->mListeningSocket.IsConnected()
#else
	    mpCommandSocketInfo->mpConnectedSocket.get() != 0
#endif
	    )
	{
		const char* message = SendStart ? "start-sync\n" : "finish-sync\n";
		try
		{
#ifdef WIN32
			mpCommandSocketInfo->mListeningSocket.Write(message, 
				(int)strlen(message));
#else
			mpCommandSocketInfo->mpConnectedSocket->Write(message,
				strlen(message));
#endif
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
	
	// Going to need a copy of the root directory. Get a connection, and fetch it.
	BackupProtocolClient &connection(rClientContext.GetConnection());
	
	// Ask server for a list of everything in the root directory, which is a directory itself
	std::auto_ptr<BackupProtocolClientSuccess> dirreply(connection.QueryListDirectory(
			BackupProtocolClientListDirectory::RootDirectory,
			BackupProtocolClientListDirectory::Flags_Dir,	// only directories
			BackupProtocolClientListDirectory::Flags_Deleted | BackupProtocolClientListDirectory::Flags_OldVersion, // exclude old/deleted stuff
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
			TRACE1("Found mount point at %s\n", entry->mnt_dir);
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
			TRACE1("Found mount point at %s\n", entry.mnt_mountp);
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
		std::set<std::string, mntLenCompare>::const_reverse_iterator i(mountPoints.rbegin());
		ASSERT(*i == "/");
	}
#endif // n NDEBUG
#endif // n HAVE_STRUCT_STATFS_F_MNTONNAME || n HAVE_STRUCT_STATVFS_F_MNTONNAME
#endif // HAVE_MOUNTS

	// Then... go through each of the entries in the configuration,
	// making sure there's a directory created for it.
	for(std::list<std::pair<std::string, Configuration> >::const_iterator i = rLocationsConf.mSubConfigurations.begin();
		i != rLocationsConf.mSubConfigurations.end(); ++i)
	{
TRACE0("new location\n");
		// Create a record for it
		Location *ploc = new Location;
		try
		{
			// Setup names in the location record
			ploc->mName = i->first;
			ploc->mPath = i->second.GetKeyValue("Path");
			
			// Read the exclude lists from the Configuration
			ploc->mpExcludeFiles = BackupClientMakeExcludeList_Files(i->second);
			ploc->mpExcludeDirs = BackupClientMakeExcludeList_Dirs(i->second);
			
			// Do a fsstat on the pathname to find out which mount it's on
			{

#if defined HAVE_STRUCT_STATFS_F_MNTONNAME || defined HAVE_STRUCT_STATVFS_F_MNTONNAME || defined WIN32

				// BSD style statfs -- includes mount point, which is nice.
#ifdef HAVE_STRUCT_STATVFS_F_MNTONNAME
				struct statvfs s;
				if(::statvfs(ploc->mPath.c_str(), &s) != 0)
#else // HAVE_STRUCT_STATVFS_F_MNTONNAME
				struct statfs s;
				if(::statfs(ploc->mPath.c_str(), &s) != 0)
#endif // HAVE_STRUCT_STATVFS_F_MNTONNAME
				{
					THROW_EXCEPTION(CommonException, OSFileError)
				}

				// Where the filesystem is mounted
				std::string mountName(s.f_mntonname);

#else // !HAVE_STRUCT_STATFS_F_MNTONNAME && !WIN32

				// Warn in logs if the directory isn't absolute
				if(ploc->mPath[0] != '/')
				{
					::syslog(LOG_ERR, "Location path '%s' isn't absolute", ploc->mPath.c_str());
				}
				// Go through the mount points found, and find a suitable one
				std::string mountName("/");
				{
					std::set<std::string, mntLenCompare>::const_iterator i(mountPoints.begin());
					TRACE1("%d potential mount points\n", mountPoints.size());
					for(; i != mountPoints.end(); ++i)
					{
						// Compare first n characters with the filename
						// If it matches, the file belongs in that mount point
						// (sorting order ensures this)
						TRACE1("checking against mount point %s\n", i->c_str());
						if(::strncmp(i->c_str(), ploc->mPath.c_str(), i->size()) == 0)
						{
							// Match
							mountName = *i;
							break;
						}
					}
					TRACE2("mount point chosen for %s is %s\n", ploc->mPath.c_str(), mountName.c_str());
				}

#endif
				
				// Got it?
				std::map<std::string, int>::iterator f(mounts.find(mountName));
				if(f != mounts.end())
				{
					// Yes -- store the index
					ploc->mIDMapIndex = f->second;
				}
				else
				{
					// No -- new index
					ploc->mIDMapIndex = numIDMaps;
					mounts[mountName] = numIDMaps;
					
					// Store the mount name
					mIDMapMounts.push_back(mountName);
					
					// Increment number of maps
					++numIDMaps;
				}
			}
		
			// Does this exist on the server?
			BackupStoreDirectory::Iterator iter(dir);
			BackupStoreFilenameClear dirname(ploc->mName);	// generate the filename
			BackupStoreDirectory::Entry *en = iter.FindMatchingClearName(dirname);
			int64_t oid = 0;
			if(en != 0)
			{
				oid = en->GetObjectID();
				
				// Delete the entry from the directory, so we get a list of
				// unused root directories at the end of this.
				dir.DeleteEntry(oid);
			}
			else
			{
				// Doesn't exist, so it has to be created on the server. Let's go!
				// First, get the directory's attributes and modification time
				box_time_t attrModTime = 0;
				BackupClientFileAttributes attr;
				attr.ReadAttributes(ploc->mPath.c_str(), true /* directories have zero mod times */,
					0 /* not interested in mod time */, &attrModTime /* get the attribute modification time */);
				
				// Execute create directory command
				MemBlockStream attrStream(attr);
				std::auto_ptr<BackupProtocolClientSuccess> dirCreate(connection.QueryCreateDirectory(
					BackupProtocolClientListDirectory::RootDirectory,
					attrModTime, dirname, attrStream));
					
				// Object ID for later creation
				oid = dirCreate->GetObjectID();
			}

			// Create and store the directory object for the root of this location
			ASSERT(oid != 0);
			BackupClientDirectoryRecord *precord = new BackupClientDirectoryRecord(oid, i->first);
			ploc->mpDirectoryRecord.reset(precord);
			
			// Push it back on the vector of locations
			mLocations.push_back(ploc);
		}
		catch(...)
		{
			delete ploc;
			ploc = 0;
			throw;
		}
	}
	
	// Any entries in the root directory which need deleting?
	if(dir.GetNumberOfEntries() > 0)
	{
		::syslog(LOG_INFO, "%d redundant locations in root directory found, will delete from store after %d seconds.",
			dir.GetNumberOfEntries(), BACKUP_DELETE_UNUSED_ROOT_ENTRIES_AFTER);

		// Store directories in list of things to delete
		mUnusedRootDirEntries.clear();
		BackupStoreDirectory::Iterator iter(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = iter.Next()) != 0)
		{
			// Add name to list
			BackupStoreFilenameClear clear(en->GetName());
			const std::string &name(clear.GetClearFilename());
			mUnusedRootDirEntries.push_back(std::pair<int64_t,std::string>(en->GetObjectID(), name));
			// Log this
			::syslog(LOG_INFO, "Unused location in root: %s", name.c_str());
		}
		ASSERT(mUnusedRootDirEntries.size() > 0);
		// Time to delete them
		mDeleteUnusedRootDirEntriesAfter =
			GetCurrentBoxTime() + SecondsToBoxTime(BACKUP_DELETE_UNUSED_ROOT_ENTRIES_AFTER);
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
	// Need to do different things depending on whether it's an in memory implementation,
	// or whether it's all stored on disc.
	
#ifdef BACKIPCLIENTINODETOIDMAP_IN_MEMORY_IMPLEMENTATION

	// Make sure we have some blank, empty ID maps
	DeleteIDMapVector(mNewIDMaps);
	FillIDMapVector(mNewIDMaps, true /* new maps */);

	// Then make sure that the current maps have objects, even if they are empty
	// (for the very first run)
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
		TRACE1("Deleting %s\n", filename.c_str());
		::unlink(filename.c_str());
		
		// Add a suffix for the new map
		filename += ".n";

		// Delete that too
		TRACE1("Deleting %s\n", filename.c_str());
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
	char newStateSize = sprintf(newState, "state %d\n", State);

#ifdef WIN32
	#ifndef _MSC_VER
		#warning FIX ME: race condition
	#endif

	// what happens if the socket is closed by the other thread before
	// we can write to it? Null pointer deref at best.
	if (mpCommandSocketInfo && 
	    mpCommandSocketInfo->mListeningSocket.IsConnected())
	{
		try
		{
			mpCommandSocketInfo->mListeningSocket.Write(newState, newStateSize);
		}
		catch(...)
		{
			CloseCommandConnection();
		}
	}
#else
	if(mpCommandSocketInfo != 0 && mpCommandSocketInfo->mpConnectedSocket.get() != 0)
	{
		// Something connected to the command socket, tell it about the new state
		try
		{
			mpCommandSocketInfo->mpConnectedSocket->Write(newState, newStateSize);
		}
		catch(...)
		{
			CloseCommandConnection();
		}
	}
#endif
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
//		Purpose: Run the script to tell the sysadmin about events which need attention.
//		Created: 25/2/04
//
// --------------------------------------------------------------------------
void BackupDaemon::NotifySysadmin(int Event)
{
	static const char *sEventNames[] = {"store-full", "read-error", 0};

	TRACE1("BackupDaemon::NotifySysadmin() called, event = %d\n", Event);

	if(Event < 0 || Event > NotifyEvent__MAX)
	{
		THROW_EXCEPTION(BackupStoreException, BadNotifySysadminEventCode);
	}

	// Don't send lots of repeated messages
	if(mNotificationsSent[Event])
	{
		return;
	}

	// Is there a notifation script?
	const Configuration &conf(GetConfiguration());
	if(!conf.KeyExists("NotifyScript"))
	{
		// Log, and then return
		::syslog(LOG_ERR, "Not notifying administrator about event %s -- set NotifyScript to do this in future", sEventNames[Event]);
		return;
	}

	// Script to run
	std::string script(conf.GetKeyValue("NotifyScript") + ' ' + sEventNames[Event]);
	
	// Log what we're about to do
	::syslog(LOG_INFO, "About to notify administrator about event %s, running script '%s'", sEventNames[Event], script.c_str());
	
	// Then do it
	if(::system(script.c_str()) != 0)
	{
		::syslog(LOG_ERR, "Notify script returned an error code. ('%s')", script.c_str());
	}

	// Flag that this is done so the administrator isn't constantly bombarded with lots of errors
	mNotificationsSent[Event] = true;
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
	if(mUnusedRootDirEntries.empty() || mDeleteUnusedRootDirEntriesAfter == 0)
	{
		// Nothing to do.
		return;
	}
	
	// Check time
	if(GetCurrentBoxTime() < mDeleteUnusedRootDirEntriesAfter)
	{
		// Too early to delete files
		return;
	}

	// Entries to delete, and it's the right time to do so...
	::syslog(LOG_INFO, "Deleting unused locations from store root...");
	BackupProtocolClient &connection(rContext.GetConnection());
	for(std::vector<std::pair<int64_t,std::string> >::iterator i(mUnusedRootDirEntries.begin()); i != mUnusedRootDirEntries.end(); ++i)
	{
		connection.QueryDeleteDirectory(i->first);
		
		// Log this
		::syslog(LOG_INFO, "Deleted %s (ID %08llx) from store root", i->second.c_str(), i->first);
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
	if (mpExcludeFiles)
	{
		delete mpExcludeFiles;
		mpExcludeFiles = NULL;
	}
	if (mpExcludeDirs)
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

	if (aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if (aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		BackupClientDirectoryRecord *pSubRecord = new BackupClientDirectoryRecord(0, "");
		if (!pSubRecord)
			throw std::bad_alloc();

		mpDirectoryRecord.reset(pSubRecord);
		mpDirectoryRecord->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(CommonException, Internal)
	}

	//
	//
	//
	rArchive.Read(aMagicMarker);

	if (aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if (aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		mpExcludeFiles = new ExcludeList;
		if (!mpExcludeFiles)
			throw std::bad_alloc();

		mpExcludeFiles->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(CommonException, Internal)
	}

	//
	//
	//
	rArchive.Read(aMagicMarker);

	if (aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if (aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		mpExcludeDirs = new ExcludeList;
		if (!mpExcludeDirs)
			throw std::bad_alloc();

		mpExcludeDirs->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(CommonException, Internal)
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
	if (mpDirectoryRecord.get() == NULL)
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
	if (!mpExcludeFiles)
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
	if (!mpExcludeDirs)
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
//		Name:    BackupDaemon::SerializeStoreObjectInfo(int64_t aClientStoreMarker, box_time_t theLastSyncTime, box_time_t theNextSyncTime)
//		Purpose: Serializes remote directory and file information into a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------

static const int STOREOBJECTINFO_MAGIC_ID_VALUE = 0x7777525F;
static const std::string STOREOBJECTINFO_MAGIC_ID_STRING = "BBACKUPD-STATE";
static const int STOREOBJECTINFO_VERSION = 1;

bool BackupDaemon::SerializeStoreObjectInfo(int64_t aClientStoreMarker, box_time_t theLastSyncTime, box_time_t theNextSyncTime) const
{
	if(!GetConfiguration().KeyExists("StoreObjectInfoFile"))
	{
		return false;
	}

	std::string StoreObjectInfoFile = 
		GetConfiguration().GetKeyValue("StoreObjectInfoFile");

	if (StoreObjectInfoFile.size() <= 0)
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
		anArchive.Write(aClientStoreMarker);
		anArchive.Write(theLastSyncTime);
		anArchive.Write(theNextSyncTime);

		//
		//
		//
		int64_t iCount = mLocations.size();
		anArchive.Write(iCount);

		for (int v = 0; v < iCount; v++)
		{
			ASSERT(mLocations[v]);
			mLocations[v]->Serialize(anArchive);
		}

		//
		//
		//
		iCount = mIDMapMounts.size();
		anArchive.Write(iCount);

		for (int v = 0; v < iCount; v++)
			anArchive.Write(mIDMapMounts[v]);

		//
		//
		//
		aFile.Close();
		::syslog(LOG_INFO, "Saved store object info file '%s'", 
			StoreObjectInfoFile.c_str());
	}
	catch (...)
	{
		::syslog(LOG_WARNING, "Requested store object info file '%s' "
			"not accessible or could not be created", 
			StoreObjectInfoFile.c_str());
	}

	return created;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::DeserializeStoreObjectInfo(int64_t & aClientStoreMarker, box_time_t & theLastSyncTime, box_time_t & theNextSyncTime)
//		Purpose: Deserializes remote directory and file information from a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
bool BackupDaemon::DeserializeStoreObjectInfo(int64_t & aClientStoreMarker, box_time_t & theLastSyncTime, box_time_t & theNextSyncTime)
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

	if (StoreObjectInfoFile.size() <= 0)
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

		if (iMagicValue != STOREOBJECTINFO_MAGIC_ID_VALUE)
		{
			::syslog(LOG_WARNING, "Store object info file '%s' "
				"is not a valid or compatible serialised "
				"archive. Will re-cache from store.", 
				StoreObjectInfoFile.c_str());
			return false;
		}

		//
		// get a bit optimistic and read in a string identifier
		//
		std::string strMagicValue;
		anArchive.Read(strMagicValue);

		if (strMagicValue != STOREOBJECTINFO_MAGIC_ID_STRING)
		{
			::syslog(LOG_WARNING, "Store object info file '%s' "
				"is not a valid or compatible serialised "
				"archive. Will re-cache from store.", 
				StoreObjectInfoFile.c_str());
			return false;
		}

		//
		// check if we are loading some future format
		// version by mistake
		//
		int iVersion = 0;
		anArchive.Read(iVersion);

		if (iVersion != STOREOBJECTINFO_VERSION)
		{
			::syslog(LOG_WARNING, "Store object info file '%s' "
				"version %d unsupported. "
				"Will re-cache from store.", 
				StoreObjectInfoFile.c_str(), 
				iVersion);
			return false;
		}

		//
		// check if this state file is even valid 
		// for the loaded bbackupd.conf file
		//
		box_time_t lastKnownConfigModTime;
		anArchive.Read(lastKnownConfigModTime);

		if (lastKnownConfigModTime != GetLoadedConfigModifiedTime())
		{
			::syslog(LOG_WARNING, "Store object info file '%s' "
				"out of date. Will re-cache from store", 
				StoreObjectInfoFile.c_str());
			return false;
		}

		//
		// this is it, go at it
		//
		anArchive.Read(aClientStoreMarker);
		anArchive.Read(theLastSyncTime);
		anArchive.Read(theNextSyncTime);

		//
		//
		//
		int64_t iCount = 0;
		anArchive.Read(iCount);

		for (int v = 0; v < iCount; v++)
		{
			Location* pLocation = new Location;
			if (!pLocation)
				throw std::bad_alloc();

			pLocation->Deserialize(anArchive);
			mLocations.push_back(pLocation);
		}

		//
		//
		//
		iCount = 0;
		anArchive.Read(iCount);

		for (int v = 0; v < iCount; v++)
		{
			std::string strItem;
			anArchive.Read(strItem);

			mIDMapMounts.push_back(strItem);
		}

		//
		//
		//
		aFile.Close();
		::syslog(LOG_INFO, "Loaded store object info file '%s', "
			"version [%d]", StoreObjectInfoFile.c_str(), 
			iVersion);

		return true;
	}
	catch (...)
	{
		DeleteAllLocations();

		aClientStoreMarker = 
			BackupClientContext::ClientStoreMarker_NotKnown;
		theLastSyncTime = 0;
		theNextSyncTime = 0;

		::syslog(LOG_WARNING, "Requested store object info file '%s' "
			"does not exist, not accessible, or inconsistent. "
			"Will re-cache from store.", 
			StoreObjectInfoFile.c_str());
	}

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
		// File doesn't exist -- so can't be deleted. But something isn't quite right, so log a message
		::syslog(LOG_ERR, "Expected to be able to delete "
			"store object info file '%s', but the file did not exist.",
			storeObjectInfoFile.c_str());
		// Return true to stop things going around in a loop
		return true;
	}

	// Actually delete it
	if(::unlink(storeObjectInfoFile.c_str()) != 0)
	{
		::syslog(LOG_ERR, "Failed to delete the old "
			"store object info file '%s': %s",
			storeObjectInfoFile.c_str(), strerror(errno));
		return false;
	}

	return true;
}
