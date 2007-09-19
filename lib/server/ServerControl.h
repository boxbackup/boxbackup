#ifndef SERVER_CONTROL_H
#define SERVER_CONTROL_H

#include "Test.h"

#ifdef WIN32

#include "WinNamedPipeStream.h"
#include "IOStreamGetLine.h"
#include "BoxPortsAndFiles.h"

static bool SendCommands(const std::string& rCmd)
{
	WinNamedPipeStream connection;

	try
	{
		connection.Connect(BOX_NAMED_PIPE_NAME);
	}
	catch(...)
	{
		printf("Failed to connect to daemon control socket.\n");
		return false;
	}

	// For receiving data
	IOStreamGetLine getLine(connection);
	
	// Wait for the configuration summary
	std::string configSummary;
	if(!getLine.GetLine(configSummary))
	{
		printf("Failed to receive configuration summary from daemon\n");
		return false;
	}

	// Was the connection rejected by the server?
	if(getLine.IsEOF())
	{
		printf("Server rejected the connection.\n");
		return false;
	}

	// Decode it
	int autoBackup, updateStoreInterval, minimumFileAge, maxUploadWait;
	if(::sscanf(configSummary.c_str(), "bbackupd: %d %d %d %d", 
			&autoBackup, &updateStoreInterval, 
			&minimumFileAge, &maxUploadWait) != 4)
	{
		printf("Config summary didn't decode\n");
		return false;
	}

	std::string cmds;
	bool expectResponse;

	if (rCmd != "")
	{
		cmds = rCmd;
		cmds += "\nquit\n";
		expectResponse = true;
	}
	else
	{
		cmds = "quit\n";
		expectResponse = false;
	}
	
	connection.Write(cmds.c_str(), cmds.size());
	
	// Read the response
	std::string line;
	bool statusOk = !expectResponse;

	while (expectResponse && !getLine.IsEOF() && getLine.GetLine(line))
	{
		// Is this an OK or error line?
		if (line == "ok")
		{
			statusOk = true;
		}
		else if (line == "error")
		{
			printf("ERROR (%s)\n", rCmd.c_str());
			break;
		}
		else
		{
			printf("WARNING: Unexpected response to command '%s': "
				"%s", rCmd.c_str(), line.c_str());
		}
	}
	
	return statusOk;
}

inline bool HUPServer(int pid)
{
	return SendCommands("reload");
}

inline bool KillServerInternal(int pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, false, pid);
	if (hProcess == NULL)
	{
		printf("Failed to open process %d: error %d\n",
			pid, (int)GetLastError());
		return false;
	}

	if (!TerminateProcess(hProcess, 1))
	{
		printf("Failed to terminate process %d: error %d\n",
			pid, (int)GetLastError());
		CloseHandle(hProcess);
		return false;
	}

	CloseHandle(hProcess);
	return true;
}

#else // !WIN32

inline bool HUPServer(int pid)
{
	if(pid == 0) return false;
	return ::kill(pid, SIGHUP) == 0;
}

inline bool KillServerInternal(int pid)
{
	if(pid == 0 || pid == -1) return false;
	bool killed = (::kill(pid, SIGTERM) == 0);
	TEST_THAT(killed);
	return killed;
}

#endif // WIN32

inline bool KillServer(int pid)
{
	if (!KillServerInternal(pid))
	{
		return false;
	}

	for (int i = 0; i < 30; i++)
	{
		if (!ServerIsAlive(pid)) break;
		::sleep(1);
		if (!ServerIsAlive(pid)) break;

		if (i == 0) 
		{
			printf("waiting for server to die");
		}

		printf(".");
		fflush(stdout);
	}

	if (!ServerIsAlive(pid))
	{
		printf("done.\n");
	}
	else
	{
		printf("failed!\n");
	}

	fflush(stdout);

	return !ServerIsAlive(pid);
}

#endif // SERVER_CONTROL_H
