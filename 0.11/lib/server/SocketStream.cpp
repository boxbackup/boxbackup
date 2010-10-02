// --------------------------------------------------------------------------
//
// File
//		Name:    SocketStream.cpp
//		Purpose: I/O stream interface for sockets
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <sys/types.h>
#include <errno.h>
#include <string.h>

#ifndef WIN32
	#include <poll.h>
#endif

#ifdef HAVE_UCRED_H
	#include <ucred.h>
#endif

#include "SocketStream.h"
#include "ServerException.h"
#include "CommonException.h"
#include "Socket.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::SocketStream()
//		Purpose: Constructor (create stream ready for Open() call)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
SocketStream::SocketStream()
	: mSocketHandle(INVALID_SOCKET_VALUE),
	  mReadClosed(false),
	  mWriteClosed(false),
	  mBytesRead(0),
	  mBytesWritten(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::SocketStream(int)
//		Purpose: Create stream from existing socket handle
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
SocketStream::SocketStream(int socket)
	: mSocketHandle(socket),
	  mReadClosed(false),
	  mWriteClosed(false),
	  mBytesRead(0),
	  mBytesWritten(0)
{
	if(socket < 0)
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::SocketStream(const SocketStream &)
//		Purpose: Copy constructor (dup()s socket)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
SocketStream::SocketStream(const SocketStream &rToCopy)
	: mSocketHandle(::dup(rToCopy.mSocketHandle)),
	  mReadClosed(rToCopy.mReadClosed),
	  mWriteClosed(rToCopy.mWriteClosed),
	  mBytesRead(rToCopy.mBytesRead),
	  mBytesWritten(rToCopy.mBytesWritten)

{
	if(rToCopy.mSocketHandle < 0)
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle);
	}
	if(mSocketHandle == INVALID_SOCKET_VALUE)
	{
		THROW_EXCEPTION(ServerException, DupError);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::~SocketStream()
//		Purpose: Destructor, closes stream if open
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
SocketStream::~SocketStream()
{
	if(mSocketHandle != INVALID_SOCKET_VALUE)
	{
		Close();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::Attach(int)
//		Purpose: Attach a socket handle to this stream
//		Created: 11/12/03
//
// --------------------------------------------------------------------------
void SocketStream::Attach(int socket)
{
	if(mSocketHandle != INVALID_SOCKET_VALUE) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}

	ResetCounters();

	mSocketHandle = socket;
	mReadClosed = false;
	mWriteClosed = false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::Open(Socket::Type, char *, int)
//		Purpose: Opens a connection to a listening socket (INET or UNIX)
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void SocketStream::Open(Socket::Type Type, const std::string& rName, int Port)
{
	if(mSocketHandle != INVALID_SOCKET_VALUE) 
	{
		THROW_EXCEPTION(ServerException, SocketAlreadyOpen)
	}
	
	// Setup parameters based on type, looking up names if required
	int sockDomain = 0;
	SocketAllAddr addr;
	int addrLen = 0;
	Socket::NameLookupToSockAddr(addr, sockDomain, Type, rName, Port, addrLen);

	// Create the socket
	mSocketHandle = ::socket(sockDomain, SOCK_STREAM,
		0 /* let OS choose protocol */);
	if(mSocketHandle == INVALID_SOCKET_VALUE)
	{
		BOX_LOG_SYS_ERROR("Failed to create a network socket");
		THROW_EXCEPTION(ServerException, SocketOpenError)
	}
	
	// Connect it
	if(::connect(mSocketHandle, &addr.sa_generic, addrLen) == -1)
	{
		// Dispose of the socket
#ifdef WIN32
		DWORD err = WSAGetLastError();
		::closesocket(mSocketHandle);
		BOX_LOG_WIN_ERROR_NUMBER("Failed to connect to socket " 
			"(type " << Type << ", name " << rName <<
			", port " << Port << ")", err);
#else // !WIN32
		BOX_LOG_SYS_ERROR("Failed to connect to socket (type " <<
			Type << ", name " << rName << ", port " << Port <<
			")");
		::close(mSocketHandle);
#endif // WIN32

		mSocketHandle = INVALID_SOCKET_VALUE;
		THROW_EXCEPTION(ConnectionException, Conn_SocketConnectError)
	}

	ResetCounters();

	mReadClosed = false;
	mWriteClosed = false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::Read(void *pBuffer, int NBytes)
//		Purpose: Reads data from stream. Maybe returns less than asked for.
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
int SocketStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	if(mSocketHandle == INVALID_SOCKET_VALUE) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}

	if(Timeout != IOStream::TimeOutInfinite)
	{
		struct pollfd p;
		p.fd = mSocketHandle;
		p.events = POLLIN;
		p.revents = 0;
		switch(::poll(&p, 1, (Timeout == IOStream::TimeOutInfinite)?INFTIM:Timeout))
		{
		case -1:
			// error
			if(errno == EINTR)
			{
				// Signal. Just return 0 bytes
				return 0;
			}
			else
			{
				// Bad!
				BOX_LOG_SYS_ERROR("Failed to poll socket");
				THROW_EXCEPTION(ServerException,
					SocketPollError)
			}
			break;
			
		case 0:
			// no data
			return 0;
			break;
			
		default:
			// good to go!
			break;
		}
	}

#ifdef WIN32
	int r = ::recv(mSocketHandle, (char*)pBuffer, NBytes, 0);
#else
	int r = ::read(mSocketHandle, pBuffer, NBytes);
#endif
	if(r == -1)
	{
		if(errno == EINTR)
		{
			// Nothing could be read
			return 0;
		}
		else
		{
			// Other error
			BOX_LOG_SYS_ERROR("Failed to read from socket");
			THROW_EXCEPTION(ConnectionException,
				Conn_SocketReadError);
		}
	}

	// Closed for reading?
	if(r == 0)
	{
		mReadClosed = true;
	}
	
	mBytesRead += r;
	return r;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::Write(void *pBuffer, int NBytes)
//		Purpose: Writes data, blocking until it's all done.
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void SocketStream::Write(const void *pBuffer, int NBytes)
{
	if(mSocketHandle == INVALID_SOCKET_VALUE) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	
	// Buffer in byte sized type.
	ASSERT(sizeof(char) == 1);
	const char *buffer = (char *)pBuffer;
	
	// Bytes left to send
	int bytesLeft = NBytes;
	
	while(bytesLeft > 0)
	{
		// Try to send.
#ifdef WIN32
		int sent = ::send(mSocketHandle, buffer, bytesLeft, 0);
#else
		int sent = ::write(mSocketHandle, buffer, bytesLeft);
#endif
		if(sent == -1)
		{
			// Error.
			mWriteClosed = true;	// assume can't write again
			BOX_LOG_SYS_ERROR("Failed to write to socket");
			THROW_EXCEPTION(ConnectionException,
				Conn_SocketWriteError);
		}
		
		// Knock off bytes sent
		bytesLeft -= sent;
		// Move buffer pointer
		buffer += sent;

		mBytesWritten += sent;
		
		// Need to wait until it can send again?
		if(bytesLeft > 0)
		{
			BOX_TRACE("Waiting to send data on socket " << 
				mSocketHandle << " (" << bytesLeft <<
				" of " << NBytes << " bytes left)");
			
			// Wait for data to send.
			struct pollfd p;
			p.fd = mSocketHandle;
			p.events = POLLOUT;
			p.revents = 0;
			
			if(::poll(&p, 1, 16000 /* 16 seconds */) == -1)
			{
				// Don't exception if it's just a signal
				if(errno != EINTR)
				{
					BOX_LOG_SYS_ERROR("Failed to poll "
						"socket");
					THROW_EXCEPTION(ServerException,
						SocketPollError)
				}
			}
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::Close()
//		Purpose: Closes connection to remote socket
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void SocketStream::Close()
{
	if(mSocketHandle == INVALID_SOCKET_VALUE) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
#ifdef WIN32
	if(::closesocket(mSocketHandle) == -1)
#else
	if(::close(mSocketHandle) == -1)
#endif
	{
		BOX_LOG_SYS_ERROR("Failed to close socket");
		// don't throw an exception here, assume that the socket was
		// already closed or closing.
	}
	mSocketHandle = INVALID_SOCKET_VALUE;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::Shutdown(bool, bool)
//		Purpose: Shuts down a socket for further reading and/or writing
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void SocketStream::Shutdown(bool Read, bool Write)
{
	if(mSocketHandle == INVALID_SOCKET_VALUE) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	
	// Do anything?
	if(!Read && !Write) return;
	
	int how = SHUT_RDWR;
	if(Read && !Write) how = SHUT_RD;
	if(!Read && Write) how = SHUT_WR;
	
	// Shut it down!
	if(::shutdown(mSocketHandle, how) == -1)
	{
		BOX_LOG_SYS_ERROR("Failed to shutdown socket");
		THROW_EXCEPTION(ConnectionException, Conn_SocketShutdownError)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::StreamDataLeft()
//		Purpose: Still capable of reading data?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool SocketStream::StreamDataLeft()
{
	return !mReadClosed;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::StreamClosed()
//		Purpose: Connection been closed?
//		Created: 2003/08/02
//
// --------------------------------------------------------------------------
bool SocketStream::StreamClosed()
{
	return mWriteClosed;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::GetSocketHandle()
//		Purpose: Returns socket handle for this stream (derived classes only).
//				 Will exception if there's no valid socket.
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
tOSSocketHandle SocketStream::GetSocketHandle()
{
	if(mSocketHandle == INVALID_SOCKET_VALUE) 
	{
		THROW_EXCEPTION(ServerException, BadSocketHandle)
	}
	return mSocketHandle;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    SocketStream::GetPeerCredentials(uid_t &, gid_t &)
//		Purpose: Returns true if the peer credientials are available.
//				 (will work on UNIX domain sockets only)
//		Created: 19/2/04
//
// --------------------------------------------------------------------------
bool SocketStream::GetPeerCredentials(uid_t &rUidOut, gid_t &rGidOut)
{
#ifdef HAVE_GETPEEREID
	uid_t remoteEUID = 0xffff;
	gid_t remoteEGID = 0xffff;

	if(::getpeereid(mSocketHandle, &remoteEUID, &remoteEGID) == 0)
	{
		rUidOut = remoteEUID;
		rGidOut = remoteEGID;
		return true;
	}
#endif

#if HAVE_DECL_SO_PEERCRED
	struct ucred cred;
	socklen_t credLen = sizeof(cred);

	if(::getsockopt(mSocketHandle, SOL_SOCKET, SO_PEERCRED, &cred,
		&credLen) == 0)
	{
		rUidOut = cred.uid;
		rGidOut = cred.gid;
		return true;
	}

	BOX_LOG_SYS_ERROR("Failed to get peer credentials on socket");
#endif

#if defined HAVE_UCRED_H && HAVE_GETPEERUCRED
	ucred_t *pucred = NULL;
	if(::getpeerucred(mSocketHandle, &pucred) == 0)
	{
		rUidOut = ucred_geteuid(pucred);
		rGidOut = ucred_getegid(pucred);
		ucred_free(pucred);
		if (rUidOut == -1 || rGidOut == -1)
		{
			BOX_ERROR("Failed to get peer credentials on "
				"socket: insufficient information");
			return false;
		}
		return true;
	}

	BOX_LOG_SYS_ERROR("Failed to get peer credentials on socket");
#endif

	// Not available
	return false;
}

