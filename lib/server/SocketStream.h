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

#ifdef HAVE_SYS_POLL_H
#	include <sys/poll.h>
#endif

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
	using IOStream::Write;

	virtual void Close();
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	virtual void Shutdown(bool Read = true, bool Write = true);

	virtual bool GetPeerCredentials(uid_t &rUidOut, gid_t &rGidOut);

protected:
	void MarkAsReadClosed() {mReadClosed = true;}
	void MarkAsWriteClosed() {mWriteClosed = true;}
	void CheckForMissingTimeout(int Timeout);

	// Converts a timeout in milliseconds (or IOStream::TimeOutInfinite)
	// into one that can be passed to poll() (also in milliseconds), also
	// compensating for time elapsed since the wait should have started,
	// if known.
	int PollTimeout(int timeout, box_time_t start_time)
	{
		if (timeout == IOStream::TimeOutInfinite)
		{
			return INFTIM;
		}

		if (start_time == 0)
		{
			return timeout; // no adjustment possible
		}

		box_time_t end_time = start_time + MilliSecondsToBoxTime(timeout);
		box_time_t now = GetCurrentBoxTime();
		box_time_t remaining = end_time - now;

		if (remaining < 0)
		{
			return 0; // no delay
		}
		else if (BoxTimeToMilliSeconds(remaining) > INT_MAX)
		{
			return INT_MAX;
		}
		else
		{
			return (int) BoxTimeToMilliSeconds(remaining);
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

