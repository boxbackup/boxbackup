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

#include <climits>

#include "BoxTime.h"
#include "IOStream.h"
#include "Socket.h"

#ifdef WIN32
	typedef SOCKET tOSSocketHandle;
	#define INVALID_SOCKET_VALUE (tOSSocketHandle)(-1)
#else
	typedef int tOSSocketHandle;
	#define INVALID_SOCKET_VALUE -1
#endif

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
	
	void Open(Socket::Type Type, const std::string& rName, int Port = 0);
	void Attach(int socket);

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
	virtual void Close();
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	virtual void Shutdown(bool Read = true, bool Write = true);

	virtual bool GetPeerCredentials(uid_t &rUidOut, gid_t &rGidOut);

protected:
	void MarkAsReadClosed() {mReadClosed = true;}
	void MarkAsWriteClosed() {mWriteClosed = true;}
	void CheckForMissingTimeout(int Timeout);

	int PollTimeout(box_time_t Timeout)
	{
		if (Timeout < 0)
		{
			return 0;
		}
		else if (Timeout == IOStream::TimeOutInfinite || Timeout > INT_MAX)
		{
			return INFTIM;
		}
		else
		{
			return (int) Timeout;
		}
	}
	bool Poll(short Events, int Timeout);

private:
	tOSSocketHandle mSocketHandle;
	bool mReadClosed;
	bool mWriteClosed;

protected:
	off_t mBytesRead;
	off_t mBytesWritten;

public:
	off_t GetBytesRead() const {return mBytesRead;}
	off_t GetBytesWritten() const {return mBytesWritten;}
	void ResetCounters() {mBytesRead = mBytesWritten = 0;}
	bool IsOpened() { return mSocketHandle != INVALID_SOCKET_VALUE; }
	
	/**
	 * Only for use by NiceSocketStream!
	 */
	tOSSocketHandle GetSocketHandle();
};

#endif // SOCKETSTREAM__H

