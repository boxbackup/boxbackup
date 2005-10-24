// --------------------------------------------------------------------------
//
// File
//		Name:    Socket.h
//		Purpose: Socket related stuff
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef SOCKET__H
#define SOCKET__H

#ifdef WIN32
#include "emu.h"
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#endif

#include <string>

typedef union {
	struct sockaddr sa_generic;
	struct sockaddr_in sa_inet;
#ifndef WIN32
	struct sockaddr_un sa_unix;
#endif
} SocketAllAddr;

// --------------------------------------------------------------------------
//
// Namespace
//		Name:    Socket
//		Purpose: Socket utilities
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
namespace Socket
{
	enum
	{
		TypeINET = 1,
		TypeUNIX = 2
	};

	void NameLookupToSockAddr(SocketAllAddr &addr, int &sockDomain, int Type, const char *Name, int Port, int &rSockAddrLenOut);
	void LogIncomingConnection(const struct sockaddr *addr, socklen_t addrlen);
	std::string IncomingConnectionLogMessage(const struct sockaddr *addr, socklen_t addrlen);
};

#endif // SOCKET__H

