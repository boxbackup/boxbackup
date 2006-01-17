// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    Socket.cpp
//		Purpose: Socket related stuff
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
void Socket::NameLookupToSockAddr(SocketAllAddr &addr, int &sockDomain, int Type, const char *Name, int Port, int &rSockAddrLenOut)
{
	int sockAddrLen = 0;

	switch(Type)
	{
	case TypeINET:
		sockDomain = AF_INET;
		{
			// Lookup hostname
			struct hostent *phost = ::gethostbyname(Name);
			if(phost != NULL)
			{
				if(phost->h_addr_list[0] != 0)
				{
					sockAddrLen = sizeof(addr.sa_inet);
#ifndef PLATFORM_sockaddr_NO_len
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
	
	case TypeUNIX:
		sockDomain = AF_UNIX;
		{
			// Check length of name is OK
			unsigned int nameLen = ::strlen(Name);
			if(nameLen >= (sizeof(addr.sa_unix.sun_path) - 1))
			{
				THROW_EXCEPTION(ServerException, SocketNameUNIXPathTooLong);
			}
			sockAddrLen = nameLen + (((char*)(&(addr.sa_unix.sun_path[0]))) - ((char*)(&addr.sa_unix)));
#ifndef PLATFORM_sockaddr_NO_len
			addr.sa_unix.sun_len = sockAddrLen;
#endif
			addr.sa_unix.sun_family = PF_UNIX;
			::strcpy(addr.sa_unix.sun_path, Name);
		}
		break;
		
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
		::syslog(LOG_INFO, "Incoming connection from local (UNIX socket)");
		break;		
	
	case AF_INET:
		{
			sockaddr_in *a = (sockaddr_in*)addr;
			::syslog(LOG_INFO, "Incoming connection from %s port %d", inet_ntoa(a->sin_addr), ntohs(a->sin_port));
		}
		break;		
	
	default:
		::syslog(LOG_INFO, "Incoming connection of unknown type");
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

