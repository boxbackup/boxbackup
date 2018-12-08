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

#include "BackupConstants.h"
#include "BoxPortsAndFiles.h"
#include "BoxTime.h"
#include "BackupClientContext.h"
#include "ConfiguredBackupClient.h"
#include "SocketStreamTLS.h"
#include "Socket.h"
#include "BackupStoreConstants.h"
#include "BackupStoreException.h"
#include "BackupDaemon.h"
#include "autogen_BackupProtocol.h"
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
	const Configuration& rConfig,
	LocationResolver &rResolver,
	ProgressNotifier& rProgressNotifier
)
: mrConfig(rConfig),
  mrResolver(rResolver),
  mrProgressNotifier(rProgressNotifier),
  mClientStoreMarker(ClientStoreMarker::NotKnown),
  mpDeleteList(NULL),
  mpCurrentIDMap(NULL),
  mpNewIDMap(NULL),
  mStorageLimitExceeded(false),
  mpExcludeFiles(NULL),
  mpExcludeDirs(NULL),
  mKeepAliveTimer(0, "KeepAliveTime"),
  mbIsManaged(false),
  mKeepAliveTime(0),
  mMaximumDiffingTime(0)
{ }

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
	try
	{
		// Quit nicely:
		CloseAnyOpenConnection();
	}
	catch(BoxException &e)
	{
		if(EXCEPTION_IS_TYPE(e, ConnectionException, TLSReadFailed))
		{
			// Since we are closing anyway, we don't care if the connection was
			// unusable, and in fact we may be closing it in response to having
			// tried and failed to use it anyway, so don't rethrow.
		}
		else
		{
			// Do not throw exceptions in destructors, regardless!
			BOX_WARNING("Caught exception while cleaning up BackupClientContext: " <<
				e.what());
		}
	}
	
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
BackupProtocolCallable &BackupClientContext::GetConnection()
{
	// Already got it? Just return it.
	if(mapConnection.get())
	{
		return *mapConnection;
	}

	// Defensive. Must close connection before releasing any old socket.
	mapConnection.reset();

	try
	{
		mapConnection = GetConfiguredBackupClient(mrConfig, false); // !read_only

		// Login -- if this fails, the Protocol will exception
		BackupProtocolLoginConfirmed& login_conf(mapConnection->GetLoginConfirmed());

		// If reconnecting, check that the client store marker is the one we expect:
		if(mClientStoreMarker != ClientStoreMarker::NotKnown)
		{
			if(login_conf.GetClientStoreMarker() != mClientStoreMarker)
			{
				// Not good... finish the connection, abort, etc, ignoring errors
				try
				{
					mapConnection->QueryFinished();
				}
				catch(...)
				{
					// IGNORE
				}

				// Then throw an exception about this
				THROW_EXCEPTION_MESSAGE(BackupStoreException,
					ClientMarkerNotAsExpected,
					"Expected " << mClientStoreMarker <<
					" but found " << login_conf.GetClientStoreMarker() <<
					": is someone else writing to the "
					"same account?");
			}
		}
		else // mClientStoreMarker == ClientStoreMarker::NotKnown
		{
			// Yes, choose one, the current time will do
			box_time_t marker = GetCurrentBoxTime();
			
			// Set it on the store
			mapConnection->QuerySetClientStoreMarker(marker);
			
			// Record it so that it can be picked up later.
			mClientStoreMarker = marker;
		}

		// Log success
		BOX_INFO("Connection made, login successful");

		// Check to see if there is any space available on the server
		if(login_conf.GetBlocksUsed() >= login_conf.GetBlocksHardLimit())
		{
			// no -- flag so only things like deletions happen
			mStorageLimitExceeded = true;
			BOX_WARNING("Exceeded storage hard-limit on server, "
				"not uploading changes to files");
		}
	}
	catch(...)
	{
		// Clean up.
		mapConnection.reset();
		throw;
	}
	
	return *mapConnection;
}

BackupProtocolCallable* BackupClientContext::GetOpenConnection() const
{
	return mapConnection.get();
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
	// Delete any pending list
	if(mpDeleteList != 0)
	{
		delete mpDeleteList;
		mpDeleteList = 0;
	}

	BackupProtocolCallable* pConnection(GetOpenConnection());
	if(pConnection)
	{
		try
		{
			// Quit nicely:
			pConnection->QueryFinished();
		}
		catch(...)
		{
			// Close the connection anyway:
			mapConnection.reset();
			throw;
		}

		// Close the connection
		mapConnection.reset();
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
	BackupProtocolCallable* pConnection(GetOpenConnection());
	if(pConnection)
	{
		return pConnection->GetTimeout();
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
	BackupProtocolCallable &connection(GetConnection());

	// Request filenames from the server, in a "safe" manner to ignore errors properly
	{
		BackupProtocolGetObjectName send(ObjectID, ContainingDirectory);
		connection.Send(send);
	}
	std::auto_ptr<BackupProtocolMessage> preply(connection.Receive());

	// Is it of the right type?
	if(preply->GetType() != BackupProtocolObjectName::TypeID)
	{
		// Was an error or something
		return false;
	}

	// Cast to expected type.
	BackupProtocolObjectName *names = (BackupProtocolObjectName *)(preply.get());

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
			if(!mrResolver.FindLocationPathName(elementName.GetClearFilename(), locPath))
			{
				// Didn't find the location... so can't give the local filename
				return false;
			}

			// Add in location path
			path = (path.empty())?(locPath):(locPath + DIRECTORY_SEPARATOR_ASCHAR + path);
		}
	}

	// Is it a directory?
	rIsDirectoryOut = ((names->GetFlags() & BackupProtocolListDirectory::Flags_Dir) == BackupProtocolListDirectory::Flags_Dir);
	
	// Is it the current version?
	rIsCurrentVersionOut = ((names->GetFlags() & (BackupProtocolListDirectory::Flags_OldVersion | BackupProtocolListDirectory::Flags_Deleted)) == 0);

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
	mKeepAliveTimer.Reset(mKeepAliveTime * MILLI_SEC_IN_SEC);
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
	BackupProtocolCallable* pConnection(GetOpenConnection());
	if (!pConnection)
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
	pConnection->QueryGetIsAlive();

	mKeepAliveTimer.Reset(mKeepAliveTime * MILLI_SEC_IN_SEC);
}

int BackupClientContext::GetMaximumDiffingTime()
{
	return mMaximumDiffingTime;
}
