// --------------------------------------------------------------------------
//
// File
//		Name:    SocketStream.h
//		Purpose: I/O stream interface for sockets
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef SOCKETSTREAM__H
#define SOCKETSTREAM__H

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    SocketStream
//		Purpose: Stream interface for sockets
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
class SocketStream : public IOStream
{
public:
	SocketStream();
	SocketStream(int socket);
	SocketStream(const SocketStream &rToCopy);
	~SocketStream();
	
	void Open(int Type, const char *Name, int Port = 0);
	void Attach(int socket);

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const void *pBuffer, int NBytes);
	virtual void Close();
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	virtual void Shutdown(bool Read = true, bool Write = true);

	virtual bool GetPeerCredentials(uid_t &rUidOut, gid_t &rGidOut);

protected:
#ifdef WIN32
	SOCKET GetSocketHandle();
#else
	int GetSocketHandle();
#endif
	void MarkAsReadClosed() {mReadClosed = true;}
	void MarkAsWriteClosed() {mWriteClosed = true;}

private:
#ifdef WIN32
	SOCKET mSocketHandle;
#else
	int mSocketHandle;
#endif
	bool mReadClosed;
	bool mWriteClosed;
};

#endif // SOCKETSTREAM__H

