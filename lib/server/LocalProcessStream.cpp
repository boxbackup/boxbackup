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
//		Name:    LocalProcessStream.cpp
//		Purpose: Opens a process, and presents stdin/stdout as a stream.
//		Created: 12/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/socket.h>
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
}




