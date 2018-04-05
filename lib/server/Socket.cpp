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

#ifdef WIN32
	#include <ws2tcpip.h> // for InetNtop
#endif

#ifndef WIN32
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <string.h>
#include <stdio.h>

#include "autogen_ConnectionException.h"
#include "autogen_ServerException.h"
#include "Exception.h"
#include "Socket.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    Socket::NameLookupToSockAddr(SocketAllAddr &, int,
//			 char *, int)
//		Purpose: Sets up a sockaddr structure given a name and type
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void Socket::NameLookupToSockAddr(SocketAllAddr &addr, int &sockDomain,
	enum Type Type, const std::string& rName, int Port,
	int &rSockAddrLenOut)
{
	int sockAddrLen = 0;

	switch(Type)
	{
	case TypeINET:
		sockDomain = AF_INET;
		{
			// Lookup hostname
			struct hostent *phost = ::gethostbyname(rName.c_str());
			if(phost != NULL && phost->h_addr_list[0] != 0)
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
				THROW_SOCKET_ERROR("Failed to resolve hostname: " << rName,
					ConnectionException, SocketNameLookupError);
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

std::string Socket::NamePretty(enum Type type, const std::string& rName,
	const SocketAllAddr &addr)
{
	if(type == Socket::TypeINET)
	{
		// For an IP socket, try to include the resolved IP address as well as the hostname:
		int sockDomain = AF_INET;
		char name_buf[256];

#ifdef WIN32
		SocketAllAddr addr_copy = addr;
		const char* addr_str = InetNtop(sockDomain, &addr_copy.sa_inet.sin_addr, name_buf,
			sizeof(name_buf));
#else
		const char* addr_str = inet_ntop(sockDomain, &addr.sa_inet.sin_addr, name_buf,
			sizeof(name_buf));
#endif

		std::ostringstream name_oss;
		name_oss << rName << " (";
		if(addr_str == NULL)
		{
			name_oss << "failed to convert IP address to string";
		}
		else
		{
			name_oss << addr_str;
		}
		name_oss << ")";
		return name_oss.str();
	}
	else
	{
		// For a UNIX socket, the path is all we need to show the user:
		return rName;
	}
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
	BOX_INFO("Incoming connection from " <<
		IncomingConnectionLogMessage(addr, addrlen));
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
		return std::string("local (UNIX socket)");
		break;		
	
	case AF_INET:
		{
			sockaddr_in *a = (sockaddr_in*)addr;
			std::ostringstream oss;
			oss << inet_ntoa(a->sin_addr) << " port " <<
				ntohs(a->sin_port);
			return oss.str();
		}
		break;		
	
	default:
		{
			std::ostringstream oss;
			oss << "unknown socket type " << addr->sa_family;
			return oss.str();
		}
		break;
	}
	
	// Dummy.
	return std::string();
}

