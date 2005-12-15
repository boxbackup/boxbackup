// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    CommandSocketManager.cpp
//		Purpose: Implementation for command socket management interface
//		Created: 2005/04/08
//
// --------------------------------------------------------------------------

#include <syslog.h>

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
	// Another thread is listening on the command socket, 
	// no need for us to do anything here.
	DWORD timeout = BoxTimeToMilliSeconds(RequiredDelay);

	if (timeout > 0)
	{
		Sleep(timeout);
	}
#else // !WIN32
	TRACE1("Wait on command socket, delay = %lld\n", RequiredDelay);
	
	try
	{
		// Timeout value for connections and things
		int timeout = ((int)BoxTimeToMilliSeconds(RequiredDelay));
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
				::syslog(LOG_ERR, "On this platform, no security check "
					"can be made on the credientials of peers connecting "
					"to the command socket. (bbackupctl)");
#else
				// Security check -- does the process connecting
				// to this socket have the same UID as this process?
				bool uidOK = false;
				// BLOCK
				{
					uid_t remoteEUID = 0xffff;
					gid_t remoteEGID = 0xffff;
					if(mpConnectedSocket->GetPeerCredentials(remoteEUID, 
						remoteEGID))
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
				
				// Is this an acceptible connection?
				if(!uidOK)
				{
					// Dump the connection
					::syslog(LOG_ERR, "Incoming command connection from peer "
						"had different user ID than this process, or security "
						"check could not be completed.");
					mpConnectedSocket.reset();
					return;
				}

				// Log
				::syslog(LOG_INFO, "Incoming connection to command socket");
				TRACE0("Accepted new command connection\n");
				
				// Send a header line summarising the configuration and current state
				char summary[256];
				int summarySize = sprintf(summary, "bbackupd: %d %d %d %d\n"
					"state %d\n",
					mConf.GetKeyValueBool("AutomaticBackup"),
					mConf.GetKeyValueInt("UpdateStoreInterval"),
					mConf.GetKeyValueInt("MinimumFileAge"),
					mConf.GetKeyValueInt("MaxUploadWait"),
					mState);
				mpConnectedSocket->Write(summary, summarySize);
				
				// Set the timeout to something very small, so we don't
				// spend too long on waiting for any incoming data
				timeout = 10; // milliseconds
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
			mpConnectedSocket->Write("ping\n", 5);
		
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
	try
	{
		TRACE0("Closing command connection\n");
	
#ifdef WIN32
		mListeningSocket.Close();
#else
		if(mpGetLine)
		{
			delete mpGetLine;
			mpGetLine = 0;
		}
		mpConnectedSocket.reset();
#endif
	}
	catch(...)
	{
		// Ignore any errors
	}
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
			mListeningSocket.Write(message, strlen(message));
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

#ifdef WIN32
	#warning FIX ME: race condition
	// what happens if the socket is closed by the other thread before
	// we can write to it? Null pointer deref at best.
	if (mListeningSocket.IsConnected())
		return;
#else	
	if(mpConnectedSocket.get() == 0)
		return;
#endif
	
	// Something connected to the command socket, tell it about the new state
	char newState[64];
	char newStateSize = sprintf(newState, "state %d\n", State);
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
