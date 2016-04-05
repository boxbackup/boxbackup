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
  mIsConnected(false),
  mNeedAnotherRead(false)
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
  mIsConnected(true),
  mNeedAnotherRead(false)
{ 
	StartFirstRead();
}

// Start the first overlapped read
void WinNamedPipeStream::StartFirstRead()
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

	StartOverlappedRead();
}

void WinNamedPipeStream::StartOverlappedRead()
{
	// We should only do this when the buffer is empty. We don't want
	// to start an overlapped read anywhere else than the start of the
	// buffer, because it could complete at any time and we don't want
	// to mess about with interrupting the read already in progress.
	ASSERT(mBytesInBuffer == 0);

	// Initialise the OVERLAPPED structure
	memset(&mReadOverlap, 0, sizeof(mReadOverlap));
	mReadOverlap.hEvent = mReadableEvent;

	if (!ReadFile(mSocketHandle, mReadBuffer, sizeof(mReadBuffer),
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
			BOX_INFO("Control client disconnected");
			mReadClosed = true;
		}
		else if (err == ERROR_BROKEN_PIPE ||
			err == ERROR_PIPE_NOT_CONNECTED)
		{
			BOX_NOTICE("Control client disconnected");
			mReadClosed = true;
			mIsConnected = false;
		}
		else
		{
			Close();
			THROW_WIN_ERROR_NUMBER("Failed to start overlapped "
				"read", err, ConnectionException,
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

	StartFirstRead();
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
		0, // FILE_FLAG_OVERLAPPED, // dwFlagsAndAttributes
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

	StartFirstRead();
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

	if (waitResult == WAIT_FAILED)
	{
		THROW_WIN_ERROR_NUMBER("Failed to wait for overlapped I/O",
			GetLastError(), ServerException, Internal);
	}

	if (waitResult == WAIT_ABANDONED)
	{
		THROW_EXCEPTION_MESSAGE(ServerException, Internal,
			"Wait for overlapped I/O abandoned by system");
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
			"Failed to wait for overlapped I/O: unknown "
			"result code: " << waitResult);
	}

	// Overlapped operation completed successfully. Return the number
	// of bytes transferred.
	if (GetOverlappedResult(mSocketHandle, &Overlapped,
		&NumBytesTransferred, TRUE))
	{
		*pBytesTransferred = NumBytesTransferred;
		return true;
	}

	// We are here because GetOverlappedResult() informed us that the
	// overlapped operation encountered an error, so what was it?
	DWORD err = GetLastError();

	if (err == ERROR_HANDLE_EOF)
	{
		Close();
		*pBytesTransferred = 0;
		return true;
	}

	// ERROR_NO_DATA is a strange name for 
	// "The pipe is being closed". No exception wanted.

	if (err == ERROR_NO_DATA || 
		err == ERROR_PIPE_NOT_CONNECTED ||
		err == ERROR_BROKEN_PIPE)
	{
		BOX_INFO(BOX_WIN_ERRNO_MESSAGE(err,
			"Named pipe peer disconnected"));
		Close();
		*pBytesTransferred = 0;
		return true;
	}

	THROW_WIN_ERROR_NUMBER("Failed to wait for overlapped I/O "
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
	if (mSocketHandle == INVALID_HANDLE_VALUE || !mIsConnected) 
	{
		THROW_EXCEPTION_MESSAGE(ServerException, BadSocketHandle,
			"Tried to read from closed pipe");
	}

	if (mReadClosed)
	{
		THROW_EXCEPTION_MESSAGE(ConnectionException,
			SocketShutdownError, "Tried to read from closing pipe");
	}

	// ensure safe to cast NBytes to unsigned
	if (NBytes < 0)
	{
		THROW_EXCEPTION(CommonException, AssertFailed);
	}

	int64_t NumBytesRead;

	// Satisfy from buffer if possible, to avoid blocking on read.
	if (mBytesInBuffer == 0)
	{
		if (mNeedAnotherRead)
		{
			// Start the next overlapped read
			StartOverlappedRead();
		}

		mNeedAnotherRead = WaitForOverlappedOperation(mReadOverlap,
			Timeout, &NumBytesRead);
	}
	else
	{
		// Just return the existing data from the buffer
		// this time around. The caller should call again,
		// and then the buffer will be empty.
		NumBytesRead = 0;
	}

	int BytesToCopy = NumBytesRead + mBytesInBuffer;

	if (NBytes < BytesToCopy)
	{
		BytesToCopy = NBytes;
	}

	memcpy(pBuffer, mReadBuffer, BytesToCopy);

	size_t BytesRemaining = mBytesInBuffer + NumBytesRead - BytesToCopy;
	ASSERT(BytesToCopy + BytesRemaining <= sizeof(mReadBuffer));
	memmove(mReadBuffer, mReadBuffer + BytesToCopy, BytesRemaining);
	mBytesInBuffer = BytesRemaining;

	return BytesToCopy;
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
		// Unfortunately this does happen. We should still call
		// GetOverlappedResult() to get the number of bytes written,
		// so we can treat it just the same.
		// BOX_NOTICE("Write claimed success while overlapped?");
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
	// until the deadline expires or the pipe becomes disconnected.
	for(box_time_t remaining = deadline - GetCurrentBoxTime();
		remaining > 0 && !mWritesInProgress.empty() && mIsConnected;
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
		BOX_LOG_WIN_ERROR("Failed to cancel outstanding I/O");
	}

	if (mReadableEvent == INVALID_HANDLE_VALUE)
	{
		BOX_ERROR("Failed to destroy Readable event: "
			"invalid handle");
	}
	else if (!CloseHandle(mReadableEvent))
	{
		BOX_LOG_WIN_ERROR("Failed to destroy Readable event");
	}

	mReadableEvent = INVALID_HANDLE_VALUE;

	if (mIsConnected && !FlushFileBuffers(mSocketHandle))
	{
		BOX_LOG_WIN_ERROR("Failed to FlushFileBuffers");
	}

	if (mIsServer && mIsConnected && !DisconnectNamedPipe(mSocketHandle))
	{
		DWORD err = GetLastError();
		if (err != ERROR_PIPE_NOT_CONNECTED)
		{
			BOX_LOG_WIN_ERROR("Failed to DisconnectNamedPipe");
		}
	}

	if (!CloseHandle(mSocketHandle))
	{
		THROW_WIN_ERROR_NUMBER("Failed to CloseHandle",
			GetLastError(), ServerException, SocketCloseError);
	}

	mSocketHandle = INVALID_HANDLE_VALUE;
	mIsConnected = false;
	mReadClosed  = true;
	mWriteClosed = true;
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
