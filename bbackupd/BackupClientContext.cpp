// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientContext.cpp
//		Purpose: Keep track of context
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif
#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif
#ifdef HAVE_SYS_TIME_H
	#include <sys/time.h>
#endif

#include "BoxPortsAndFiles.h"
#include "BoxTime.h"
#include "BackupClientContext.h"
#include "SocketStreamTLS.h"
#include "Socket.h"
#include "BackupStoreConstants.h"
#include "BackupStoreException.h"
#include "BackupDaemon.h"
#include "autogen_BackupProtocolClient.h"
#include "BackupStoreFile.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::BackupClientContext(BackupDaemon &, TLSContext &, const std::string &, int32_t, bool)
//		Purpose: Constructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupClientContext::BackupClientContext(BackupDaemon &rDaemon, TLSContext &rTLSContext, const std::string &rHostname,
			int32_t AccountNumber, bool ExtendedLogging)
	: mrDaemon(rDaemon),
	  mrTLSContext(rTLSContext),
	  mHostname(rHostname),
	  mAccountNumber(AccountNumber),
	  mpSocket(0),
	  mpConnection(0),
	  mExtendedLogging(ExtendedLogging),
	  mClientStoreMarker(ClientStoreMarker_NotKnown),
	  mpDeleteList(0),
	  mpCurrentIDMap(0),
	  mpNewIDMap(0),
	  mStorageLimitExceeded(false),
	  mpExcludeFiles(0),
	  mpExcludeDirs(0),
	  mbIsManaged(false),
	  mTimeMgmtEpoch(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::~BackupClientContext()
//		Purpose: Destructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupClientContext::~BackupClientContext()
{
	CloseAnyOpenConnection();
	
	// Delete delete list
	if(mpDeleteList != 0)
	{
		delete mpDeleteList;
		mpDeleteList = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::GetConnection()
//		Purpose: Returns the connection, making the connection and logging into
//				 the backup store if necessary.
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupProtocolClient &BackupClientContext::GetConnection()
{
	// Already got it? Just return it.
	if(mpConnection != 0)
	{
		return *mpConnection;
	}
	
	// Get a socket connection
	if(mpSocket == 0)
	{
		mpSocket = new SocketStreamTLS;
		ASSERT(mpSocket != 0);	// will have exceptioned if this was a problem
	}
	
	try
	{
		// Defensive.
		if(mpConnection != 0)
		{
			delete mpConnection;
			mpConnection = 0;
		}
		
		// Log intention
		::syslog(LOG_INFO, "Opening connection to server %s...", mHostname.c_str());

		// Connect!
		mpSocket->Open(mrTLSContext, Socket::TypeINET, mHostname.c_str(), BOX_PORT_BBSTORED);
		
		// And create a procotol object
		mpConnection = new BackupProtocolClient(*mpSocket);
		
		// Set logging option
		mpConnection->SetLogToSysLog(mExtendedLogging);
		
		// Handshake
		mpConnection->Handshake();
		
		// Check the version of the server
		{
			std::auto_ptr<BackupProtocolClientVersion> serverVersion(mpConnection->QueryVersion(BACKUP_STORE_SERVER_VERSION));
			if(serverVersion->GetVersion() != BACKUP_STORE_SERVER_VERSION)
			{
				THROW_EXCEPTION(BackupStoreException, WrongServerVersion)
			}
		}

		// Login -- if this fails, the Protocol will exception
		std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(mpConnection->QueryLogin(mAccountNumber, 0 /* read/write */));
		
		// Check that the client store marker is the one we expect
		if(mClientStoreMarker != ClientStoreMarker_NotKnown)
		{
			if(loginConf->GetClientStoreMarker() != mClientStoreMarker)
			{
				// Not good... finish the connection, abort, etc, ignoring errors
				try
				{
					mpConnection->QueryFinished();
					mpSocket->Shutdown();
					mpSocket->Close();
				}
				catch(...)
				{
					// IGNORE
				}
				
				// Then throw an exception about this
				THROW_EXCEPTION(BackupStoreException, ClientMarkerNotAsExpected)
			}
		}
		
		// Log success
		::syslog(LOG_INFO, "Connection made, login successful");

		// Check to see if there is any space available on the server
		int64_t softLimit = loginConf->GetBlocksSoftLimit();
		int64_t hardLimit = loginConf->GetBlocksHardLimit();
		// Threshold for uploading new stuff
		int64_t stopUploadThreshold = softLimit + ((hardLimit - softLimit) / 3);
		if(loginConf->GetBlocksUsed() > stopUploadThreshold)
		{
			// no -- flag so only things like deletions happen
			mStorageLimitExceeded = true;
			// Log
			::syslog(LOG_WARNING, "Exceeded storage limits on server -- not uploading changes to files");
		}
	}
	catch(...)
	{
		// Clean up.
		delete mpConnection;
		mpConnection = 0;
		delete mpSocket;
		mpSocket = 0;
		throw;
	}
	
	return *mpConnection;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::CloseAnyOpenConnection()
//		Purpose: Closes a connection, if it's open
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
void BackupClientContext::CloseAnyOpenConnection()
{
	if(mpConnection)
	{
		try
		{
			// Need to set a client store marker?
			if(mClientStoreMarker == ClientStoreMarker_NotKnown)
			{
				// Yes, choose one, the current time will do
				box_time_t marker = GetCurrentBoxTime();
				
				// Set it on the store
				mpConnection->QuerySetClientStoreMarker(marker);
				
				// Record it so that it can be picked up later.
				mClientStoreMarker = marker;
			}
		
			// Quit nicely
			mpConnection->QueryFinished();
		}
		catch(...)
		{
			// Ignore errors here
		}
		
		// Delete it anyway.
		delete mpConnection;
		mpConnection = 0;
	}
	
	if(mpSocket)
	{
		try
		{
			// Be nice about closing the socket
			mpSocket->Shutdown();
			mpSocket->Close();
		}
		catch(...)
		{
			// Ignore errors
		}
		
		// Delete object
		delete mpSocket;
		mpSocket = 0;
	}

	// Delete any pending list
	if(mpDeleteList != 0)
	{
		delete mpDeleteList;
		mpDeleteList = 0;
	}
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::GetTimeout()
//		Purpose: Gets the current timeout time.
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
int BackupClientContext::GetTimeout() const
{
	if(mpConnection)
	{
		return mpConnection->GetTimeout();
	}
	
	return (15*60*1000);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::GetDeleteList()
//		Purpose: Returns the delete list, creating one if necessary
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
BackupClientDeleteList &BackupClientContext::GetDeleteList()
{
	// Already created?
	if(mpDeleteList == 0)
	{
		mpDeleteList = new BackupClientDeleteList;
	}

	// Return reference to object	
	return *mpDeleteList;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    
//		Purpose: 
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
void BackupClientContext::PerformDeletions()
{
	// Got a list?
	if(mpDeleteList == 0)
	{
		// Nothing to do
		return;
	}
	
	// Delegate to the delete list object
	mpDeleteList->PerformDeletions(*this);
	
	// Delete the object
	delete mpDeleteList;
	mpDeleteList = 0;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::GetCurrentIDMap() const
//		Purpose: Return a (const) reference to the current ID map
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
const BackupClientInodeToIDMap &BackupClientContext::GetCurrentIDMap() const
{
	ASSERT(mpCurrentIDMap != 0);
	if(mpCurrentIDMap == 0)
	{
		THROW_EXCEPTION(CommonException, Internal)
	}
	return *mpCurrentIDMap;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::GetNewIDMap() const
//		Purpose: Return a reference to the new ID map
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
BackupClientInodeToIDMap &BackupClientContext::GetNewIDMap() const
{
	ASSERT(mpNewIDMap != 0);
	if(mpNewIDMap == 0)
	{
		THROW_EXCEPTION(CommonException, Internal)
	}
	return *mpNewIDMap;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::FindFilename(int64_t, int64_t, std::string &, bool &) const
//		Purpose: Attempts to find the pathname of an object with a given ID on the server.
//				 Returns true if it can be found, in which case rPathOut is the local filename,
//				 and rIsDirectoryOut == true if the local object is a directory.
//		Created: 12/11/03
//
// --------------------------------------------------------------------------
bool BackupClientContext::FindFilename(int64_t ObjectID, int64_t ContainingDirectory, std::string &rPathOut, bool &rIsDirectoryOut,
	bool &rIsCurrentVersionOut, box_time_t *pModTimeOnServer, box_time_t *pAttributesHashOnServer, BackupStoreFilenameClear *pLeafname)
{
	// Make a connection to the server
	BackupProtocolClient &connection(GetConnection());

	// Request filenames from the server, in a "safe" manner to ignore errors properly
	{
		BackupProtocolClientGetObjectName send(ObjectID, ContainingDirectory);
		connection.Send(send);
	}
	std::auto_ptr<BackupProtocolObjectCl> preply(connection.Receive());

	// Is it of the right type?
	if(preply->GetType() != BackupProtocolClientObjectName::TypeID)
	{
		// Was an error or something
		return false;
	}

	// Cast to expected type.
	BackupProtocolClientObjectName *names = (BackupProtocolClientObjectName *)(preply.get());

	// Anything found?
	int32_t numElements = names->GetNumNameElements();
	if(numElements <= 0)
	{
		// No.
		return false;
	}
	
	// Get the stream containing all the names
	std::auto_ptr<IOStream> nameStream(connection.ReceiveStream());

	// Path
	std::string path;

	// Remember this is in reverse order!
	for(int l = 0; l < numElements; ++l)
	{
		BackupStoreFilenameClear elementName;
		elementName.ReadFromStream(*nameStream, GetTimeout());

		// Store leafname for caller?
		if(l == 0 && pLeafname)
		{
			*pLeafname = elementName;
		}

		// Is it part of the filename in the location?
		if(l < (numElements - 1))
		{
			// Part of filename within
			path = (path.empty())?(elementName.GetClearFilename()):(elementName.GetClearFilename() + DIRECTORY_SEPARATOR_ASCHAR + path);
		}
		else
		{
			// Location name -- look up in daemon's records
			std::string locPath;
			if(!mrDaemon.FindLocationPathName(elementName.GetClearFilename(), locPath))
			{
				// Didn't find the location... so can't give the local filename
				return false;
			}

			// Add in location path
			path = (path.empty())?(locPath):(locPath + DIRECTORY_SEPARATOR_ASCHAR + path);
		}
	}

	// Is it a directory?
	rIsDirectoryOut = ((names->GetFlags() & BackupProtocolClientListDirectory::Flags_Dir) == BackupProtocolClientListDirectory::Flags_Dir);
	
	// Is it the current version?
	rIsCurrentVersionOut = ((names->GetFlags() & (BackupProtocolClientListDirectory::Flags_OldVersion | BackupProtocolClientListDirectory::Flags_Deleted)) == 0);

	// And other information which may be required
	if(pModTimeOnServer) *pModTimeOnServer = names->GetModificationTime();
	if(pAttributesHashOnServer) *pAttributesHashOnServer = names->GetAttributesHash();

	// Tell caller about the pathname
	rPathOut = path;

	// Found
	return true;
}


// maximum time to spend diffing
static int sMaximumDiffTime = 600;
// maximum time of SSL inactivity (keep-alive interval)
static int sKeepAliveTime = 0;

void BackupClientContext::SetMaximumDiffingTime(int iSeconds)
{
	sMaximumDiffTime = iSeconds < 0 ? 0 : iSeconds;
	TRACE1("Set maximum diffing time to %d seconds\n", sMaximumDiffTime);
}

void BackupClientContext::SetKeepAliveTime(int iSeconds)
{
	sKeepAliveTime = iSeconds < 0 ? 0 : iSeconds;
	TRACE1("Set keep-alive time to %d seconds\n", sKeepAliveTime);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    static TimerSigHandler(int)
//		Purpose: Signal handler
//		Created: 19/3/04
//
// --------------------------------------------------------------------------
static void TimerSigHandler(int iUnused)
{
	BackupStoreFile::DiffTimerExpired();	
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::ManageDiffProcess()
//		Purpose: Initiates a file diff control timer
//		Created: 04/19/2005
//
// --------------------------------------------------------------------------
void BackupClientContext::ManageDiffProcess()
{
	if (mbIsManaged || !mpConnection)
		return;

	ASSERT(mTimeMgmtEpoch == 0);

#ifdef PLATFORM_CYGWIN
	::signal(SIGALRM, TimerSigHandler);
#elif defined WIN32
	// no support for SIGVTALRM
	SetTimerHandler(TimerSigHandler);
#else
	::signal(SIGVTALRM, TimerSigHandler);
#endif // PLATFORM_CYGWIN

	struct itimerval timeout;
	memset(&timeout, 0, sizeof(timeout));

	//
	//
	//
	if (sMaximumDiffTime <= 0 && sKeepAliveTime <= 0)
	{
		TRACE0("Diff control not requested - letting things run wild\n");
		return;
	}
	else if (sMaximumDiffTime > 0 && sKeepAliveTime > 0)
	{
		timeout.it_value.tv_sec = sKeepAliveTime < sMaximumDiffTime ? sKeepAliveTime : sMaximumDiffTime;
		timeout.it_interval.tv_sec = sKeepAliveTime < sMaximumDiffTime ? sKeepAliveTime : sMaximumDiffTime;
	}
	else
	{
		timeout.it_value.tv_sec = sKeepAliveTime > 0 ? sKeepAliveTime : sMaximumDiffTime;
		timeout.it_interval.tv_sec = sKeepAliveTime > 0 ? sKeepAliveTime : sMaximumDiffTime;
	}

	// avoid race
	mTimeMgmtEpoch = time(NULL);

#ifdef PLATFORM_CYGWIN
	if(::setitimer(ITIMER_REAL, &timeout, NULL) != 0)
#else
	if(::setitimer(ITIMER_VIRTUAL, &timeout, NULL) != 0)
#endif // PLATFORM_CYGWIN
	{
		mTimeMgmtEpoch = 0;

		TRACE0("WARNING: couldn't set file diff control timeout\n");
		THROW_EXCEPTION(BackupStoreException, Internal)
	}

	mbIsManaged = true;
	TRACE0("Initiated timer for file diff control\n");
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::UnManageDiffProcess()
//		Purpose: suspends file diff control timer
//		Created: 04/19/2005
//
// --------------------------------------------------------------------------
void BackupClientContext::UnManageDiffProcess()
{
	if (!mbIsManaged /* don't test for active connection, just do it */)
		return;

	struct itimerval timeout;
	memset(&timeout, 0, sizeof(timeout));

#ifdef PLATFORM_CYGWIN
	if(::setitimer(ITIMER_REAL, &timeout, NULL) != 0)
#else
	if(::setitimer(ITIMER_VIRTUAL, &timeout, NULL) != 0)
#endif // PLATFORM_CYGWIN
	{
		TRACE0("WARNING: couldn't clear file diff control timeout\n");
		THROW_EXCEPTION(BackupStoreException, Internal)
	}

	mbIsManaged = false;
	mTimeMgmtEpoch = 0;

	TRACE0("Suspended timer for file diff control\n");
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::DoKeepAlive()
//		Purpose: Does something inconsequential over the SSL link to keep it up
//		Created: 04/19/2005
//
// --------------------------------------------------------------------------
void BackupClientContext::DoKeepAlive()
{
	if (!mpConnection)
	{
		::syslog(LOG_ERR, "DoKeepAlive() called with no connection!");
		return;
	}

	mpConnection->QueryGetIsAlive();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::GetTimeMgmtEpoch()
//		Purpose: Returns the unix time when the diff was started, or zero
//				 if the diff process is unmanaged.
//		Created: 04/19/2005
//
// --------------------------------------------------------------------------
time_t BackupClientContext::GetTimeMgmtEpoch() 
{
	return mTimeMgmtEpoch;
}

int BackupClientContext::GetMaximumDiffingTime() 
{
	return sMaximumDiffTime;
}

int BackupClientContext::GetKeepaliveTime() 
{
	return sKeepAliveTime;
}
