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
	virtual void Write(const void *pBuffer, int NBytes);
	virtual void WriteAllBuffered();
	virtual void Close();
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

protected:
	void MarkAsReadClosed()  {mReadClosed  = true;}
	void MarkAsWriteClosed() {mWriteClosed = true;}

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

public:
	static std::string sPipeNamePrefix;
};

#endif // WINNAMEDPIPESTREAM__H
