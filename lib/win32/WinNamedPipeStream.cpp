// --------------------------------------------------------------------------
//
// File
//		Name:    WinNamedPipeStream.cpp
//		Purpose: I/O stream interface for Win32 named pipes
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef WIN32

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <windows.h>

#include "WinNamedPipeStream.h"
#include "ServerException.h"
#include "CommonException.h"
#include "Socket.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::WinNamedPipeStream()
//		Purpose: Constructor (create stream ready for Open() call)
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------
WinNamedPipeStream::WinNamedPipeStream()
	: mSocketHandle(NULL),
	  mReadClosed(false),
	  mWriteClosed(false),
	  mIsServer(false),
	  mIsConnected(false)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::~WinNamedPipeStream()
//		Purpose: Destructor, closes stream if open
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------
WinNamedPipeStream::~WinNamedPipeStream()
{
	if (mSocketHandle != NULL)
	{
		Close();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Accept(const char* Name)
//		Purpose: Creates a new named pipe with the given name,
//			and wait for a connection on it
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::Accept(const wchar_t* pName)
{
	if (mSocketHandle != NULL || mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}

	SECURITY_ATTRIBUTES Security;
	Security.nLength = sizeof(SECURITY_ATTRIBUTES);
	Security.lpSecurityDescriptor = NULL; // inherit from process
	Security.bInheritHandle = FALSE; // don't pass to new processes

	mSocketHandle = CreateNamedPipeW( 
		pName,                     // pipe name 
		PIPE_ACCESS_DUPLEX,        // read/write access 
		PIPE_TYPE_MESSAGE |        // message type pipe 
		PIPE_READMODE_MESSAGE |    // message-read mode 
		PIPE_WAIT,                 // blocking mode 
		1,                         // max. instances  
		4096,                      // output buffer size 
		4096,                      // input buffer size 
		NMPWAIT_USE_DEFAULT_WAIT,  // client time-out 
		&Security);                // use our security attributes

	if (mSocketHandle == NULL)
	{
		::syslog(LOG_ERR, "CreateNamedPipeW failed: %d", 
			GetLastError());
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}

	bool connected = ConnectNamedPipe(mSocketHandle, (LPOVERLAPPED) NULL);

	if (!connected)
	{
		::syslog(LOG_ERR, "ConnectNamedPipe failed: %d", 
			GetLastError());
		CloseHandle(mSocketHandle);
		mSocketHandle = NULL;
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}
	
	mReadClosed  = FALSE;
	mWriteClosed = FALSE;
	mIsServer    = TRUE; // must flush and disconnect before closing
	mIsConnected = TRUE;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Connect(const char* Name)
//		Purpose: Opens a connection to a listening named pipe
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::Connect(const wchar_t* pName)
{
	if (mSocketHandle != NULL || mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}
	
	mSocketHandle = CreateFileW( 
		pName,          // pipe name 
		GENERIC_READ |  // read and write access 
		GENERIC_WRITE, 
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,
		0,              // default attributes 
		NULL);          // no template file 

	if (mSocketHandle == INVALID_HANDLE_VALUE)
	{
		::syslog(LOG_ERR, "Failed to connect to server's named pipe: "
			"error %d", GetLastError());
		CloseHandle(mSocketHandle);
		mSocketHandle = NULL;
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}

	DWORD Flags = PIPE_READMODE_MESSAGE | // put this end into message mode
		PIPE_WAIT;                    // put this end into blocking mode

	if (!SetNamedPipeHandleState(
		mSocketHandle,          // pipe handle
		&Flags,                 // mode flags
		NULL,                   // don't change the collection count
		NULL))                  // don't change the collect timeout
	{
		::syslog(LOG_ERR, "Failed to put pipe into message mode: "
			"error %d", GetLastError());
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}

	mReadClosed  = FALSE;
	mWriteClosed = FALSE;
	mIsServer    = FALSE; // just close the socket
	mIsConnected = TRUE;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Read(void *pBuffer, int NBytes)
//		Purpose: Reads data from stream. Maybe returns less than asked for.
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
int WinNamedPipeStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	// TODO no support for timeouts yet
	ASSERT(Timeout == IOStream::TimeOutInfinite)
	
	if (mSocketHandle == NULL || !mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}

	DWORD NumBytesRead;
	
	bool Success = ReadFile( 
		mSocketHandle, // pipe handle 
		pBuffer,       // buffer to receive reply 
		NBytes,        // size of buffer 
		&NumBytesRead, // number of bytes read 
		NULL);         // not overlapped 
	
	if (!Success)
	{
		THROW_EXCEPTION(ConnectionException, Conn_SocketReadError)
	}
	
	// Closed for reading at EOF?
	if (NumBytesRead == 0)
	{
		mReadClosed = true;
	}
	
	return NumBytesRead;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Write(void *pBuffer, int NBytes)
//		Purpose: Writes data, blocking until it's all done.
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::Write(const void *pBuffer, int NBytes)
{
	if (mSocketHandle == NULL || !mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	
	// Buffer in byte sized type.
	ASSERT(sizeof(char) == 1);
	const char *pByteBuffer = (char *)pBuffer;
	
	int NumBytesWrittenTotal = 0;

	while (NumBytesWrittenTotal < NBytes)
	{
		DWORD NumBytesWrittenThisTime = 0;

		bool Success = WriteFile( 
			mSocketHandle,    // pipe handle 
			pByteBuffer + NumBytesWrittenTotal, // message 
			NBytes      - NumBytesWrittenTotal, // message length 
			&NumBytesWrittenThisTime, // bytes written this time
			NULL);            // not overlapped 

		if (!Success)
		{
			mWriteClosed = true;	// assume can't write again
			THROW_EXCEPTION(ConnectionException, 
				Conn_SocketWriteError)
		}

		NumBytesWrittenTotal += NumBytesWrittenThisTime;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Close()
//		Purpose: Closes connection to remote socket
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::Close()
{
	if (mSocketHandle == NULL || !mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}

	if (mIsServer)
	{	
		if (!FlushFileBuffers(mSocketHandle))
		{
			::syslog(LOG_INFO, "FlushFileBuffers failed: %d", 
				GetLastError());
		}
	
		if (!DisconnectNamedPipe(mSocketHandle))
		{
			::syslog(LOG_ERR, "DisconnectNamedPipe failed: %d", 
				GetLastError());
		}

		mIsServer = false;
	}

	if (!CloseHandle(mSocketHandle))
	{
		::syslog(LOG_ERR, "CloseHandle failed: %d", GetLastError());
		THROW_EXCEPTION(ServerException, SocketCloseError)
	}
	
	mSocketHandle = NULL;
	mIsConnected = FALSE;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::StreamDataLeft()
//		Purpose: Still capable of reading data?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool WinNamedPipeStream::StreamDataLeft()
{
	return !mReadClosed;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::StreamClosed()
//		Purpose: Connection been closed?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool WinNamedPipeStream::StreamClosed()
{
	return mWriteClosed;
}

#endif // WIN32
