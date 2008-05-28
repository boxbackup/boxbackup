// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientContext.cpp
//		Purpose: Keep track of context
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include "Box.h"

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
#include "Logging.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::BackupClientContext(BackupDaemon &, TLSContext &, const std::string &, int32_t, bool, bool, std::string)
//		Purpose: Constructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupClientContext::BackupClientContext
(
	BackupDaemon &rDaemon, 
	TLSContext &rTLSContext, 
	const std::string &rHostname,
	int Port,
	int32_t AccountNumber, 
	bool ExtendedLogging,
	bool ExtendedLogToFile,
	std::string ExtendedLogFile,
	ProgressNotifier& rProgressNotifier
)
	: mrDaemon(rDaemon),
	  mrTLSContext(rTLSContext),
	  mHostname(rHostname),
	  mPort(Port),
	  mAccountNumber(AccountNumber),
	  mpSocket(0),
	  mpConnection(0),
	  mExtendedLogging(ExtendedLogging),
	  mExtendedLogToFile(ExtendedLogToFile),
	  mExtendedLogFile(ExtendedLogFile),
	  mpExtendedLogFileHandle(NULL),
	  mClientStoreMarker(ClientStoreMarker_NotKnown),
	  mpDeleteList(0),
	  mpCurrentIDMap(0),
	  mpNewIDMap(0),
	  mStorageLimitExceeded(false),
	  mpExcludeFiles(0),
	  mpExcludeDirs(0),
	  mKeepAliveTimer(0),
	  mbIsManaged(false),
	  mrProgressNotifier(rProgressNotifier)
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
		BOX_INFO("Opening connection to server '" <<
			mHostname << "'...");

		// Connect!
		mpSocket->Open(mrTLSContext, Socket::TypeINET,
			mHostname.c_str(), mPort);
		
		// And create a procotol object
		mpConnection = new BackupProtocolClient(*mpSocket);
		
		// Set logging option
		mpConnection->SetLogToSysLog(mExtendedLogging);
		
		if (mExtendedLogToFile)
		{
			ASSERT(mpExtendedLogFileHandle == NULL);
			
			mpExtendedLogFileHandle = fopen(
				mExtendedLogFile.c_str(), "a+");

			if (!mpExtendedLogFileHandle)
			{
				BOX_LOG_SYS_ERROR("Failed to open extended "
					"log file: " << mExtendedLogFile);
			}
			else
			{
				mpConnection->SetLogToFile(mpExtendedLogFileHandle);
			}
		}
		
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
		BOX_INFO("Connection made, login successful");

		// Check to see if there is any space available on the server
		if(loginConf->GetBlocksUsed() >= loginConf->GetBlocksHardLimit())
		{
			// no -- flag so only things like deletions happen
			mStorageLimitExceeded = true;
			// Log
			BOX_WARNING("Exceeded storage hard-limit on server, "
				"not uploading changes to files");
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

	if (mpExtendedLogFileHandle != NULL)
	{
		fclose(mpExtendedLogFileHandle);
		mpExtendedLogFileHandle = NULL;
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
//		Name:    BackupClientContext::PerformDeletions()
//		Purpose: Perform any pending file deletions.
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

void BackupClientContext::SetMaximumDiffingTime(int iSeconds)
{
	mMaximumDiffingTime = iSeconds < 0 ? 0 : iSeconds;
	BOX_TRACE("Set maximum diffing time to " << mMaximumDiffingTime <<
		" seconds");
}

void BackupClientContext::SetKeepAliveTime(int iSeconds)
{
	mKeepAliveTime = iSeconds < 0 ? 0 : iSeconds;
	BOX_TRACE("Set keep-alive time to " << mKeepAliveTime << " seconds");
	mKeepAliveTimer = Timer(mKeepAliveTime);
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
	ASSERT(!mbIsManaged);
	mbIsManaged = true;
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
	// ASSERT(mbIsManaged);
	mbIsManaged = false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientContext::DoKeepAlive()
//		Purpose: Check whether it's time to send a KeepAlive
//			 message over the SSL link, and if so, send it.
//		Created: 04/19/2005
//
// --------------------------------------------------------------------------
void BackupClientContext::DoKeepAlive()
{
	if (!mpConnection)
	{
		return;
	}
	
	if (mKeepAliveTime == 0)
	{
		return;
	}

	if (!mKeepAliveTimer.HasExpired())
	{
		return;
	}
	
	BOX_TRACE("KeepAliveTime reached, sending keep-alive message");
	mpConnection->QueryGetIsAlive();
	
	mKeepAliveTimer = Timer(mKeepAliveTime);
}

int BackupClientContext::GetMaximumDiffingTime() 
{
	return mMaximumDiffingTime;
}
