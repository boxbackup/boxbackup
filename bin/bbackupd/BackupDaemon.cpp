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
#include <unistd.h>

#ifndef WIN32
	#include <signal.h>
	#include <syslog.h>
	#include <sys/param.h>
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
#include "Socket.h"

#include "MemLeakFindOn.h"

#define 	MAX_SLEEP_TIME	((unsigned int)1024)

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
	  mpCommandSocketInfo(0),
	  mDeleteUnusedRootDirEntriesAfter(0)
{
	// Only ever one instance of a daemon
	// SSLLib::Initialise();
	
	// Initialise notifcation sent status
	for(int l = 0; l <= NotifyEvent__MAX; ++l)
	{
		mNotificationsSent[l] = false;
	}
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
	// DeleteAllLocations();
	// DeleteAllIDMaps();
	
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

void ConnectorConnectPipe()
{
	HANDLE SocketHandle = CreateFileW( 
		L"\\\\.\\pipe\\boxbackup",   // pipe name 
		GENERIC_READ |  // read and write access 
		GENERIC_WRITE, 
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,
		0,              // default attributes 
		NULL);          // no template file 

	if (SocketHandle == INVALID_HANDLE_VALUE)
	{
		printf("Connector: Error connecting to named pipe: %d\n", 
			GetLastError());
		return;
	}

	if (!CloseHandle(SocketHandle))
	{
		printf("Connector: CloseHandle failed: %d\n", GetLastError());
	}
}

unsigned int WINAPI ConnectorThread(LPVOID lpParam)
{
	Sleep(1000);

	while (1)
	{
		ConnectorConnectPipe();
		Sleep(1000);
	}

	return 0;
}

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
unsigned int WINAPI HelperThread( LPVOID lpParam ) 
{ 
	printf( "Parameter = %lu.\n", *(DWORD*)lpParam ); 
	((BackupDaemon *)lpParam)->RunHelperThread();

	return 0;
}

void BackupDaemon::RunHelperThread(void)
{
	mpCommandSocketInfo = new CommandSocketInfo;
	this->mReceivedCommandConn = false;

	while ( !IsTerminateWanted() )
	{
		try
		{
			mpCommandSocketInfo->mListeningSocket.Accept(
				BOX_NAMED_PIPE_NAME);
		}
		catch (ConnectionException &e)
		{
			if (e.GetType()    == ConnectionException::ExceptionType &&
			    e.GetSubType() == ConnectionException::SocketConnectError)
			{
				::syslog(LOG_ERR, "Impossible error in "
					"this thread! Aborting.");
				exit(1);
			}
		}

		CloseCommandConnection();
	}
} 

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

	_beginthreadex( 
        	NULL,                        // default security attributes 
        	0,                           // use default stack size  
        	ConnectorThread,             // thread function 
        	this,                        // argument to thread function 
        	0,                           // use default creation flags 
        	NULL);                       // returns the thread identifier 

	// init our own timer for file diff timeouts
	InitTimer();

	// Handle things nicely on exceptions
	try
	{
		Run2();
	}
	catch(...)
	{
		if(mpCommandSocketInfo != 0)
		{
			delete mpCommandSocketInfo;
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

	// clean up windows specific stuff.
	FiniTimer();
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
	// Setup parameters based on type, looking up names if required
	int sockDomain = 0;
	SocketAllAddr addr;
	int addrLen = 0;
	Socket::NameLookupToSockAddr(addr, sockDomain, 
		Socket::TypeINET, "1.2.3.4", 1234, addrLen);

	// Create the socket
	int handle = ::socket(sockDomain, SOCK_STREAM, 
		0 /* let OS choose protocol */);
	if(handle == -1)
	{
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}
	
	// Connect it
	if(::connect(handle, &addr.sa_generic, addrLen) == -1)
	{
		// Dispose of the socket
		::closesocket(handle);
		THROW_EXCEPTION(ConnectionException, Conn_SocketConnectError)
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
		TRACE0("Closing command connection\n");
		
#ifdef WIN32
		mpCommandSocketInfo->mListeningSocket.Close();
#else
		if(mpCommandSocketInfo->mpGetLine)
		{
			delete mpCommandSocketInfo->mpGetLine;
			mpCommandSocketInfo->mpGetLine = 0;
		}
		mpCommandSocketInfo->mpConnectedSocket.reset();
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
