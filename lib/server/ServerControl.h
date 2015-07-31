#ifndef SERVER_CONTROL_H
#define SERVER_CONTROL_H

#include "Test.h"

bool HUPServer(int pid);
bool KillServer(int pid, bool WaitForProcess = false);
int StartDaemon(int current_pid, const std::string& cmd_line, const char* pid_file);
bool StopDaemon(int current_pid, const std::string& pid_file,
	const std::string& memleaks_file, bool wait_for_process);

#ifdef WIN32
	#include "WinNamedPipeStream.h"
	#include "IOStreamGetLine.h"
	#include "BoxPortsAndFiles.h"

	void SetNamedPipeName(const std::string& rPipeName);
	// bool SendCommands(const std::string& rCmd);
#endif // WIN32

#endif // SERVER_CONTROL_H
