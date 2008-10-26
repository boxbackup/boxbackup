// --------------------------------------------------------------------------
//
// File
//		Name:    Socket.cpp
//		Purpose: Socket related stuff
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <sys/types.h>
#ifndef WIN32
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <string.h>
#include <stdio.h>

#include "Socket.h"
#include "ServerException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    Socket::NameLookupToSockAddr(SocketAllAddr &, int, char *, int)
//		Purpose: Sets up a sockaddr structure given a name and type
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void Socket::NameLookupToSockAddr(SocketAllAddr &addr, int &sockDomain,
	int Type, const std::string& rName, int Port, int &rSockAddrLenOut)
{
	int sockAddrLen = 0;

	switch(Type)
	{
	case TypeINET:
		sockDomain = AF_INET;
		{
			// Lookup hostname
			struct hostent *phost = ::gethostbyname(rName.c_str());
			if(phost != NULL)
			{
				if(phost->h_addr_list[0] != 0)
				{
					sockAddrLen = sizeof(addr.sa_inet);
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
					addr.sa_inet.sin_len = sizeof(addr.sa_inet);
#endif
					addr.sa_inet.sin_family = PF_INET;
					addr.sa_inet.sin_port = htons(Port);
					addr.sa_inet.sin_addr = *((in_addr*)phost->h_addr_list[0]);
					for(unsigned int l = 0; l < sizeof(addr.sa_inet.sin_zero); ++l)
					{
						addr.sa_inet.sin_zero[l] = 0;
					}
				}
				else
				{
					THROW_EXCEPTION(ConnectionException, Conn_SocketNameLookupError);
				}
			}
			else
			{
				THROW_EXCEPTION(ConnectionException, Conn_SocketNameLookupError);
			}
		}
		break;
	
#ifndef WIN32
	case TypeUNIX:
		sockDomain = AF_UNIX;
		{
			// Check length of name is OK
			unsigned int nameLen = rName.length();
			if(nameLen >= (sizeof(addr.sa_unix.sun_path) - 1))
			{
				THROW_EXCEPTION(ServerException, SocketNameUNIXPathTooLong);
			}
			sockAddrLen = nameLen + (((char*)(&(addr.sa_unix.sun_path[0]))) - ((char*)(&addr.sa_unix)));
#ifdef HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
			addr.sa_unix.sun_len = sockAddrLen;
#endif
			addr.sa_unix.sun_family = PF_UNIX;
			::strncpy(addr.sa_unix.sun_path, rName.c_str(),
				sizeof(addr.sa_unix.sun_path) - 1);
			addr.sa_unix.sun_path[sizeof(addr.sa_unix.sun_path)-1] = 0;
		}
		break;
#endif
	
	default:
		THROW_EXCEPTION(CommonException, BadArguments)
		break;
	}
	
	// Return size of structure to caller
	rSockAddrLenOut = sockAddrLen;
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    Socket::LogIncomingConnection(const struct sockaddr *, socklen_t)
//		Purpose: Writes a message logging the connection to syslog
//		Created: 2003/08/01
//
// --------------------------------------------------------------------------
void Socket::LogIncomingConnection(const struct sockaddr *addr, socklen_t addrlen)
{
	if(addr == NULL) {THROW_EXCEPTION(CommonException, BadArguments)}

	switch(addr->sa_family)
	{
	case AF_UNIX:
		BOX_INFO("Incoming connection from local (UNIX socket)");
		break;		
	
	case AF_INET:
		{
			sockaddr_in *a = (sockaddr_in*)addr;
			BOX_INFO("Incoming connection from " <<
				inet_ntoa(a->sin_addr) << " port " <<
				ntohs(a->sin_port));
		}
		break;		
	
	default:
		BOX_WARNING("Incoming connection of unknown type");
		break;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Socket::IncomingConnectionLogMessage(const struct sockaddr *, socklen_t)
//		Purpose: Returns a string for use in log messages
//		Created: 2003/08/01
//
// --------------------------------------------------------------------------
std::string Socket::IncomingConnectionLogMessage(const struct sockaddr *addr, socklen_t addrlen)
{
	if(addr == NULL) {THROW_EXCEPTION(CommonException, BadArguments)}

	switch(addr->sa_family)
	{
	case AF_UNIX:
		return std::string("Incoming connection from local (UNIX socket)");
		break;		
	
	case AF_INET:
		{
			char msg[256];	// more than enough
			sockaddr_in *a = (sockaddr_in*)addr;
			sprintf(msg, "Incoming connection from %s port %d", inet_ntoa(a->sin_addr), ntohs(a->sin_port));
			return std::string(msg);
		}
		break;		
	
	default:
		return std::string("Incoming connection of unknown type");
		break;
	}
	
	// Dummy.
	return std::string();
}

