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
	  mIsServer(false)
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
void WinNamedPipeStream::Accept(const char* Name)
{
	if (mSocketHandle != NULL) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}

	ASSERT(Name == NULL)

	mSocketHandle = CreateNamedPipeW( 
		L"\\\\.\\pipe\\boxbackup", // pipe name 
		PIPE_ACCESS_DUPLEX,        // read/write access 
		PIPE_TYPE_MESSAGE |        // message type pipe 
		PIPE_READMODE_MESSAGE |    // message-read mode 
		PIPE_WAIT,                 // blocking mode 
		PIPE_UNLIMITED_INSTANCES,  // max. instances  
		4096,                      // output buffer size 
		4096,                      // input buffer size 
		NMPWAIT_USE_DEFAULT_WAIT,  // client time-out 
		NULL);                     // default security attribute 

	if (mSocketHandle == NULL)
	{
		::syslog(LOG_ERR, "CreateNamedPipeW failed: %d", GetLastError());
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}

	if (!ConnectNamedPipe(mSocketHandle, (LPOVERLAPPED) NULL))
	{
		CloseHandle(mSocketHandle);
		::syslog(LOG_ERR, "ConnectNamedPipe failed: %d", GetLastError());
		mSocketHandle = NULL;
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}
	
	mReadClosed  = FALSE;
	mWriteClosed = FALSE;
	mIsServer    = TRUE; // must DisconnectNamedPipe rather than CloseFile
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Connect(const char* Name)
//		Purpose: Opens a connection to a listening named pipe
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::Connect(const char* Name)
{
	if (mSocketHandle != NULL) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}
	
	ASSERT(Name == NULL)

	mSocketHandle = CreateFileW( 
		L"\\\\.\\pipe\\boxbackup",   // pipe name 
		GENERIC_READ |  // read and write access 
		GENERIC_WRITE, 
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,
		0,              // default attributes 
		NULL);          // no template file 

	if (mSocketHandle == INVALID_HANDLE_VALUE)
	{
		::syslog(LOG_ERR, "Error connecting to server's named pipe: %d", 
			GetLastError());
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}
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
	
	if (mSocketHandle == NULL) 
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
	if (mSocketHandle == NULL) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	
	// Buffer in byte sized type.
	// ASSERT(sizeof(char) == 1);
	// const char *buffer = (char *)pBuffer;
	
	DWORD NumBytesWritten;
	
	bool Success = WriteFile( 
		mSocketHandle,    // pipe handle 
		pBuffer,          // message 
		NBytes,           // message length 
		&NumBytesWritten, // bytes written 
		NULL);            // not overlapped 

	if (!Success)
	{
		mWriteClosed = true;	// assume can't write again
		THROW_EXCEPTION(ConnectionException, Conn_SocketWriteError)
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
	if (mSocketHandle == NULL) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	
	if (!FlushFileBuffers(mSocketHandle))
	{
		::syslog(LOG_ERR, "FlushFileBuffers failed: %d", GetLastError());
		THROW_EXCEPTION(ServerException, SocketCloseError)
	}
	
	if (mIsServer)
	{
		if (!DisconnectNamedPipe(mSocketHandle))
		{
			::syslog(LOG_ERR, "DisconnectNamedPipe failed: %d", GetLastError());
			THROW_EXCEPTION(ServerException, SocketCloseError)
		}
		mIsServer = false;
	}

	if (!CloseHandle(mSocketHandle))
	{
		::syslog(LOG_ERR, "CloseHandle failed: %d", GetLastError());
		THROW_EXCEPTION(ServerException, SocketCloseError)
	}
	
	mSocketHandle = NULL;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Shutdown(bool, bool)
//		Purpose: Shuts down a socket for further reading and/or writing
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
/*
void WinNamedPipeStream::Shutdown(bool Read, bool Write)
{
	if(mSocketHandle == -1) {THROW_EXCEPTION(ServerException, BadSocketHandle)}
	
	// Do anything?
	if(!Read && !Write) return;
	
	int how = SHUT_RDWR;
	if(Read && !Write) how = SHUT_RD;
	if(!Read && Write) how = SHUT_WR;
	
	// Shut it down!
	if(::shutdown(mSocketHandle, how) == -1)
	{
		THROW_EXCEPTION(ConnectionException, Conn_SocketShutdownError)
	}
}
*/

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


// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::GetSocketHandle()
//		Purpose: Returns socket handle for this stream (derived classes only).
//				 Will exception if there's no valid socket.
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
/*
tOSSocketHandle SocketStream::GetSocketHandle()
{
	if(mSocketHandle == -1) {THROW_EXCEPTION(ServerException, BadSocketHandle)}
	return mSocketHandle;
}
*/

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::GetPeerCredentials(uid_t &, gid_t &)
//		Purpose: Returns true if the peer credientials are available.
//				 (will work on UNIX domain sockets only)
//		Created: 19/2/04
//
// --------------------------------------------------------------------------
/*
bool WinNamedPipeStream::GetPeerCredentials(uid_t &rUidOut, gid_t &rGidOut)
{
#ifdef HAVE_GETPEEREID
	uid_t remoteEUID = 0xffff;
	gid_t remoteEGID = 0xffff;

	if(::getpeereid(mSocketHandle, &remoteEUID, &remoteEGID) == 0)
	{
		rUidOut = remoteEUID;
		rGidOut = remoteEGID;
		return true;
	}
#endif

#if HAVE_DECL_SO_PEERCRED
	struct ucred cred;
	socklen_t credLen = sizeof(cred);

	if(::getsockopt(mSocketHandle, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == 0)
	{
		rUidOut = cred.uid;
		rGidOut = cred.gid;
		return true;
	}
#endif

	// Not available
	return false;
}
*/

#endif // WIN32
