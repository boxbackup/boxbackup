#include "Box.h"

#include <errno.h>

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#include <stdio.h>

#ifdef HAVE_SYS_TYPES_H
	#include <sys/types.h>
#endif

#ifdef HAVE_SYS_WAIT_H
	#include <sys/wait.h>
#endif

#include <string>

#include "BoxTime.h"
#include "IOStreamGetLine.h"
#include "ServerControl.h"
#include "Test.h"

#ifdef WIN32

#include "WinNamedPipeStream.h"
#include "BoxPortsAndFiles.h"

static std::string sPipeName;

void SetNamedPipeName(const std::string& rPipeName)
{
	sPipeName = rPipeName;
}

bool SendCommands(const std::string& rCmd)
{
	WinNamedPipeStream connection;

	try
	{
		connection.Connect(sPipeName);
	}
	catch(...)
	{
		BOX_ERROR("Failed to connect to daemon control socket");
		return false;
	}

	// For receiving data
	IOStreamGetLine getLine(connection);
	
	// Wait for the configuration summary
	std::string configSummary;
	while(true)
	{
		try
		{
			configSummary = getLine.GetLine(false /* no preprocess */);
			break;
		}
		catch(BoxException &e)
		{
			if(EXCEPTION_IS_TYPE(e, CommonException, SignalReceived))
			{
				// try again
				continue;
			}
			else if(EXCEPTION_IS_TYPE(e, CommonException, GetLineEOF))
			{
				BOX_ERROR("Server rejected the connection");
			}
			else
			{
				BOX_ERROR("Failed to receive configuration summary from daemon: " <<
					e.what());
			}

			return false;
		}
	}

	// Decode it
	int autoBackup, updateStoreInterval, minimumFileAge, maxUploadWait;
	if(::sscanf(configSummary.c_str(), "bbackupd: %d %d %d %d", 
			&autoBackup, &updateStoreInterval, 
			&minimumFileAge, &maxUploadWait) != 4)
	{
		BOX_ERROR("Config summary didn't decode");
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
	bool statusOk = !expectResponse;

	while (expectResponse)
	{
		std::string line;
		try
		{
			line = getLine.GetLine(false /* no preprocessing */,
				120000); // 2 minute timeout for tests
		}
		catch(BoxException &e)
		{
			if(EXCEPTION_IS_TYPE(e, CommonException, SignalReceived))
			{
				// try again
				continue;
			}
			else if(EXCEPTION_IS_TYPE(e, CommonException, GetLineEOF) ||
				EXCEPTION_IS_TYPE(e, CommonException, IOStreamTimedOut))
			{
				BOX_WARNING("Disconnected by daemon or timed out waiting for "
					"response: " << e.what());
				break;
			}
			else
			{
				throw;
			}
		}

		// Is this an OK or error line?
		if (line == "ok")
		{
			statusOk = true;
		}
		else if (line == "error")
		{
			BOX_ERROR(rCmd);
			break;
		}
		else
		{
			BOX_WARNING("Unexpected response to command '" <<
				rCmd << "': " << line)
		}
	}
	
	return statusOk;
}

bool HUPServer(int pid)
{
	return SendCommands("reload");
}

bool KillServerInternal(int pid)
{
	HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, false, pid);
	if (hProcess == NULL)
	{
		BOX_ERROR("Failed to open process " << pid << ": " <<
			GetErrorMessage(GetLastError()));
		return false;
	}

	if (!TerminateProcess(hProcess, 1))
	{
		BOX_ERROR("Failed to terminate process " << pid << ": " <<
			GetErrorMessage(GetLastError()));
		CloseHandle(hProcess);
		return false;
	}

	CloseHandle(hProcess);
	return true;
}

#else // !WIN32

bool HUPServer(int pid)
{
	if(pid == 0) return false;
	return ::kill(pid, SIGHUP) == 0;
}

bool KillServerInternal(int pid)
{
	if(pid == 0 || pid == -1) return false;
	bool killed = (::kill(pid, SIGTERM) == 0);
	if (!killed)
	{
		BOX_LOG_SYS_ERROR("Failed to kill process " << pid);
	}
	TEST_THAT(killed);
	return killed;
}

#endif // WIN32

bool KillServer(int pid, bool WaitForProcess)
{
	if (!KillServerInternal(pid))
	{
		return false;
	}

	#ifdef HAVE_WAITPID
	if (WaitForProcess)
	{
		int status, result;

		result = waitpid(pid, &status, 0);
		if (result != pid)
		{
			BOX_LOG_SYS_ERROR("waitpid failed");
		}
		TEST_THAT(result == pid);

		TEST_THAT(WIFEXITED(status));
		if (WIFEXITED(status))
		{
			if (WEXITSTATUS(status) != 0)
			{
				BOX_WARNING("process exited with code " <<
					WEXITSTATUS(status));
			}
			TEST_THAT(WEXITSTATUS(status) == 0);
		}
	}
	#endif

	BOX_INFO("Waiting for server to die (pid " << pid << ")");
	printf("Waiting for server to die (pid %d): ", pid);

	for (int i = 0; i < 300; i++)
	{
		if (i % 10 == 0)
		{
			printf(".");
			fflush(stdout);
		}

		if (!ServerIsAlive(pid)) break;
		ShortSleep(MilliSecondsToBoxTime(100), false);
		if (!ServerIsAlive(pid)) break;
	}

	if (!ServerIsAlive(pid))
	{
		printf(" done.\n");
	}
	else
	{
		printf(" failed!\n");
	}

	fflush(stdout);

	return !ServerIsAlive(pid);
}

bool KillServer(const std::string& pid_file, bool WaitForProcess)
{
	FileStream fs(pid_file);
	IOStreamGetLine getline(fs);
	std::string line = getline.GetLine(false); // !preprocess
	int pid = atoi(line.c_str());
	bool status = KillServer(pid, WaitForProcess);
	TEST_EQUAL_LINE(true, status, std::string("kill(") + pid_file + ")");

#ifdef WIN32
	if(WaitForProcess)
	{
		int unlink_result = EMU_UNLINK(pid_file.c_str());
		TEST_EQUAL_LINE(0, unlink_result, std::string("unlink ") + pid_file);
		if(unlink_result != 0)
		{
			return false;
		}
	}
#endif

	return status;
}

int StartDaemon(int current_pid, const std::string& cmd_line, const char* pid_file)
{
	TEST_THAT_OR(current_pid == 0, return 0);

	int new_pid = LaunchServer(cmd_line, pid_file);
	TEST_THAT_OR(new_pid != -1 && new_pid != 0, return 0);

	::sleep(1);
	TEST_THAT_OR(ServerIsAlive(new_pid), return 0);
	return new_pid;
}

bool StopDaemon(int current_pid, const std::string& pid_file,
	const std::string& memleaks_file, bool wait_for_process)
{
	TEST_THAT_OR(current_pid != 0, return false);
	TEST_THAT_OR(ServerIsAlive(current_pid), return false);
	TEST_THAT_OR(KillServer(current_pid, wait_for_process), return false);
	::sleep(1);

	TEST_THAT_OR(!ServerIsAlive(current_pid), return false);

	#ifdef WIN32
		int unlink_result = EMU_UNLINK(pid_file.c_str());
		TEST_EQUAL_LINE(0, unlink_result, std::string("unlink ") + pid_file);
		if(unlink_result != 0)
		{
			return false;
		}
	#else
		TestRemoteProcessMemLeaks(memleaks_file.c_str());
	#endif

	return true;
}


