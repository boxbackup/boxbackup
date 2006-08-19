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
	~WinNamedPipeStream();

	// server side - create the named pipe and listen for connections
	void Accept(const wchar_t* Name);

	// client side - connect to a waiting server
	void Connect(const wchar_t* Name);

	// both sides
	virtual int Read(void *pBuffer, int NBytes, 
		int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const void *pBuffer, int NBytes);
	virtual void Close();
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();
	bool IsConnected() { return mIsConnected; }

protected:
	HANDLE GetSocketHandle();
	void MarkAsReadClosed()  {mReadClosed  = true;}
	void MarkAsWriteClosed() {mWriteClosed = true;}

private:
	WinNamedPipeStream(const WinNamedPipeStream &rToCopy) 
		{ /* do not call */ }

	HANDLE mSocketHandle;
	bool mReadClosed;
	bool mWriteClosed;
	bool mIsServer;
	bool mIsConnected;
};

#endif // WINNAMEDPIPESTREAM__H
