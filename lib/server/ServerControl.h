#ifndef SERVER_CONTROL_H
#define SERVER_CONTROL_H

#include "Test.h"

bool HUPServer(int pid);
bool KillServer(int pid);

#ifdef WIN32
	#include "WinNamedPipeStream.h"
	#include "IOStreamGetLine.h"
	#include "BoxPortsAndFiles.h"

	void SetNamedPipeName(const std::string& rPipeName);
	// bool SendCommands(const std::string& rCmd);
#endif // WIN32

#endif // SERVER_CONTROL_H
