#include "Box.h"

#include <errno.h>
#include <stdio.h>

#ifdef HAVE_SYS_TYPES_H
	#include <sys/types.h>
#endif

#ifdef HAVE_SYS_WAIT_H
	#include <sys/wait.h>
#endif

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#include "BoxTime.h"
#include "IOStreamGetLine.h"
#include "ServerControl.h"
#include "SocketStream.h"
#include "Test.h"
#include "autogen_ServerException.h"

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
	if(!getLine.GetLine(configSummary))
	{
		BOX_ERROR("Failed to receive configuration summary from daemon");
		return false;
	}

	// Was the connection rejected by the server?
	if(getLine.IsEOF())
	{
		BOX_ERROR("Server rejected the connection");
		return false;
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
	std::string line = getline.GetLine();
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

int LaunchServer(const std::string& rCommandLine, const char *pidFile, int port,
	const std::string& socket_path)
{
	BOX_INFO("Starting server: " << rCommandLine);

#ifdef WIN32

	// Use a Windows "Job Object" as a container for all our child
	// processes. The test runner will create this job object when
	// it starts, and close the handle (killing any running daemons)
	// when it exits. This is the best way to avoid daemons hanging
	// around and causing subsequent tests to fail, and/or the test
	// runner to hang waiting for a daemon that will never terminate.

	PROCESS_INFORMATION procInfo;

	STARTUPINFO startInfo;
	startInfo.cb = sizeof(startInfo);
	startInfo.lpReserved = NULL;
	startInfo.lpDesktop  = NULL;
	startInfo.lpTitle    = NULL;
	startInfo.dwFlags = 0;
	startInfo.cbReserved2 = 0;
	startInfo.lpReserved2 = NULL;

	std::string cmd = ConvertPaths(rCommandLine);
	CHAR* tempCmd = strdup(cmd.c_str());

	DWORD result = CreateProcess
	(
		NULL,        // lpApplicationName, naughty!
		tempCmd,     // lpCommandLine
		NULL,        // lpProcessAttributes
		NULL,        // lpThreadAttributes
		false,       // bInheritHandles
		0,           // dwCreationFlags
		NULL,        // lpEnvironment
		NULL,        // lpCurrentDirectory
		&startInfo,  // lpStartupInfo
		&procInfo    // lpProcessInformation
	);

	free(tempCmd);

	TEST_THAT_OR(result != 0,
		BOX_LOG_WIN_ERROR("Failed to CreateProcess: " << rCommandLine);
		return -1;
		);

	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);

	return WaitForServerStartup(pidFile, (int)procInfo.dwProcessId, port, socket_path);

#else // !WIN32

	TEST_THAT_OR(RunCommand(rCommandLine) == 0,
		TEST_FAIL_WITH_MESSAGE("Failed to start server: " << rCommandLine);
		return -1;
		)

	return WaitForServerStartup(pidFile, 0, port, socket_path);

#endif // WIN32
}

int WaitForServerStartup(const char *pidFile, int pidIfKnown, int port,
	const std::string& socket_path)
{
#ifdef WIN32
	if(pidFile == NULL && port == 0 && socket_path == "")
	{
		return pidIfKnown;
	}
#else
	// On other platforms there is no other way to get the PID, so a NULL pidFile doesn't
	// make sense.
	ASSERT(pidFile != NULL);
#endif

	// time for it to start up
	BOX_TRACE("Waiting for server to start");

	for (int i = 150; i >= 0; i--)
	{
		if(i == 0)
		{
			// ran out of time waiting
			TEST_FAIL_WITH_MESSAGE("Server didn't start within expected time");
			return -1;
		}

		ShortSleep(MilliSecondsToBoxTime(100), false);

		if(!TestFileNotEmpty(pidFile))
		{
			// Hasn't written a complete PID file yet, go round again
			continue;
		}

		// Once we know what PID the process has/had, we can check if it has died during or
		// shortly after startup:
		if (pidIfKnown && !ServerIsAlive(pidIfKnown))
		{
			TEST_FAIL_WITH_MESSAGE("Server died!");
			return -1;
		}

		if(port != 0 || socket_path != "")
		{
			try
			{
				if(port != 0)
				{
					SocketStream conn;
					conn.Open(Socket::TypeINET, "localhost", port);
				}

				if(socket_path != "")
				{
					SocketStream conn;
					conn.Open(Socket::TypeUNIX, socket_path);
				}
			}
			catch(ServerException &e)
			{
				if(EXCEPTION_IS_TYPE(e, ServerException, SocketOpenError))
				{
					// not listening on port, go round again
					continue;
				}
				else
				{
					// something bad happened, break
					throw;
				}
			}
		}

		// All tests that we can do have passed, looks good!
		break;
	}

	BOX_TRACE("Server started");

	// read pid file
	int pid = ReadPidFile(pidFile);

	// On Win32 we can check whether the PID in the pidFile matches
	// the one returned by the system, which it always should.
	if (pidIfKnown && pid != pidIfKnown)
	{
		BOX_ERROR("Server wrote wrong pid to file (" << pidFile <<
			"): expected " << pidIfKnown << " but found " <<
			pid);
		TEST_FAIL_WITH_MESSAGE("Server wrote wrong pid to file");
		return -1;
	}

	return pid;
}

int StartDaemon(int current_pid, const std::string& cmd_line, const char* pid_file, int port,
	const std::string& socket_path)
{
	TEST_THAT_OR(current_pid == 0, return 0);

	int new_pid = LaunchServer(cmd_line, pid_file, port, socket_path);
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


