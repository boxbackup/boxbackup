// --------------------------------------------------------------------------
//
// File
//		Name:    MainHelper.cpp
//		Purpose: Helper stuff for main() programs
//		Created: 2017/02/19
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef WIN32
#	include <winsock2.h>
#endif

#include "autogen_CommonException.h"
#include "Logging.h"

// Windows requires winsock to be initialised before use, unlike every other platform.
void mainhelper_init_win32_sockets()
{
#ifdef WIN32
	WSADATA info;
	
	// Under Win32 we must initialise the Winsock library
	// before using it.
	
	if (WSAStartup(0x0101, &info) == SOCKET_ERROR) 
	{
		// throw error? perhaps give it its own id in the future
		DWORD wserrno = WSAGetLastError();
		THROW_WIN_ERROR_NUMBER("Failed to initialise Windows Sockets library", wserrno,
			CommonException, Internal)
	}
#endif
}

