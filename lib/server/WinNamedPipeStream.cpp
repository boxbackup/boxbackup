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

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

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
	: mSocketHandle(INVALID_HANDLE_VALUE),
	  mReadableEvent(INVALID_HANDLE_VALUE),
	  mBytesInBuffer(0),
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
	if (mSocketHandle != INVALID_HANDLE_VALUE)
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
	if (mSocketHandle != INVALID_HANDLE_VALUE || mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}

	mSocketHandle = CreateNamedPipeW( 
		pName,                     // pipe name 
		PIPE_ACCESS_DUPLEX |       // read/write access 
		FILE_FLAG_OVERLAPPED,      // enabled overlapped I/O
		PIPE_TYPE_MESSAGE |        // message type pipe 
		PIPE_READMODE_MESSAGE |    // message-read mode 
		PIPE_WAIT,                 // blocking mode 
		1,                         // max. instances  
		4096,                      // output buffer size 
		4096,                      // input buffer size 
		NMPWAIT_USE_DEFAULT_WAIT,  // client time-out 
		NULL);                     // default security attribute 

	if (mSocketHandle == INVALID_HANDLE_VALUE)
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
		Close();
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}
	
	mReadClosed  = false;
	mWriteClosed = false;
	mIsServer    = true; // must flush and disconnect before closing
	mIsConnected = true;

	// create the Readable event
	mReadableEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (mReadableEvent == INVALID_HANDLE_VALUE)
	{
		::syslog(LOG_ERR, "Failed to create the Readable event: "
			"error %d", GetLastError());
		Close();
		THROW_EXCEPTION(CommonException, Internal)
	}

	// initialise the OVERLAPPED structure
	memset(&mReadOverlap, 0, sizeof(mReadOverlap));
	mReadOverlap.hEvent = mReadableEvent;

	// start the first overlapped read
	if (!ReadFile(mSocketHandle, mReadBuffer, sizeof(mReadBuffer),
		NULL, &mReadOverlap))
	{
		DWORD err = GetLastError();

		if (err != ERROR_IO_PENDING)
		{
			::syslog(LOG_ERR, "Failed to start overlapped read: "
				"error %d", err);
			Close();
			THROW_EXCEPTION(ConnectionException, 
				Conn_SocketReadError)
		}
	}
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
	if (mSocketHandle != INVALID_HANDLE_VALUE || mIsConnected) 
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
		DWORD err = GetLastError();
		if (err == ERROR_PIPE_BUSY)
		{
			::syslog(LOG_ERR, "Failed to connect to backup "
				"daemon: it is busy with another connection");
		}
		else
		{
			::syslog(LOG_ERR, "Failed to connect to backup "
				"daemon: error %d", err);
		}
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}

	mReadClosed  = false;
	mWriteClosed = false;
	mIsServer    = false; // just close the socket
	mIsConnected = true;
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
	if (Timeout != IOStream::TimeOutInfinite)
	{
		THROW_EXCEPTION(CommonException, AssertFailed)
	}
	
	if (mSocketHandle == INVALID_HANDLE_VALUE || !mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}

	DWORD NumBytesRead;

	if (mIsServer)
	{
		// overlapped I/O completed successfully? (wait if needed)

		if (!GetOverlappedResult(mSocketHandle,
			&mReadOverlap, &NumBytesRead, TRUE))
		{
			DWORD err = GetLastError();

			if (err == ERROR_HANDLE_EOF)
			{
				mReadClosed = true;
			}
			else if (err == ERROR_BROKEN_PIPE)
			{
				::syslog(LOG_ERR, 
					"Control client disconnected");
				mReadClosed = true;
			}
			else
			{
				::syslog(LOG_ERR, "Failed to wait for "
					"ReadFile to complete: error %d", err);
				Close();
				THROW_EXCEPTION(ConnectionException, 
					Conn_SocketReadError)
			}
		}

		size_t BytesToCopy = NumBytesRead + mBytesInBuffer;
		size_t BytesRemaining = 0;

		if (BytesToCopy > NBytes)
		{
			BytesRemaining = BytesToCopy - NBytes;
			BytesToCopy = NBytes;
		}

		memcpy(pBuffer, mReadBuffer, BytesToCopy);
		memcpy(mReadBuffer, mReadBuffer + BytesToCopy, BytesRemaining);

		mBytesInBuffer = BytesRemaining;
		NumBytesRead = BytesToCopy;

		// start the next overlapped read
		if (!ReadFile(mSocketHandle, 
			mReadBuffer + mBytesInBuffer, 
			sizeof(mReadBuffer) - mBytesInBuffer,
			NULL, &mReadOverlap))
		{
			DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING)
			{
				ResetEvent(mReadableEvent);
			}
			else if (err == ERROR_HANDLE_EOF)
			{
				mReadClosed = true;
			}
			else
			{
				::syslog(LOG_ERR, "Failed to start "
					"overlapped read: error %d", err);
				Close();
				THROW_EXCEPTION(ConnectionException, 
					Conn_SocketReadError)
			}
		}

		// If the read succeeded immediately, leave the event 
		// signaled, so that we will be called again to process 
		// the newly read data and start another overlapped read.
	}
	else
	{
		if (!ReadFile( 
			mSocketHandle, // pipe handle 
			pBuffer,       // buffer to receive reply 
			NBytes,        // size of buffer 
			&NumBytesRead, // number of bytes read 
			NULL))         // not overlapped 
		{
			Close();
			THROW_EXCEPTION(ConnectionException, 
				Conn_SocketReadError)
		}
		
		// Closed for reading at EOF?
		if (NumBytesRead == 0)
		{
			mReadClosed = true;
		}
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
	if (mSocketHandle == INVALID_HANDLE_VALUE || !mIsConnected) 
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
			Close();
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
	if (mSocketHandle == INVALID_HANDLE_VALUE && mIsConnected)
	{
		fprintf(stderr, "Inconsistent connected state\n");
		::syslog(LOG_ERR, "Inconsistent connected state");
		mIsConnected = false;
	}

	if (mSocketHandle == INVALID_HANDLE_VALUE) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}

	if (mIsServer)
	{
		if (!CancelIo(mSocketHandle))
		{
			::syslog(LOG_ERR, "Failed to cancel outstanding "
				"I/O: error %d", GetLastError());
		}

		if (mReadableEvent == INVALID_HANDLE_VALUE)
		{
			::syslog(LOG_ERR, "Failed to destroy Readable "
				"event: invalid handle");
		}
		else if (!CloseHandle(mReadableEvent))
		{
			::syslog(LOG_ERR, "Failed to destroy Readable "
				"event: error %d", GetLastError());
		}

		mReadableEvent = INVALID_HANDLE_VALUE;

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

	bool result = CloseHandle(mSocketHandle);

	mSocketHandle = INVALID_HANDLE_VALUE;
	mIsConnected = false;
	mReadClosed  = true;
	mWriteClosed = true;

	if (!result) 
	{
		::syslog(LOG_ERR, "CloseHandle failed: %d", GetLastError());
		THROW_EXCEPTION(ServerException, SocketCloseError)
	}
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

// --------------------------------------------------------------------------
//
// Function
//		Name:    IOStream::WriteAllBuffered()
//		Purpose: Ensures that any data which has been buffered is written to the stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::WriteAllBuffered()
{
	if (mSocketHandle == INVALID_HANDLE_VALUE || !mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	
	if (!FlushFileBuffers(mSocketHandle))
	{
		::syslog(LOG_WARNING, "FlushFileBuffers failed: %d", 
			GetLastError());
	}
}


#endif // WIN32
