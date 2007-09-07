// --------------------------------------------------------------------------
//
// File
//		Name:    LocalProcessStream.cpp
//		Purpose: Opens a process, and presents stdin/stdout as a stream.
//		Created: 12/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_SYS_SOCKET_H
	#include <sys/socket.h>
#endif

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "LocalProcessStream.h"
#include "autogen_ServerException.h"
#include "Utils.h"

#ifdef WIN32
	#include "FileStream.h"
#else
	#include "SocketStream.h"
#endif

#include "MemLeakFindOn.h"

#define MAX_ARGUMENTS	64

// --------------------------------------------------------------------------
//
// Function
//		Name:    LocalProcessStream(const char *, pid_t &)
//		Purpose: Run a new process, and return a stream giving access
//			 to its stdin and stdout (stdout and stderr on 
//			 Win32). Returns the PID of the new process -- this
//			 must be waited on at some point to avoid zombies
//			 (except on Win32).
//		Created: 12/3/04
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> LocalProcessStream(const char *CommandLine, pid_t &rPidOut)
{
#ifndef WIN32

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

#else // WIN32

	SECURITY_ATTRIBUTES secAttr; 
	secAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	secAttr.bInheritHandle = TRUE; 
	secAttr.lpSecurityDescriptor = NULL; 

	HANDLE writeInChild, readFromChild;
	if(!CreatePipe(&readFromChild, &writeInChild, &secAttr, 0))
	{
		BOX_ERROR("Failed to CreatePipe for child process: " <<
			GetErrorMessage(GetLastError()));
		THROW_EXCEPTION(ServerException, SocketPairFailed)
	}
	SetHandleInformation(readFromChild, HANDLE_FLAG_INHERIT, 0);

	PROCESS_INFORMATION procInfo; 
	STARTUPINFO startupInfo;

	ZeroMemory(&procInfo,    sizeof(procInfo));
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb         = sizeof(startupInfo);
	startupInfo.hStdError  = writeInChild;
	startupInfo.hStdOutput = writeInChild;
	startupInfo.hStdInput  = INVALID_HANDLE_VALUE;
	startupInfo.dwFlags   |= STARTF_USESTDHANDLES;

	CHAR* commandLineCopy = (CHAR*)malloc(strlen(CommandLine) + 1);
	strcpy(commandLineCopy, CommandLine);

	BOOL result = CreateProcess(NULL, 
		commandLineCopy, // command line 
		NULL,          // process security attributes 
		NULL,          // primary thread security attributes 
		TRUE,          // handles are inherited 
		0,             // creation flags 
		NULL,          // use parent's environment 
		NULL,          // use parent's current directory 
		&startupInfo,  // STARTUPINFO pointer 
		&procInfo);    // receives PROCESS_INFORMATION 

	free(commandLineCopy);
   
	if(!result)
	{
		BOX_ERROR("Failed to CreateProcess: '" << CommandLine <<
			"': " << GetErrorMessage(GetLastError()));
		CloseHandle(writeInChild);
		CloseHandle(readFromChild);
		THROW_EXCEPTION(ServerException, ServerForkError)
	}

	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);
	CloseHandle(writeInChild);

	rPidOut = (int)(procInfo.dwProcessId);

	std::auto_ptr<IOStream> stream(new FileStream(readFromChild));
	return stream;

#endif // ! WIN32
}




