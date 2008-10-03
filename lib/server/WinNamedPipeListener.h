// --------------------------------------------------------------------------
//
// File
//		Name:    WinNamedPipeListener.h
//		Purpose: Windows named pipe socket connection listener
//			 for server
//		Created: 2008/09/30
//
// --------------------------------------------------------------------------

#ifndef WINNAMEDPIPELISTENER__H
#define WINNAMEDPIPELISTENER__H

#include <OverlappedIO.h>
#include <WinNamedPipeStream.h>

#include "ServerException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    WinNamedPipeListener
//		Purpose: 
//		Created: 2008/09/30
//
// --------------------------------------------------------------------------
template<int ListenBacklog = 128>
class WinNamedPipeListener
{
private:
	std::auto_ptr<std::string> mapPipeName;
	std::auto_ptr<OverlappedIO> mapOverlapConnect;
	HANDLE mPipeHandle;

public:
	// Initialise
	WinNamedPipeListener()
	: mPipeHandle(INVALID_HANDLE_VALUE)
	{ }

private:
	WinNamedPipeListener(const WinNamedPipeListener &rToCopy)
	{ /* forbidden */ }

	HANDLE CreatePipeHandle(const std::string& rName)
	{
		std::string socket = WinNamedPipeStream::sPipeNamePrefix +
			rName;

		HANDLE handle = CreateNamedPipeA( 
			socket.c_str(),            // pipe name 
			PIPE_ACCESS_DUPLEX |       // read/write access 
			FILE_FLAG_OVERLAPPED,      // enabled overlapped I/O
			PIPE_TYPE_BYTE |           // message type pipe 
			PIPE_READMODE_BYTE |       // message-read mode 
			PIPE_WAIT,                 // blocking mode 
			ListenBacklog + 1,         // max. instances  
			4096,                      // output buffer size 
			4096,                      // input buffer size 
			NMPWAIT_USE_DEFAULT_WAIT,  // client time-out 
			NULL);                     // default security attribute 

		if (handle == INVALID_HANDLE_VALUE)
		{
			BOX_LOG_WIN_ERROR("Failed to create named pipe " <<
				socket);
			THROW_EXCEPTION(ServerException, SocketOpenError)
		}

		return handle;
	}

public:
	~WinNamedPipeListener()
	{
		Close();
	}

	void Close()
	{
		if (mPipeHandle != INVALID_HANDLE_VALUE)
		{
			if (mapOverlapConnect.get())
			{
				// outstanding connect in progress
				if (CancelIo(mPipeHandle) != TRUE)
				{
					BOX_LOG_WIN_ERROR("Failed to cancel "
						"outstanding connect request "
						"on named pipe");
				}

				mapOverlapConnect.reset();
			}

			if (CloseHandle(mPipeHandle) != TRUE)
			{
				BOX_LOG_WIN_ERROR("Failed to close named pipe "
					"handle");
			}

			mPipeHandle = INVALID_HANDLE_VALUE;
		}
	}

	// ------------------------------------------------------------------
	//
	// Function
	//		Name:    WinNamedPipeListener::Listen(std::string name)
	//		Purpose: Initialises socket name
	//		Created: 2003/07/31
	//
	// ------------------------------------------------------------------
	void Listen(const std::string& rName)
	{
		Close();
		mapPipeName.reset(new std::string(rName));
		mPipeHandle = CreatePipeHandle(rName);
	}

	// ------------------------------------------------------------------
	//
	// Function
	//		Name:    WinNamedPipeListener::Accept(int)
	//		Purpose: Accepts a connection, returning a pointer to
	//			 a class of the specified type. May return a
	//			 null pointer if a signal happens, or there's
	//			 a timeout. Timeout specified in
	//			 milliseconds, defaults to infinite time.
	//		Created: 2003/07/31
	//
	// ------------------------------------------------------------------
	std::auto_ptr<WinNamedPipeStream> Accept(int Timeout = INFTIM,
		const char* pLogMsgOut = NULL)
	{
		if(!mapPipeName.get())
		{
			THROW_EXCEPTION(ServerException, BadSocketHandle);
		}

		BOOL connected = FALSE;
		std::auto_ptr<WinNamedPipeStream> mapStream;

		if (!mapOverlapConnect.get())
		{
			// start a new connect operation
			mapOverlapConnect.reset(new OverlappedIO());
			connected = ConnectNamedPipe(mPipeHandle,
				&mapOverlapConnect->mOverlapped);

			if (connected == FALSE)
			{
				if (GetLastError() == ERROR_PIPE_CONNECTED)
				{
					connected = TRUE;
				}
				else if (GetLastError() != ERROR_IO_PENDING)
				{
					BOX_LOG_WIN_ERROR("Failed to connect "
						"named pipe");
					THROW_EXCEPTION(ServerException,
						SocketAcceptError);
				}
			}
		}

		if (connected == FALSE)
		{
			// wait for connection
			DWORD result = WaitForSingleObject(
				mapOverlapConnect->mOverlapped.hEvent,
				(Timeout == INFTIM) ? INFINITE : Timeout);

			if (result == WAIT_OBJECT_0)
			{
				DWORD dummy;

				if (!GetOverlappedResult(mPipeHandle,
					&mapOverlapConnect->mOverlapped,
					&dummy, TRUE))
				{
					BOX_LOG_WIN_ERROR("Failed to get "
						"overlapped connect result");
					THROW_EXCEPTION(ServerException,
						SocketAcceptError);
				}
				
				connected = TRUE;
			}
			else if (result == WAIT_TIMEOUT)
			{
				return mapStream; // contains NULL
			}
			else if (result == WAIT_ABANDONED)
			{
				BOX_ERROR("Wait for named pipe connection "
					"was abandoned by the system");
				THROW_EXCEPTION(ServerException,
					SocketAcceptError);
			}
			else if (result == WAIT_FAILED)
			{
				BOX_LOG_WIN_ERROR("Failed to wait for named "
					"pipe connection");
				THROW_EXCEPTION(ServerException,
					SocketAcceptError);
			}
			else
			{
				BOX_ERROR("Failed to wait for named pipe "
					"connection: unknown return code " <<
					result);
				THROW_EXCEPTION(ServerException,
					SocketAcceptError);
			}
		}

		ASSERT(connected == TRUE);

		mapStream.reset(new WinNamedPipeStream(mPipeHandle));
		mPipeHandle = CreatePipeHandle(*mapPipeName);
		mapOverlapConnect.reset();

		return mapStream;
	}
};

#include "MemLeakFindOff.h"

#endif // WINNAMEDPIPELISTENER__H
