// --------------------------------------------------------------------------
//
// File
//		Name:    LocalProcessStream.cpp
//		Purpose: Opens a process, and presents stdin/stdout as a stream.
//		Created: 12/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifndef WIN32
#include <sys/socket.h>
#endif
#include <unistd.h>

#include "LocalProcessStream.h"
#include "SocketStream.h"
#include "autogen_ServerException.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#define MAX_ARGUMENTS	64

// --------------------------------------------------------------------------
//
// Function
//		Name:    LocalProcessStream(const char *, pid_t &)
//		Purpose: Run a new process, and return a stream giving access to it's
//				 stdin and stdout. Returns the PID of the new process -- this
//				 must be waited on at some point to avoid zombies.
//		Created: 12/3/04
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> LocalProcessStream(const char *CommandLine, pid_t &rPidOut)
{
	// Split up command
	std::vector<std::string> command;
	SplitString(std::string(CommandLine), ' ', command);
#ifndef WIN32
	// Build arguments
	char *args[MAX_ARGUMENTS + 4];
	{
		int a = 0;
		std::vector<std::string>::const_iterator i(command.begin());
		while(a < MAX_ARGUMENTS && i != command.end())
		{
			args[a++] = (char*)(*(i++)).c_str();
		}
		args[a] = NULL;
	}

	// Create a socket pair to communicate over.
	int sv[2] = {-1,-1};
	if(::socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sv) != 0)
	{
		THROW_EXCEPTION(ServerException, SocketPairFailed)
	}
	
	std::auto_ptr<IOStream> stream(new SocketStream(sv[0]));

	// Fork
	pid_t pid = 0;
	switch(pid = vfork())
	{
	case -1:	// error
		::close(sv[0]);
		::close(sv[1]);
		THROW_EXCEPTION(ServerException, ServerForkError)
		break;
		
	case 0:		// child
		// Close end of the socket not being used
		::close(sv[0]);
		// Duplicate the file handles to stdin and stdout
		if(sv[1] != 0) ::dup2(sv[1], 0);
		if(sv[1] != 1) ::dup2(sv[1], 1);
		// Close the now redundant socket
		if(sv[1] != 0 && sv[1] != 1)
		{
			::close(sv[1]);
		}
		// Execute command!
		::execv(args[0], args);
		::_exit(127);	// report error
		break;
	
	default:
		// just continue...
		break;
	}
	
	// Close the file descriptor not being used
	::close(sv[1]);

	// Return the stream object and PID
	rPidOut = pid;
	return stream;	
#else
	::syslog(LOG_ERR, "vfork not implimented - LocalProcessStream.cpp");
	std::auto_ptr<IOStream> stream;
	return stream;
#endif

}




