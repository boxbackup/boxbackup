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

std::string WinNamedPipeStream::sPipeNamePrefix = "\\\\.\\pipe\\";

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
		try
		{
			Close();
		}
		catch (std::exception &e)
		{
			BOX_ERROR("Caught exception while destroying "
				"named pipe, ignored: " << e.what());
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Accept(const std::string& rName)
//		Purpose: Creates a new named pipe with the given name,
//			and wait for a connection on it
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::Accept(const std::string& rName)
{
	if (mSocketHandle != INVALID_HANDLE_VALUE || mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}

	std::string socket = sPipeNamePrefix + rName;

	mSocketHandle = CreateNamedPipeA( 
		socket.c_str(),            // pipe name 
		PIPE_ACCESS_DUPLEX |       // read/write access 
		FILE_FLAG_OVERLAPPED,      // enabled overlapped I/O
		PIPE_TYPE_BYTE |           // message type pipe 
		PIPE_READMODE_BYTE |       // message-read mode 
		PIPE_WAIT,                 // blocking mode 
		1,                         // max. instances  
		4096,                      // output buffer size 
		4096,                      // input buffer size 
		NMPWAIT_USE_DEFAULT_WAIT,  // client time-out 
		NULL);                     // default security attribute 

	if (mSocketHandle == INVALID_HANDLE_VALUE)
	{
		BOX_ERROR("Failed to CreateNamedPipeA(" << socket << "): " <<
			GetErrorMessage(GetLastError()));
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}

	bool connected = ConnectNamedPipe(mSocketHandle, (LPOVERLAPPED) NULL);

	if (!connected)
	{
		BOX_ERROR("Failed to ConnectNamedPipe(" << socket << "): " <<
			GetErrorMessage(GetLastError()));
		Close();
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}
	
	mBytesInBuffer = 0;
	mReadClosed  = false;
	mWriteClosed = false;
	mIsServer    = true; // must flush and disconnect before closing
	mIsConnected = true;

	// create the Readable event
	mReadableEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (mReadableEvent == INVALID_HANDLE_VALUE)
	{
		BOX_ERROR("Failed to create the Readable event: " <<
			GetErrorMessage(GetLastError()));
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
			BOX_ERROR("Failed to start overlapped read: " <<
				GetErrorMessage(err));
			Close();
			THROW_EXCEPTION(ConnectionException, 
				Conn_SocketReadError)
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::Connect(const std::string& rName)
//		Purpose: Opens a connection to a listening named pipe
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------
void WinNamedPipeStream::Connect(const std::string& rName)
{
	if (mSocketHandle != INVALID_HANDLE_VALUE || mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}

	std::string socket = sPipeNamePrefix + rName;
	
	mSocketHandle = CreateFileA( 
		socket.c_str(), // pipe name 
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
			BOX_ERROR("Failed to connect to backup daemon: "
				"it is busy with another connection");
		}
		else
		{
			BOX_ERROR("Failed to connect to backup daemon: " <<
				GetErrorMessage(err));
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

	if (mReadClosed)
	{
		THROW_EXCEPTION(ConnectionException, SocketShutdownError)
	}

	// ensure safe to cast NBytes to unsigned
	if (NBytes < 0)
	{
		THROW_EXCEPTION(CommonException, AssertFailed)
	}

	DWORD NumBytesRead;

	if (mIsServer)
	{
		// satisfy from buffer if possible, to avoid
		// blocking on read.
		bool needAnotherRead = false;
		if (mBytesInBuffer == 0)
		{
			// overlapped I/O completed successfully? 
			// (wait if needed)

			if (GetOverlappedResult(mSocketHandle,
				&mReadOverlap, &NumBytesRead, TRUE))
			{
				needAnotherRead = true;
			}
			else
			{
				DWORD err = GetLastError();

				if (err == ERROR_HANDLE_EOF)
				{
					mReadClosed = true;
				}
				else 
				{
					if (err == ERROR_BROKEN_PIPE)
					{
						BOX_ERROR("Control client "
							"disconnected");
					}
					else
					{
						BOX_ERROR("Failed to wait for "
							"ReadFile to complete: "
							<< GetErrorMessage(err));
					}

					Close();
					THROW_EXCEPTION(ConnectionException, 
						Conn_SocketReadError)
				}
			}
		}
		else
		{
			NumBytesRead = 0;
		}

		size_t BytesToCopy = NumBytesRead + mBytesInBuffer;
		size_t BytesRemaining = 0;

		if (BytesToCopy > (size_t)NBytes)
		{
			BytesRemaining = BytesToCopy - NBytes;
			BytesToCopy = NBytes;
		}

		memcpy(pBuffer, mReadBuffer, BytesToCopy);
		memmove(mReadBuffer, mReadBuffer + BytesToCopy, BytesRemaining);

		mBytesInBuffer = BytesRemaining;
		NumBytesRead = BytesToCopy;

		if (needAnotherRead)
		{
			// reinitialise the OVERLAPPED structure
			memset(&mReadOverlap, 0, sizeof(mReadOverlap));
			mReadOverlap.hEvent = mReadableEvent;
		}

		// start the next overlapped read
		if (needAnotherRead && !ReadFile(mSocketHandle, 
			mReadBuffer + mBytesInBuffer, 
			sizeof(mReadBuffer) - mBytesInBuffer,
			NULL, &mReadOverlap))
		{
			DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING)
			{
				// Don't reset yet, there might be data
				// in the buffer waiting to be read, 
				// will check below.
				// ResetEvent(mReadableEvent);
			}
			else if (err == ERROR_HANDLE_EOF)
			{
				mReadClosed = true;
			}
			else if (err == ERROR_BROKEN_PIPE)
			{
				BOX_ERROR("Control client disconnected");
				mReadClosed = true;
			}
			else
			{
				BOX_ERROR("Failed to start overlapped read: "
					<< GetErrorMessage(err));
				Close();
				THROW_EXCEPTION(ConnectionException, 
					Conn_SocketReadError)
			}
		}

		// If the read succeeded immediately, leave the event 
		// signaled, so that we will be called again to process 
		// the newly read data and start another overlapped read.
		if (needAnotherRead && !mReadClosed)
		{
			// leave signalled
		}
		else if (!needAnotherRead && mBytesInBuffer > 0)
		{
			// leave signalled
		}
		else
		{
			// nothing left to read, reset the event
			ResetEvent(mReadableEvent);
			// FIXME: a pending read could have signalled
			// the event (again) while we were busy reading.
			// that signal would be lost, and the reading
			// thread would block. Should be pretty obvious
			// if this happens in practice: control client
			// hangs.
		}
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
			DWORD err = GetLastError();
		
			Close();

			// ERROR_NO_DATA is a strange name for 
			// "The pipe is being closed". No exception wanted.

			if (err == ERROR_NO_DATA || 
				err == ERROR_PIPE_NOT_CONNECTED) 
			{
				NumBytesRead = 0;
			}
			else
			{
				BOX_ERROR("Failed to read from control socket: "
					<< GetErrorMessage(err));
				THROW_EXCEPTION(ConnectionException, 
					Conn_SocketReadError)
			}
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
			// ERROR_NO_DATA is a strange name for 
			// "The pipe is being closed". No exception wanted.

			DWORD err = GetLastError();

			if (err != ERROR_NO_DATA)
			{
				BOX_ERROR("Failed to write to control "
					socket: " << GetErrorMessage(err));
			}

			Close();

			if (err == ERROR_NO_DATA) 
			{
				return;
			}
			else
			{
				THROW_EXCEPTION(ConnectionException, 
					Conn_SocketWriteError)
			}
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
		BOX_ERROR("Named pipe: inconsistent connected state");
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
			BOX_ERROR("Failed to cancel outstanding I/O: " <<
				GetErrorMessage(GetLastError()));
		}

		if (mReadableEvent == INVALID_HANDLE_VALUE)
		{
			BOX_ERROR("Failed to destroy Readable event: "
				"invalid handle");
		}
		else if (!CloseHandle(mReadableEvent))
		{
			BOX_ERROR("Failed to destroy Readable event: " <<
				GetErrorMessage(GetLastError()));
		}

		mReadableEvent = INVALID_HANDLE_VALUE;

		if (!FlushFileBuffers(mSocketHandle))
		{
			BOX_ERROR("Failed to FlushFileBuffers: " <<
				GetErrorMessage(GetLastError()));
		}
	
		if (!DisconnectNamedPipe(mSocketHandle))
		{
			DWORD err = GetLastError();
			if (err != ERROR_PIPE_NOT_CONNECTED)
			{
				BOX_ERROR("Failed to DisconnectNamedPipe: " <<
					GetErrorMessage(err));
			}
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
		BOX_ERROR("Failed to CloseHandle: " <<
			GetErrorMessage(GetLastError()));
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
		BOX_ERROR("Failed to FlushFileBuffers: " <<
			GetErrorMessage(GetLastError()));
	}
}


#endif // WIN32
