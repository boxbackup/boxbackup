// --------------------------------------------------------------------------
//
// File
//		Name:    CommandSocketManager.cpp
//		Purpose: Implementation for command socket management interface
//		Created: 2005/04/08
//
// --------------------------------------------------------------------------

#ifndef WIN32
	#include <syslog.h>
#endif

#include "Box.h"
#include "IOStreamGetLine.h"
#include "CommandSocketManager.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    CommandSocketManager::CommandSocketManager()
//		Purpose: Constructor
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
CommandSocketManager::CommandSocketManager(
	const Configuration& rConf,
	CommandListener* pListener, 
	const char * pSocketName)
	: mpGetLine(0),
	  mConf(rConf),
	  mState(State_Initialising)
{
	mpListener = pListener;
	::unlink(pSocketName);
	mListeningSocket.Listen(Socket::TypeUNIX, pSocketName);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::CommandSocketInfo::~CommandSocketInfo()
//		Purpose: Destructor
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
CommandSocketManager::~CommandSocketManager()
{
	if (mpConnectedSocket.get() != 0)
	{
		CloseConnection();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    CommandSocketManager::Wait(box_time_t, bool &, bool &)
//		Purpose: Waits on a the command socket for a time of UP TO the required time
//				 but may be much less, and handles a command if necessary.
//		Created: 18/2/04
//
// --------------------------------------------------------------------------

void CommandSocketManager::Wait(box_time_t RequiredDelay)
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
	
	TRACE1("Wait on command socket, delay = %lld\n", RequiredDelay);
	
	try
	{
		// Timeout value for connections and things
		int timeout = ((int)BoxTimeToMilliSeconds(RequiredDelay)) + 1;
		// Handle bad boundary cases
		if(timeout <= 0) timeout = 1;
		if(timeout == INFTIM) timeout = 100000;

		// Wait for socket connection, or handle a command?
		if(mpConnectedSocket.get() == 0)
		{
			// No connection, listen for a new one
			mpConnectedSocket.reset(mListeningSocket.Accept(timeout).release());
			
			if(mpConnectedSocket.get() == 0)
			{
				// If a connection didn't arrive, there was a timeout, 
				// which means we've waited long enough and it's time to go.
				return;
			}
			else
			{
#ifdef PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
				bool uidOK = true;
				::syslog(LOG_WARNING, "On this platform, "
					"no security check can be made on the "
					"credentials of peers connecting to "
					"the command socket. (bbackupctl)");
#else
				// Security check -- does the process connecting 
				// to this socket have the same UID as this process?
				bool uidOK = false;
				// BLOCK
				{
					uid_t remoteEUID = 0xffff;
					gid_t remoteEGID = 0xffff;
					if(mpConnectedSocket->GetPeerCredentials(
						remoteEUID, remoteEGID))
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
					::syslog(LOG_ERR, "Incoming command connection from peer "
						"had different user ID than this process, or security "
						"check could not be completed.");
					mpConnectedSocket.reset();
					return;
				}
				else
				{
					// Log
					::syslog(LOG_INFO, "Connection from command socket");
					
					// Send a header line summarising the configuration and current state
					char summary[256];
					int summarySize = sprintf(summary, "bbackupd: %d %d %d %d\nstate %d\n",
						mConf.GetKeyValueBool("AutomaticBackup"),
						mConf.GetKeyValueInt("UpdateStoreInterval"),
						mConf.GetKeyValueInt("MinimumFileAge"),
						mConf.GetKeyValueInt("MaxUploadWait"),
						mState);
					mpConnectedSocket->Write(summary, summarySize);
					
					// Set the timeout to something very small, so we don't wait too long on waiting
					// for any incoming data
					timeout = 10; // milliseconds
				}
			}
		}

		// So there must be a connection now.
		ASSERT(mpConnectedSocket.get() != 0);
		
		// Is there a getline object ready?
		if(mpGetLine == 0)
		{
			// Create a new one
			mpGetLine = new IOStreamGetLine(*(mpConnectedSocket.get()));
		}
		
		// Ping the remote side, to provide errors which will mean the socket gets closed
		// Don't do this if the timeout requested was zero, as we don't want
		// to flood the connection during background polling
		if (RequiredDelay > 0)
		{
			mpConnectedSocket->Write("ping\n", 5);
		}
		
		// Wait for a command or something on the socket
		std::string command;
		while(mpGetLine != 0 && !mpGetLine->IsEOF()
			&& mpGetLine->GetLine(command, false /* no preprocessing */, timeout))
		{
			TRACE1("Receiving command '%s' over command socket\n", command.c_str());
			
			bool sendOK = false;
			bool sendResponse = true;
		
			// Command to process!
			if(command == "quit" || command == "")
			{
				// Close the socket.
				CloseConnection();
				sendResponse = false;
			}
			else if(command == "sync")
			{
				// Sync now!
				mpListener->SetSyncRequested();
				sendOK = true;
			}
			else if(command == "force-sync")
			{
				// Sync now (forced -- overrides any SyncAllowScript)
				mpListener->SetSyncForced();
				sendOK = true;
			}
			else if(command == "reload")
			{
				// Reload the configuration
				mpListener->SetReloadConfigWanted();
				sendOK = true;
			}
			else if(command == "terminate")
			{
				// Terminate the daemon cleanly
				mpListener->SetTerminateWanted();
				sendOK = true;
			}
			
			// Send a response back?
			if(sendResponse)
			{
				mpConnectedSocket->Write(sendOK?"ok\n":"error\n", sendOK?3:6);
			}
			
			// Set timeout to something very small, 
			// so this just checks for data which is waiting
			timeout = 1;
		}
		
		// Close on EOF?
		if(mpGetLine != 0 && mpGetLine->IsEOF())
		{
			CloseConnection();
		}
	}
	catch(...)
	{
		// If an error occurs, and there is a connection active, just close that
		// connection and continue. Otherwise, let the error propagate.
		if(mpConnectedSocket.get() == 0)
		{
			throw;
		}
		else
		{
			// Close socket and ignore error
			CloseConnection();
		}
	}
#endif // WIN32
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CommandSocketManager::CloseConnection()
//		Purpose: Close the command connection, ignoring any errors
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
void CommandSocketManager::CloseConnection()
{
#ifndef WIN32
	try
	{
		TRACE0("Closing command connection\n");
		
		if(mpGetLine)
		{
			delete mpGetLine;
			mpGetLine = 0;
		}
		mpConnectedSocket.reset();
	}
	catch(...)
	{
		// Ignore any errors
	}
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CommandSocketManager::SendSyncStartOrFinish(bool sendStart)
//		Purpose: Send a message to any connected client when a sync starts or
//			finishes.
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
void CommandSocketManager::SendSyncStartOrFinish(bool SendStart)
{
	// The bbackupctl program can't rely on a state change, because it may never
	// change if the server doesn't need to be contacted.

#ifdef __MINGW32__
#warning race condition: what happens if socket is closed?
#endif

#ifdef WIN32
	if (mListeningSocket.IsConnected())
#else
	if (mpConnectedSocket.get() != 0)
#endif
	{
		const char* message = SendStart ? "start-sync\n" : "finish-sync\n";
		try
		{
#ifdef WIN32
			mListeningSocket.Write(message, (int)strlen(message));
#else
			mpConnectedSocket->Write(message, strlen(message));
#endif
		}
		catch(...)
		{
			CloseConnection();
		}
	}
}

void CommandSocketManager::SendStateUpdate(state_t State)
{
	mState = State;

	// If there's a command socket connected, then inform it -- 
	// disconnecting from the command socket if there's an error

	char newState[64];
	char newStateSize = sprintf(newState, "state %d\n", State);

#ifdef WIN32
	#ifndef _MSC_VER
		#warning FIX ME: race condition
	#endif

	// what happens if the socket is closed by the other thread before
	// we can write to it? Null pointer deref at best.
	if (mListeningSocket.IsConnected())
#else
	if (mpConnectedSocket.get() != 0)
#endif
	{
		try
		{
#ifdef WIN32
			mListeningSocket.Write(newState, newStateSize);
#else
			mpConnectedSocket->Write(newState, newStateSize);
#endif
		}
		catch(...)
		{
			CloseConnection();
		}
	}
}
