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

#include "autogen_ConnectionException.h"
#include "autogen_ServerException.h"
#include "BoxTime.h"
#include "CommonException.h"
#include "Socket.h"
#include "WinNamedPipeStream.h"

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
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    WinNamedPipeStream::WinNamedPipeStream(HANDLE)
//		Purpose: Constructor (with already-connected pipe handle)
//		Created: 2008/10/01
//
// --------------------------------------------------------------------------
WinNamedPipeStream::WinNamedPipeStream(HANDLE hNamedPipe)
	: mSocketHandle(hNamedPipe),
	  mReadableEvent(INVALID_HANDLE_VALUE),
	  mBytesInBuffer(0),
	  mReadClosed(false),
	  mWriteClosed(false),
	  mIsServer(true),
	  mIsConnected(true)
{ 
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
				SocketReadError)
		}
	}
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
	for(std::list<WriteInProgress*>::iterator i = mWritesInProgress.begin();
		i != mWritesInProgress.end(); i++)
	{
		delete *i;
	}

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
/*
void WinNamedPipeStream::Accept()
{
	if (mSocketHandle == INVALID_HANDLE_VALUE)
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle);
	}

	if (mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen);
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
*/

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

// Returns true if the operation is complete (and you will need to start
// another one), or false otherwise (you can wait again).
bool WinNamedPipeStream::WaitForOverlappedOperation(OVERLAPPED& Overlapped,
	int Timeout, int64_t* pBytesTransferred)
{
	if (Timeout == IOStream::TimeOutInfinite)
	{
		Timeout = INFINITE;
	}
	
	// overlapped I/O completed successfully? (wait if needed)
	DWORD waitResult = WaitForSingleObject(Overlapped.hEvent, Timeout);
	DWORD NumBytesTransferred = -1;

	if (waitResult == WAIT_ABANDONED)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, BadSocketHandle,
			"Wait for command socket read abandoned by system");
	}

	if (waitResult == WAIT_TIMEOUT)
	{
		// wait timed out, nothing to read
		*pBytesTransferred = 0;
		return false;
	}

	if (waitResult != WAIT_OBJECT_0)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, BadSocketHandle,
			"Failed to wait for command socket read: unknown "
			"result code: " << waitResult);
	}

	// object is ready to read from
	if (GetOverlappedResult(mSocketHandle, &Overlapped,
		&NumBytesTransferred, TRUE))
	{
		*pBytesTransferred = NumBytesTransferred;
		return true;
	}

	// We are here because there was an error.
	DWORD err = GetLastError();

	if (err == ERROR_HANDLE_EOF)
	{
		Close();
		return true;
	}

	// ERROR_NO_DATA is a strange name for 
	// "The pipe is being closed". No exception wanted.

	if (err == ERROR_NO_DATA || 
		err == ERROR_PIPE_NOT_CONNECTED ||
		err == ERROR_BROKEN_PIPE)
	{
		BOX_INFO(BOX_WIN_ERRNO_MESSAGE(err,
			"Control client disconnected"));
		Close();
		return true;
	}

	THROW_WIN_ERROR_NUMBER("Failed to wait for OVERLAPPED operation "
		"to complete", err, ConnectionException, SocketReadError);
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
	if (!mIsServer && Timeout != IOStream::TimeOutInfinite)
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

	int64_t NumBytesRead;

	// satisfy from buffer if possible, to avoid
	// blocking on read.
	bool needAnotherRead = false;
	if (mBytesInBuffer == 0)
	{
		needAnotherRead = WaitForOverlappedOperation(
			mReadOverlap, Timeout, &NumBytesRead);
	}
	else
	{
		// Just return the existing data from the buffer
		// this time around. The caller should call again,
		// and then the buffer will be empty.
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
				SocketReadError)
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
void WinNamedPipeStream::Write(const void *pBuffer, int NBytes, int Timeout)
{
	// Calculate the deadline at the beginning. Not valid if Timeout is
	// IOStream::TimeOutInfinite!
	ASSERT(Timeout != IOStream::TimeOutInfinite);

	box_time_t deadline = GetCurrentBoxTime() +
		MilliSecondsToBoxTime(Timeout);

	if (mSocketHandle == INVALID_HANDLE_VALUE || !mIsConnected) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	
	// Buffer in byte sized type.
	ASSERT(sizeof(char) == 1);
	WriteInProgress* new_write = new WriteInProgress(
		std::string((char *)pBuffer, NBytes));

	// Start the WriteFile operation, and add to queue if pending.
	BOOL Success = WriteFile( 
		mSocketHandle,    // pipe handle 
		new_write->mBuffer.c_str(), // message 
		NBytes, // message length 
		NULL, // bytes written this time
		&(new_write->mOverlap));

	if (Success == TRUE)
	{
		BOX_NOTICE("Write claimed success while overlapped?");
		mWritesInProgress.push_back(new_write);
	}
	else
	{
		DWORD err = GetLastError();

		if (err == ERROR_IO_PENDING)
		{
			BOX_TRACE("WriteFile is pending, adding to queue");
			mWritesInProgress.push_back(new_write);
		}
		else
		{
			// Not in progress any more, pop it
			Close();
			THROW_WIN_ERROR_NUMBER("Failed to start overlapped "
				"write", err, ConnectionException,
				SocketWriteError);
		}
	}

	// Wait for previous WriteFile operations to complete, one at a time,
	// until the deadline expires.
	for(box_time_t remaining = deadline - GetCurrentBoxTime();
		remaining > 0 && !mWritesInProgress.empty();
		remaining = deadline - GetCurrentBoxTime())
	{
		int new_timeout = BoxTimeToMilliSeconds(remaining);
		WriteInProgress* oldest_write =
			*(mWritesInProgress.begin());

		int64_t bytes_written = 0;
		if(WaitForOverlappedOperation(oldest_write->mOverlap,
			new_timeout, &bytes_written))
		{
			// This one is complete, pop it and start a new one
			delete oldest_write;
			mWritesInProgress.pop_front();
		}
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

	mSocketHandle = INVALID_HANDLE_VALUE;
	mIsConnected = false;
	mReadClosed  = true;
	mWriteClosed = true;

	if (!CloseHandle(mSocketHandle))
	{
		THROW_WIN_ERROR_NUMBER("Failed to CloseHandle",
			GetLastError(), ServerException, SocketCloseError);
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
