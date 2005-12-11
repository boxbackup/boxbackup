// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemon.cpp
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#define THROW_EXCEPTION(type, subtype)	\
	{	\
		throw type(type::subtype);	\
	}

// #include "Box.h"

#include <stdio.h>
#include <unistd.h>

#include "ServerException.h"

#include "BackupDaemon.h"
#include "Socket.h"

#define BOX_NAMED_PIPE_NAME L"\\\\.\\pipe\\boxbackup"
	
// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::BackupDaemon()
//		Purpose: constructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupDaemon::BackupDaemon()
	: mpCommandSocketInfo(0)
{ }

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
	if(mpCommandSocketInfo != 0)
	{
		delete mpCommandSocketInfo;
		mpCommandSocketInfo = 0;
	}
}

void ConnectorConnectPipe()
{
	HANDLE SocketHandle = CreateFileW( 
		BOX_NAMED_PIPE_NAME,   // pipe name 
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

	while (true)
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
		mpCommandSocketInfo->mListeningSocket.Close();
	}
	catch(...)
	{
		// Ignore any errors
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
{ }


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupDaemon::CommandSocketInfo::~CommandSocketInfo()
//		Purpose: Destructor
//		Created: 18/2/04
//
// --------------------------------------------------------------------------
BackupDaemon::CommandSocketInfo::~CommandSocketInfo()
{ }
