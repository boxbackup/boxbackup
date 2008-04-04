// --------------------------------------------------------------------------
//
// File
//		Name:    SocketListen.h
//		Purpose: Stream based sockets for servers
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef SOCKETLISTEN__H
#define SOCKETLISTEN__H

#include <errno.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#ifdef HAVE_KQUEUE
	#include <sys/event.h>
	#include <sys/time.h>
#endif

#ifndef WIN32
	#include <poll.h>
#endif

#include <new>
#include <memory>
#include <string>

#include "Socket.h"
#include "ServerException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    _NoSocketLocking
//		Purpose: Default locking class for SocketListen
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
class _NoSocketLocking
{
public:
	_NoSocketLocking(int sock)
	{
	}
	
	~_NoSocketLocking()
	{
	}
	
	bool HaveLock()
	{
		return true;
	}
	
private:
	_NoSocketLocking(const _NoSocketLocking &rToCopy)
	{
	}
};


// --------------------------------------------------------------------------
//
// Class
//		Name:    SocketListen
//		Purpose: 
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
template<typename SocketType, int ListenBacklog = 128, typename SocketLockingType = _NoSocketLocking, int MaxMultiListenSockets = 16>
class SocketListen
{
public:
	// Initialise
	SocketListen()
		: mSocketHandle(-1)
	{
	}
	// Close socket nicely
	~SocketListen()
	{
		Close();
	}
private:
	SocketListen(const SocketListen &rToCopy)
	{
	}
public:

	enum
	{
		MaxMultipleListenSockets = MaxMultiListenSockets
	};

	void Close()
	{
		if(mSocketHandle != -1)
		{
#ifdef WIN32
			if(::closesocket(mSocketHandle) == -1)
#else
			if(::close(mSocketHandle) == -1)
#endif
			{
				BOX_LOG_SYS_ERROR("Failed to close network "
					"socket");
				THROW_EXCEPTION(ServerException,
					SocketCloseError)
			}
		}
		mSocketHandle = -1;
	}

	// ------------------------------------------------------------------
	//
	// Function
	//		Name:    SocketListen::Listen(int, char*, int)
	//		Purpose: Initialises, starts the socket listening.
	//		Created: 2003/07/31
	//
	// ------------------------------------------------------------------
	void Listen(int Type, const char *Name, int Port = 0)
	{
		if(mSocketHandle != -1)
		{
			THROW_EXCEPTION(ServerException, SocketAlreadyOpen);
		}
		
		// Setup parameters based on type, looking up names if required
		int sockDomain = 0;
		SocketAllAddr addr;
		int addrLen = 0;
		Socket::NameLookupToSockAddr(addr, sockDomain, Type, Name,
			Port, addrLen);
	
		// Create the socket
		mSocketHandle = ::socket(sockDomain, SOCK_STREAM,
			0 /* let OS choose protocol */);
		if(mSocketHandle == -1)
		{
			BOX_LOG_SYS_ERROR("Failed to create a network socket");
			THROW_EXCEPTION(ServerException, SocketOpenError)
		}
		
		// Set an option to allow reuse (useful for -HUP situations!)
#ifdef WIN32
		if(::setsockopt(mSocketHandle, SOL_SOCKET, SO_REUSEADDR, "",
			0) == -1)
#else
		int option = true;
		if(::setsockopt(mSocketHandle, SOL_SOCKET, SO_REUSEADDR,
			&option, sizeof(option)) == -1)
#endif
		{
			BOX_LOG_SYS_ERROR("Failed to set socket options");
			THROW_EXCEPTION(ServerException, SocketOpenError)
		}

		// Bind it to the right port, and start listening
		if(::bind(mSocketHandle, &addr.sa_generic, addrLen) == -1
			|| ::listen(mSocketHandle, ListenBacklog) == -1)
		{
			// Dispose of the socket
			::close(mSocketHandle);
			mSocketHandle = -1;
			THROW_EXCEPTION(ServerException, SocketBindError)
		}	
	}
	
	// ------------------------------------------------------------------
	//
	// Function
	//		Name:    SocketListen::Accept(int)
	//		Purpose: Accepts a connection, returning a pointer to
	//			 a class of the specified type. May return a
	//			 null pointer if a signal happens, or there's
	//			 a timeout. Timeout specified in
	//			 milliseconds, defaults to infinite time.
	//		Created: 2003/07/31
	//
	// ------------------------------------------------------------------
	std::auto_ptr<SocketType> Accept(int Timeout = INFTIM,
		std::string *pLogMsg = 0)
	{
		if(mSocketHandle == -1)
		{
			THROW_EXCEPTION(ServerException, BadSocketHandle);
		}
		
		// Do the accept, using the supplied locking type
		int sock;
		struct sockaddr addr;
		socklen_t addrlen = sizeof(addr);
		// BLOCK
		{
			SocketLockingType socklock(mSocketHandle);
			
			if(!socklock.HaveLock())
			{
				// Didn't get the lock for some reason.
				// Wait a while, then return nothing.
				BOX_ERROR("Failed to get a lock on incoming "
					"connection");
				::sleep(1);
				return std::auto_ptr<SocketType>();
			}
			
			// poll this socket
			struct pollfd p;
			p.fd = mSocketHandle;
			p.events = POLLIN;
			p.revents = 0;
			switch(::poll(&p, 1, Timeout))
			{
			case -1:
				// signal?
				if(errno == EINTR)
				{
					BOX_ERROR("Failed to accept "
						"connection: interrupted by "
						"signal");
					// return nothing
					return std::auto_ptr<SocketType>();
				}
				else
				{
					BOX_LOG_SYS_ERROR("Failed to poll "
						"connection");
					THROW_EXCEPTION(ServerException,
						SocketPollError)
				}
				break;
			case 0:	// timed out
				return std::auto_ptr<SocketType>();
				break;
			default:	// got some thing...
				// control flows on...
				break;
			}
			
			sock = ::accept(mSocketHandle, &addr, &addrlen);
		}

		// Got socket (or error), unlock (implicit in destruction)
		if(sock == -1)
		{
			BOX_LOG_SYS_ERROR("Failed to accept connection");
			THROW_EXCEPTION(ServerException, SocketAcceptError)
		}

		// Log it
		if(pLogMsg)
		{
			*pLogMsg = Socket::IncomingConnectionLogMessage(&addr,
				addrlen);
		}
		else
		{
			// Do logging ourselves
			Socket::LogIncomingConnection(&addr, addrlen);
		}

		return std::auto_ptr<SocketType>(new SocketType(sock));
	}
	
	// Functions to allow adding to WaitForEvent class, for efficient waiting
	// on multiple sockets.
#ifdef HAVE_KQUEUE
	// ------------------------------------------------------------------
	//
	// Function
	//		Name:    SocketListen::FillInKEevent
	//		Purpose: Fills in a kevent structure for this socket
	//		Created: 9/3/04
	//
	// ------------------------------------------------------------------
	void FillInKEvent(struct kevent &rEvent, int Flags = 0) const
	{
		EV_SET(&rEvent, mSocketHandle, EVFILT_READ, 0, 0, 0,
			(void*)this);
	}
#else
	// ------------------------------------------------------------------
	//
	// Function
	//		Name:    SocketListen::FillInPoll
	//		Purpose: Fills in the data necessary for a poll
	//			 operation
	//		Created: 9/3/04
	//
	// ------------------------------------------------------------------
	void FillInPoll(int &fd, short &events, int Flags = 0) const
	{
		fd = mSocketHandle;
		events = POLLIN;
	}
#endif
	
private:
	int mSocketHandle;
};

#include "MemLeakFindOff.h"

#endif // SOCKETLISTEN__H

