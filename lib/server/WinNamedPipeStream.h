// --------------------------------------------------------------------------
//
// File
//		Name:    WinNamedPipeStream.h
//		Purpose: I/O stream interface for Win32 named pipes
//		Created: 2005/12/07
//
// --------------------------------------------------------------------------

#if ! defined WINNAMEDPIPESTREAM__H && defined WIN32
#define WINNAMEDPIPESTREAM__H

#include <list>

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    WinNamedPipeStream
//		Purpose: I/O stream interface for Win32 named pipes
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
class WinNamedPipeStream : public IOStream
{
public:
	WinNamedPipeStream();
	WinNamedPipeStream(HANDLE hNamedPipe);
	~WinNamedPipeStream();

	// server side - create the named pipe and listen for connections
	// use WinNamedPipeListener to do this instead.

	// client side - connect to a waiting server
	void Connect(const std::string& rName);

	// both sides
	virtual int Read(void *pBuffer, int NBytes, 
		int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	virtual void WriteAllBuffered();
	virtual void Close();
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	// Why not inherited from IOStream? Never mind, we want to enforce
	// supplying a timeout for network operations anyway.
	virtual void Write(const std::string& rBuffer, int Timeout)
	{
		IOStream::Write(rBuffer, Timeout);
	}

protected:
	void MarkAsReadClosed()  {mReadClosed  = true;}
	void MarkAsWriteClosed() {mWriteClosed = true;}
	bool WaitForOverlappedOperation(OVERLAPPED& Overlapped,
		int Timeout, int64_t* pBytesTransferred);
	void StartFirstRead();
	void StartOverlappedRead();

private:
	WinNamedPipeStream(const WinNamedPipeStream &rToCopy) 
		{ /* do not call */ }

	HANDLE mSocketHandle;
	HANDLE mReadableEvent;
	OVERLAPPED mReadOverlap;
	uint8_t mReadBuffer[4096];
	size_t  mBytesInBuffer;
	bool mReadClosed;
	bool mWriteClosed;
	bool mIsServer;
	bool mIsConnected;
	bool mNeedAnotherRead;

	class WriteInProgress {
	private:
		friend class WinNamedPipeStream;
		std::string mBuffer;
		OVERLAPPED mOverlap;
		WriteInProgress(const WriteInProgress& other); // do not call
	public:
		WriteInProgress(const std::string& dataToWrite)
		: mBuffer(dataToWrite)
		{
			// create the Writable event
			HANDLE writable_event = CreateEvent(NULL, TRUE, FALSE,
				NULL);
			if (writable_event == INVALID_HANDLE_VALUE)
			{
				BOX_LOG_WIN_ERROR("Failed to create the "
					"Writable event");
				THROW_EXCEPTION(CommonException, Internal)
			}

			memset(&mOverlap, 0, sizeof(mOverlap));
			mOverlap.hEvent = writable_event;
		}
		~WriteInProgress()
		{
			CloseHandle(mOverlap.hEvent);
		}
	};
	std::list<WriteInProgress*> mWritesInProgress;

public:
	static std::string sPipeNamePrefix;
};

#endif // WINNAMEDPIPESTREAM__H
