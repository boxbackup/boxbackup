// --------------------------------------------------------------------------
//
// File
//		Name:    Test.h
//		Purpose: Useful stuff for tests
//		Created: 2003/07/11
//
// --------------------------------------------------------------------------

#ifndef TEST__H
#define TEST__H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <stdio.h>
#include <string>

extern int failures;
extern int first_fail_line;
extern std::string first_fail_file;

#define TEST_FAIL_WITH_MESSAGE(msg) \
{ \
	if (failures == 0) \
	{ \
		first_fail_file = __FILE__; \
		first_fail_line = __LINE__; \
	} \
	failures++; \
	printf("FAILURE: " msg " at " __FILE__ "(%d)\n", __LINE__); \
}

#define TEST_ABORT_WITH_MESSAGE(msg) {TEST_FAIL_WITH_MESSAGE(msg); return 1;}

#define TEST_THAT(condition) {if(!(condition)) TEST_FAIL_WITH_MESSAGE("Condition [" #condition "] failed")}
#define TEST_THAT_ABORTONFAIL(condition) {if(!(condition)) TEST_ABORT_WITH_MESSAGE("Condition [" #condition "] failed")}

// NOTE: The 0- bit is to allow this to work with stuff which has negative constants for flags (eg ConnectionException)
#define TEST_CHECK_THROWS(statement, excepttype, subtype)									\
	{																						\
		bool didthrow = false;																\
		try																					\
		{																					\
			statement;																		\
		}																					\
		catch(excepttype &e)																\
		{																					\
			if(e.GetSubType() != ((unsigned int)excepttype::subtype)						\
					&& e.GetSubType() != (unsigned int)(0-excepttype::subtype)) 			\
			{																				\
				throw;																		\
			}																				\
			didthrow = true;																\
		}																					\
		catch(...)																			\
		{																					\
			throw;																			\
		}																					\
		if(!didthrow)																		\
		{																					\
			TEST_FAIL_WITH_MESSAGE("Didn't throw exception " #excepttype "(" #subtype ")")	\
		}																					\
	}

inline bool TestFileExists(const char *Filename)
{
	struct stat st;
	return ::stat(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == 0;
}

inline bool TestDirExists(const char *Filename)
{
	struct stat st;
	return ::stat(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == S_IFDIR;
}

// -1 if doesn't exist
inline int TestGetFileSize(const char *Filename)
{
	struct stat st;
	if(::stat(Filename, &st) == 0)
	{
		return st.st_size;
	}
	return -1;
}

inline std::string ConvertPaths(const char *pCommandLine)
{
#ifdef WIN32
	// convert UNIX paths to native

	std::string command;
	for (int i = 0; pCommandLine[i] != 0; i++)
	{
		if (pCommandLine[i] == '/')
		{
			command += '\\';
		}
		else
		{
			command += pCommandLine[i];
		}
	}

#else // !WIN32
	std::string command = pCommandLine;
#endif

	return command;
}

inline int RunCommand(const char *pCommandLine)
{
	return ::system(ConvertPaths(pCommandLine).c_str());
}

#ifdef WIN32
#include <windows.h>
#endif

inline bool ServerIsAlive(int pid)
{
#ifdef WIN32
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, false, pid);
	if (hProcess == NULL)
	{
		if (GetLastError() != ERROR_INVALID_PARAMETER)
		{
			printf("Failed to open process %d: error %d\n",
				pid, (int)GetLastError());
		}
		return false;
	}
	CloseHandle(hProcess);
	return true;
#else // !WIN32
	if(pid == 0) return false;
	return ::kill(pid, 0) != -1;
#endif // WIN32
}

inline int ReadPidFile(const char *pidFile)
{
	if(!TestFileExists(pidFile))
	{
		TEST_FAIL_WITH_MESSAGE("Server didn't save PID file "
			"(perhaps one was already running?)");	
		return -1;
	}
	
	int pid = -1;

	FILE *f = fopen(pidFile, "r");
	if(f == NULL || fscanf(f, "%d", &pid) != 1)
	{
		TEST_FAIL_WITH_MESSAGE("Couldn't read PID file");	
		return -1;
	}
	fclose(f);
	
	return pid;
}

inline int LaunchServer(const char *pCommandLine, const char *pidFile)
{
#ifdef WIN32

	PROCESS_INFORMATION procInfo;

	STARTUPINFO startInfo;
	startInfo.cb = sizeof(startInfo);
	startInfo.lpReserved = NULL;
	startInfo.lpDesktop  = NULL;
	startInfo.lpTitle    = NULL;
	startInfo.dwFlags = 0;
	startInfo.cbReserved2 = 0;
	startInfo.lpReserved2 = NULL;

	std::string cmd = ConvertPaths(pCommandLine);
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

	if (result == 0)
	{
		DWORD err = GetLastError();
		printf("Launch failed: %s: error %d\n", pCommandLine, (int)err);
		return -1;
	}

	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);

#else // !WIN32

	if(RunCommand(pCommandLine) != 0)
	{
		printf("Server: %s\n", pCommandLine);
		TEST_FAIL_WITH_MESSAGE("Couldn't start server");
		return -1;
	}

#endif // WIN32

	#ifdef WIN32
	// on other platforms there is no other way to get 
	// the PID, so a NULL pidFile doesn't make sense.

	if (pidFile == NULL)
	{
		return (int)procInfo.dwProcessId;
	}
	#endif

	// time for it to start up
	::fprintf(stdout, "Starting server: %s\n", pCommandLine);
	::fprintf(stdout, "Waiting for server to start: ");

	for (int i = 0; i < 15; i++)
	{
		if (TestFileExists(pidFile))	
		{
			break;
		}

		#ifdef WIN32
		if (!ServerIsAlive((int)procInfo.dwProcessId))
		{
			break;
		}
		#endif

		::fprintf(stdout, ".");
		::fflush(stdout);
		::sleep(1);
	}

	#ifdef WIN32
	// on Win32 we can check whether the process is alive
	// without even checking the PID file

	if (!ServerIsAlive((int)procInfo.dwProcessId))
	{
		::fprintf(stdout, "server died!\n");
		TEST_FAIL_WITH_MESSAGE("Server died!");	
		return -1;
	}
	#endif

	if (!TestFileExists(pidFile))
	{
		::fprintf(stdout, "timed out!\n");
		TEST_FAIL_WITH_MESSAGE("Server didn't save PID file");	
		return -1;
	}

	::fprintf(stdout, "done.\n");

	// wait a second for the pid to be written to the file
	::sleep(1);

	// read pid file
	int pid = ReadPidFile(pidFile);

	#ifdef WIN32
	// On Win32 we can check whether the PID in the pidFile matches
	// the one returned by the system, which it always should.

	if (pid != (int)procInfo.dwProcessId)
	{
		printf("Server wrote wrong pid to file (%s): expected %d "
			"but found %d\n", pidFile, 
			(int)procInfo.dwProcessId, pid);
		TEST_FAIL_WITH_MESSAGE("Server wrote wrong pid to file");	
		return -1;
	}
	#endif

	return pid;
}

#define TestRemoteProcessMemLeaks(filename) \
	TestRemoteProcessMemLeaksFunc(filename, __FILE__, __LINE__)

inline void TestRemoteProcessMemLeaksFunc(const char *filename,
	const char* file, int line)
{
#ifdef BOX_MEMORY_LEAK_TESTING
	// Does the file exist?
	if(!TestFileExists(filename))
	{
		if (failures == 0)
		{
			first_fail_file = file;
			first_fail_line = line;
		}
		++failures;
		printf("FAILURE: MemLeak report not available (file %s)\n", filename);
	}
	else
	{
		// Is it empty?
		if(TestGetFileSize(filename) > 0)
		{
			if (failures == 0)
			{
				first_fail_file = file;
				first_fail_line = line;
			}
			++failures;
			printf("FAILURE: Memory leaks found in other process "
				"(file %s)\n==========\n", filename);
			FILE *f = fopen(filename, "r");
			char line[512];
			while(::fgets(line, sizeof(line), f) != 0)
			{
				printf("%s", line);
			}
			fclose(f);
			printf("==========\n");
		}
		
		// Delete it
		::unlink(filename);
	}
#endif
}

#ifdef WIN32
#define BBACKUPCTL      "..\\..\\bin\\bbackupctl\\bbackupctl.exe"
#define BBACKUPD        "..\\..\\bin\\bbackupd\\bbackupd.exe"
#define BBSTORED        "..\\..\\bin\\bbstored\\bbstored.exe"
#define BBACKUPQUERY    "..\\..\\bin\\bbackupquery\\bbackupquery.exe"
#define BBSTOREACCOUNTS "..\\..\\bin\\bbstoreaccounts\\bbstoreaccounts.exe"
#define TEST_RETURN(actual, expected) TEST_THAT(actual == expected);
#else
#define BBACKUPCTL      "../../bin/bbackupctl/bbackupctl"
#define BBACKUPD        "../../bin/bbackupd/bbackupd"
#define BBSTORED        "../../bin/bbackupd/bbstored"
#define BBACKUPQUERY    "../../bin/bbackupquery/bbackupquery"
#define BBSTOREACCOUNTS "../../bin/bbstoreaccounts/bbstoreaccounts"
#define TEST_RETURN(actual, expected) TEST_THAT(actual == expected*256);
#endif

inline void terminate_bbackupd(int pid)
{
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"terminate") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");

	for (int i = 0; i < 20; i++)
	{
		if (!ServerIsAlive(pid)) break;
		fprintf(stdout, ".");
		fflush(stdout);
		sleep(1);
	}

	TEST_THAT(!ServerIsAlive(pid));
	TestRemoteProcessMemLeaks("bbackupd.memleaks");
}


// Wait a given number of seconds for something to complete
inline void wait_for_operation(int seconds)
{
	printf("waiting: ");
	fflush(stdout);
	for(int l = 0; l < seconds; ++l)
	{
		sleep(1);
		printf(".");
		fflush(stdout);
	}
	printf("\n");
	fflush(stdout);
}

#endif // TEST__H
