// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemon.cpp
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include <stdio.h>
#include <unistd.h>
#include <windows.h>

class BackupDaemon
{
public:
	BackupDaemon();
	~BackupDaemon();

private:
	BackupDaemon(const BackupDaemon &) { /* do not call */ }

public:
	void Run();

	// Allow other classes to call this too
	enum
	{
		NotifyEvent_StoreFull = 0,
		NotifyEvent_ReadError = 1,
		NotifyEvent__MAX = 1
		// When adding notifications, remember to add strings to NotifySysadmin()
	};

private:
	void Run2();
};

#define BOX_NAMED_PIPE_NAME L"\\\\.\\pipe\\boxbackup"

BackupDaemon::BackupDaemon() { }
BackupDaemon::~BackupDaemon() { }

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

void AcceptorAcceptPipe(const wchar_t* pName)
{
	printf(".");

	HANDLE handle;

	handle = CreateNamedPipeW( 
		pName,                     // pipe name 
		PIPE_ACCESS_DUPLEX,        // read/write access 
		PIPE_TYPE_MESSAGE |        // message type pipe 
		PIPE_READMODE_MESSAGE |    // message-read mode 
		PIPE_WAIT,                 // blocking mode 
		1,                         // max. instances  
		4096,                      // output buffer size 
		4096,                      // input buffer size 
		NMPWAIT_USE_DEFAULT_WAIT,  // client time-out 
		NULL);                     // default security attribute 

	if (handle == NULL)
	{
		printf("CreateNamedPipeW failed: %d\n", GetLastError());
		throw 1;
	}

	bool connected = ConnectNamedPipe(handle, (LPOVERLAPPED) NULL);

	if (!connected)
	{
		printf("ConnectNamedPipe failed: %d\n", GetLastError());
		CloseHandle(handle);
		throw 1;
	}

	if (!FlushFileBuffers(handle))
	{
		printf("FlushFileBuffers failed: %d\n", GetLastError());
	}

	if (!DisconnectNamedPipe(handle))
	{
		printf("DisconnectNamedPipe failed: %d\n", GetLastError());
	}

	if (!CloseHandle(handle))
	{
		printf("CloseHandle failed: %d\n", GetLastError());
		throw 1;
	}
}

unsigned int WINAPI AcceptorThread( LPVOID lpParam ) 
{ 
	while (true)
	{
		try
		{
			AcceptorAcceptPipe(BOX_NAMED_PIPE_NAME);
		}
		catch (int i)
		{
			if (i == 2)
			{
				printf("Impossible error in "
					"this thread! Aborting.\n");
				exit(1);
			}
		}
	}

	return 0;
}

void BackupDaemon::Run()
{
	// Create a thread to handle the named pipe
	HANDLE hThread;
	unsigned int dwThreadId;

	hThread = (HANDLE) _beginthreadex( 
        	NULL,                        // default security attributes 
        	0,                           // use default stack size  
        	AcceptorThread,              // thread function 
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

	// Handle things nicely on exceptions
	try
	{
		Run2();
	}
	catch(...)
	{
		printf("Caught exception in Run()");
	}
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
	int sockAddrLen = 0;
	int sockDomain = AF_INET;
	struct sockaddr_in addr;

	// Lookup hostname
	struct hostent *phost = ::gethostbyname("1.2.3.4");

	sockAddrLen = sizeof(addr);
	addr.sin_family = PF_INET;
	addr.sin_port = htons(2201);
	addr.sin_addr = *((in_addr*)phost->h_addr_list[0]);
	for(unsigned int l = 0; l < sizeof(addr.sin_zero); ++l)
	{
		addr.sin_zero[l] = 0;
	}
	
	// Create the socket
	int handle = ::socket(sockDomain, SOCK_STREAM, 
		0 /* let OS choose protocol */);
	if(handle == -1)
	{
		throw 2;
	}
	
	// Connect it
	if(::connect(handle, (struct sockaddr*)&addr, sockAddrLen) == -1)
	{
		// Dispose of the socket
		::closesocket(handle);
		throw 2;
	}
}
