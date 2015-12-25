// --------------------------------------------------------------------------
//
// File
//		Name:    Test.cpp
//		Purpose: Useful stuff for tests
//		Created: 2008/04/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include "BoxTime.h"
#include "FileStream.h"
#include "Test.h"
#include "Utils.h"

int num_tests_selected = 0;
int num_failures = 0;
int old_failure_count = 0;
int first_fail_line;
std::string original_working_dir;
std::string first_fail_file;
std::string current_test_name;
std::list<std::string> run_only_named_tests;
std::map<std::string, std::string> s_test_status;

bool setUp(const char* function_name)
{
	current_test_name = function_name;

	if (!run_only_named_tests.empty())
	{
		bool run_this_test = false;

		for (std::list<std::string>::iterator
			i = run_only_named_tests.begin();
			i != run_only_named_tests.end(); i++)
		{
			if (*i == current_test_name)
			{
				run_this_test = true;
				break;
			}
		}
		
		if (!run_this_test)
		{
			// not in the list, so don't run it.
			return false;
		}
	}

	printf("\n\n== %s ==\n", function_name);
	num_tests_selected++;
	old_failure_count = num_failures;

	if (original_working_dir == "")
	{
		char buf[1024];
		if (getcwd(buf, sizeof(buf)) == NULL)
		{
			BOX_LOG_SYS_ERROR("getcwd");
		}
		original_working_dir = buf;
	}
	else
	{
		if (chdir(original_working_dir.c_str()) != 0)
		{
			BOX_LOG_SYS_ERROR("chdir");
		}
	}

#ifdef _MSC_VER
	DIR* pDir = opendir("testfiles");
	if(!pDir)
	{
		THROW_SYS_FILE_ERROR("Failed to open test temporary directory",
			"testfiles", CommonException, Internal);
	}
	struct dirent* pEntry;
	for(pEntry = readdir(pDir); pEntry; pEntry = readdir(pDir))
	{
		std::string filename = pEntry->d_name;
		if(StartsWith("TestDir", filename) ||
			StartsWith("0_", filename) ||
			filename == "accounts.txt" ||
			StartsWith("file", filename) ||
			StartsWith("notifyran", filename) ||
			StartsWith("notifyscript.tag", filename) ||
			StartsWith("restore", filename) ||
			filename == "bbackupd-data" ||
			filename == "syncallowscript.control" ||
			StartsWith("syncallowscript.notifyran.", filename) ||
			filename == "test2.downloaded")
		{
			std::string filepath = std::string("testfiles\\") + filename;
			int filetype = ObjectExists(filepath);
			if(filetype == ObjectExists_File)
			{
				if(::unlink(filepath.c_str()) != 0)
				{
					TEST_FAIL_WITH_MESSAGE(BOX_SYS_ERROR_MESSAGE("Failed to delete "
						"test fixture file: unlink(\"" << filepath << "\")"));
				}
			}
			else if(filetype == ObjectExists_Dir)
			{
				std::string cmd = "rd /s /q " + filepath;
				int status = system(cmd.c_str());
				if(status != 0)
				{
					TEST_FAIL_WITH_MESSAGE("Failed to delete test fixture "
						"file: command '" << cmd << "' exited with "
						"status " << status);
				}
			}
			else
			{
				TEST_FAIL_WITH_MESSAGE("Don't know how to delete file " << filepath <<
					" of type " << filetype);
			}
		}
	}
	closedir(pDir);
	FileStream touch("testfiles/accounts.txt", O_WRONLY | O_CREAT | O_TRUNC,
		S_IRUSR | S_IWUSR);
#else
	TEST_THAT_THROWONFAIL(system(
		"rm -rf testfiles/TestDir* testfiles/0_0 testfiles/0_1 "
		"testfiles/0_2 testfiles/accounts.txt " // testfiles/test* .tgz!
		"testfiles/file* testfiles/notifyran testfiles/notifyran.* "
		"testfiles/notifyscript.tag* "
		"testfiles/restore* testfiles/bbackupd-data "
		"testfiles/syncallowscript.control "
		"testfiles/syncallowscript.notifyran.* "
		"testfiles/test2.downloaded"
		) == 0);
	TEST_THAT_THROWONFAIL(system("touch testfiles/accounts.txt") == 0);
#endif
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_0", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_1", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_2", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/bbackupd-data", 0755) == 0);

	return true;
}

bool tearDown()
{
	if (num_failures == old_failure_count)
	{
		BOX_NOTICE(current_test_name << " passed");
		s_test_status[current_test_name] = "passed";
		return true;
	}
	else
	{
		BOX_NOTICE(current_test_name << " failed"); \
		s_test_status[current_test_name] = "FAILED";
		return false;
	}
}

bool fail()
{
	num_failures++;
	return tearDown();
}

int finish_test_suite()
{
	printf("\n");
	printf("Test results:\n");

	typedef std::map<std::string, std::string>::iterator s_test_status_iterator;
	for(s_test_status_iterator i = s_test_status.begin();
		i != s_test_status.end(); i++)
	{
		BOX_NOTICE("test result: " << i->second << ": " << i->first);
	}

	TEST_LINE(num_tests_selected > 0, "No tests matched the patterns "
		"specified on the command line");

	return (num_failures == 0 && num_tests_selected > 0) ? 0 : 1;
}

bool TestFileExists(const char *Filename)
{
	EMU_STRUCT_STAT st;
	return EMU_STAT(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == 0;
}

bool TestFileNotEmpty(const char *Filename)
{
	EMU_STRUCT_STAT st;
	return EMU_STAT(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == 0 &&
		st.st_size > 0;
}

bool TestDirExists(const char *Filename)
{
	EMU_STRUCT_STAT st;
	return EMU_STAT(Filename, &st) == 0 && (st.st_mode & S_IFDIR) == S_IFDIR;
}

// -1 if doesn't exist
int TestGetFileSize(const std::string& Filename)
{
	EMU_STRUCT_STAT st;
	if(EMU_STAT(Filename.c_str(), &st) == 0)
	{
		return st.st_size;
	}
	return -1;
}

std::string ConvertPaths(const std::string& rOriginal)
{
#ifdef WIN32
	// convert UNIX paths to native

	std::string converted;
	for (size_t i = 0; i < rOriginal.size(); i++)
	{
		if (rOriginal[i] == '/')
		{
			converted += '\\';
		}
		else
		{
			converted += rOriginal[i];
		}
	}
	return converted;

#else // !WIN32
	return rOriginal;
#endif
}

int RunCommand(const std::string& rCommandLine)
{
	return ::system(ConvertPaths(rCommandLine).c_str());
}

#ifdef WIN32
#include <windows.h>
#endif

bool ServerIsAlive(int pid)
{
	#ifdef WIN32

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION,
			false, pid);
		if (hProcess == NULL)
		{
			if (GetLastError() != ERROR_INVALID_PARAMETER)
			{
				BOX_ERROR("Failed to open process " << pid <<
					": " <<
					GetErrorMessage(GetLastError()));
			}
			return false;
		}

		DWORD exitCode;
		BOOL result = GetExitCodeProcess(hProcess, &exitCode);
		CloseHandle(hProcess);

		if (result == 0)
		{
			BOX_ERROR("Failed to get exit code for process " <<
				pid << ": " <<
				GetErrorMessage(GetLastError()))
			return false;
		}

		if (exitCode == STILL_ACTIVE)
		{
			return true;
		}
		
		return false;

	#else // !WIN32

		if(pid == 0) return false;
		return ::kill(pid, 0) != -1;

	#endif // WIN32
}

int ReadPidFile(const char *pidFile)
{
	if(!TestFileNotEmpty(pidFile))
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

int LaunchServer(const std::string& rCommandLine, const char *pidFile)
{
	BOX_INFO("Starting server: " << rCommandLine);

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
		BOX_LOG_WIN_ERROR("Launch failed: " << rCommandLine);
		return -1;
		);

	CloseHandle(procInfo.hProcess);
	CloseHandle(procInfo.hThread);

	return WaitForServerStartup(pidFile, (int)procInfo.dwProcessId);

#else // !WIN32

	TEST_THAT_OR(RunCommand(rCommandLine) == 0,
		TEST_FAIL_WITH_MESSAGE("Failed to start server: " << rCommandLine);
		return -1;
		)

	return WaitForServerStartup(pidFile, 0);

#endif // WIN32
}

int WaitForServerStartup(const char *pidFile, int pidIfKnown)
{
	#ifdef WIN32
	if (pidFile == NULL)
	{
		return pidIfKnown;
	}
	#else
	// on other platforms there is no other way to get 
	// the PID, so a NULL pidFile doesn't make sense.
	ASSERT(pidFile != NULL);
	#endif

	// time for it to start up
	BOX_TRACE("Waiting for server to start");

	for (int i = 0; i < 15; i++)
	{
		if (TestFileNotEmpty(pidFile))
		{
			break;
		}

		if (pidIfKnown && !ServerIsAlive(pidIfKnown))
		{
			break;
		}

		::sleep(1);
	}

	// on Win32 we can check whether the process is alive
	// without even checking the PID file

	if (pidIfKnown && !ServerIsAlive(pidIfKnown))
	{
		TEST_FAIL_WITH_MESSAGE("Server died!");
		return -1;
	}

	if (!TestFileNotEmpty(pidFile))
	{
		TEST_FAIL_WITH_MESSAGE("Server didn't save PID file");
		return -1;
	}

	BOX_TRACE("Server started");

	// wait a second for the pid to be written to the file
	::sleep(1);

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

void TestRemoteProcessMemLeaksFunc(const char *filename,
	const char* file, int line)
{
#ifdef BOX_MEMORY_LEAK_TESTING
	// Does the file exist?
	if(!TestFileExists(filename))
	{
		if (num_failures == 0)
		{
			first_fail_file = file;
			first_fail_line = line;
		}
		++num_failures;
		printf("FAILURE: MemLeak report not available (file %s) "
			"at %s:%d\n", filename, file, line);
	}
	else
	{
		// Is it empty?
		if(TestGetFileSize(filename) > 0)
		{
			if (num_failures == 0)
			{
				first_fail_file = file;
				first_fail_line = line;
			}
			++num_failures;
			printf("FAILURE: Memory leaks found in other process "
				"(file %s) at %s:%d\n==========\n", 
				filename, file, line);
			FILE *f = fopen(filename, "r");
			char linebuf[512];
			while(::fgets(linebuf, sizeof(linebuf), f) != 0)
			{
				printf("%s", linebuf);
			}
			fclose(f);
			printf("==========\n");
		}
		
		// Delete it
		::unlink(filename);
	}
#endif
}

void force_sync()
{
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"force-sync") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
}

void wait_for_sync_start()
{
	BOX_TRACE("Waiting for sync to start...");
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"wait-for-sync") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
	BOX_TRACE("Backup daemon reported that sync has started.");
}

void wait_for_sync_end()
{
	BOX_TRACE("Waiting for sync to finish...");
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"wait-for-end") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
	BOX_TRACE("Backup daemon reported that sync has finished.");
}

void sync_and_wait()
{
	BOX_TRACE("Starting a sync and waiting for it to finish...");
	TEST_THAT(::system(BBACKUPCTL " -q -c testfiles/bbackupd.conf "
		"sync-and-wait") == 0);
	TestRemoteProcessMemLeaks("bbackupctl.memleaks");
	BOX_TRACE("Backup daemon reported that sync has finished.");
}

void terminate_bbackupd(int pid)
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
void wait_for_operation(int seconds, const char* message)
{
	BOX_INFO("Waiting " << seconds << " seconds for " << message);

	for(int l = 0; l < seconds; ++l)
	{
		sleep(1);
	}

	BOX_TRACE("Finished waiting for " << message);
}

void safe_sleep(int seconds)
{
	ShortSleep(SecondsToBoxTime(seconds), true);
}

std::auto_ptr<Configuration> load_config_file(const std::string& config_file,
	const ConfigurationVerify& verify)
{
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify(config_file, &verify, errs));
	TEST_EQUAL_LINE(0, errs.size(), "Failed to load configuration file: " + config_file +
		": " + errs);
	TEST_EQUAL_OR(0, errs.size(), config.reset());
	return config;
}

