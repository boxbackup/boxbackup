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

#ifdef HAVE_DIRENT_H
#	include <dirent.h> // for opendir(), struct DIR
#endif

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif

#include <string>

#include "BoxTime.h"
#include "Exception.h"
#include "FileStream.h"
#include "Test.h"
#include "Utils.h" // for ObjectExists_* (object_exists_t)

int num_tests_selected = 0;
int num_failures = 0;
int old_failure_count = 0;
int first_fail_line;
std::string original_working_dir;
std::string first_fail_file;
std::string current_test_name;
std::list<std::string> run_only_named_tests;
std::map<std::string, std::string> s_test_status;
box_time_t current_test_start;

bool setUp(const std::string& function_name, const std::string& specialisation)
{
	std::ostringstream specialised_name;
	specialised_name << function_name;

	if(!specialisation.empty())
	{
		specialised_name << "(" << specialisation << ")";
	}

	current_test_name = specialised_name.str();

	if (!run_only_named_tests.empty())
	{
		bool run_this_test = false;

		for (std::list<std::string>::iterator
			i = run_only_named_tests.begin();
			i != run_only_named_tests.end(); i++)
		{
			if (*i == function_name || *i == current_test_name)
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

	printf("\n\n== %s ==\n", current_test_name.c_str());
	num_tests_selected++;
	old_failure_count = num_failures;
	current_test_start = GetCurrentBoxTime();

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

	cleanup_test_environment();

	return true;
}

void cleanup_test_environment(bool delete_pid_files)
{
	// We need to do something more complex than "rm -rf testfiles" to clean up the mess and
	// prepare for the next test, in a way that works on Windows (without rm -rf) as well.
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
			filename == "bbackupd-cache" ||
			filename == "bbackupd-cache-2" ||
			filename == "bbackupd-data" ||
			filename == "store" ||
			filename == "syncallowscript.control" ||
			filename == "test_map.db" ||
			StartsWith("syncallowscript.notifyran.", filename) ||
			filename == "test2.downloaded" ||
			EndsWith("testfile", filename) ||
			EndsWith(".qdbm", filename) ||
			(delete_pid_files && EndsWith(".pid", filename)))
		{
			std::string filepath = std::string("testfiles" DIRECTORY_SEPARATOR) +
				filename;
			object_exists_t filetype = ObjectExists(filepath);

			if(filetype == ObjectExists_File)
			{
				if(EMU_UNLINK(filepath.c_str()) != 0)
				{
					if(EndsWith(".pid", filename))
					{
						// Failure to delete the PID file may mean that the daemon is still
						// running, and in some tests that is expected.
						BOX_WARNING(BOX_SYS_FILE_ERRNO_MESSAGE("Failed to delete existing "
							"PID file", errno, filepath));
						continue;
					}
					else
					{
						TEST_FAIL_WITH_MESSAGE(BOX_SYS_ERROR_MESSAGE("Failed to delete "
							"test fixture file: unlink(\"" << filepath << "\")"));
					}
				}
			}
			else if(filetype == ObjectExists_Dir)
			{
#ifdef _MSC_VER
				// More complex command invocation required to properly encode
				// arguments when non-ASCII characters are involved:
				std::string cmd = "cmd /c rd /s /q " + filepath;
				WCHAR* wide_cmd = ConvertUtf8ToWideString(cmd.c_str());
				if(wide_cmd == NULL)
				{
					TEST_FAIL_WITH_MESSAGE("Failed to convert string "
						"to wide string: " << cmd);
					continue;
				}

				STARTUPINFOW si;
				PROCESS_INFORMATION pi;

				ZeroMemory( &si, sizeof(si) );
				si.cb = sizeof(si);
				ZeroMemory( &pi, sizeof(pi) );

				BOOL result = CreateProcessW(
					NULL, // lpApplicationName
					wide_cmd, // lpCommandLine
					NULL, // lpProcessAttributes
					NULL, // lpThreadAttributes
					TRUE, // bInheritHandles
					0, // dwCreationFlags
					NULL, // lpEnvironment
					NULL, // lpCurrentDirectory
					&si, // lpStartupInfo
					&pi // lpProcessInformation
				);
				delete [] wide_cmd;

				if(result == FALSE)
				{
					TEST_FAIL_WITH_MESSAGE("Failed to delete test "
						"fixture file: failed to execute command "
						"'" << cmd << "': " <<
						GetErrorMessage(GetLastError()));
					continue;
				}

				// Wait until child process exits.
				WaitForSingleObject(pi.hProcess, INFINITE);
				DWORD exit_code;
				result = GetExitCodeProcess(pi.hProcess, &exit_code);

				if(result == FALSE)
				{
					TEST_FAIL_WITH_MESSAGE("Failed to delete "
						"test fixture file: failed to get "
						"command exit status: '" <<
						cmd << "': " <<
						GetErrorMessage(GetLastError()));
				}
				else if(exit_code != 0)
				{
					TEST_FAIL_WITH_MESSAGE("Failed to delete test "
						"fixture file: command '" << cmd << "' "
						"exited with status " << exit_code);
				}

				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
#else // !_MSC_VER
				// Deleting directories is so much easier on Unix!
				std::string cmd = "rm -rf '" + filepath + "'";
				TEST_EQUAL_LINE(0, system(cmd.c_str()), "system() failed: " << cmd);
#endif
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

	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_0", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_1", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_2", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/bbackupd-data", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/bbackupd-cache", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/bbackupd-cache-2", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/store", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/store/subdir", 0755) == 0);
}

bool tearDown()
{
#ifndef WIN32
	// Reset SIGCHLD handler, in case the test has changed it:
	::signal(SIGCHLD, SIG_DFL);
#endif

	box_time_t elapsed_time = GetCurrentBoxTime() - current_test_start;
	std::ostringstream buf;
	buf.setf(std::ios_base::fixed);
	buf.precision(1);
	buf << " (" << ((float)BoxTimeToMilliSeconds(elapsed_time) / 1000) << " sec)";

	if (num_failures == old_failure_count)
	{
		BOX_NOTICE(current_test_name << " passed" << buf.str());
		s_test_status[current_test_name] = "passed";
		return true;
	}
	else
	{
		BOX_NOTICE(current_test_name << " failed" << buf.str()); \
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
	return process_is_running(pid);
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

#ifdef WIN32
// Used by infrastructure/buildenv-testmain-template.cpp and lib/server/ServerControl.cpp.
// Needs to be defind in a translation unit that's accessible to all tests, so might as well
// do that here.
HANDLE sTestChildDaemonJobObject = INVALID_HANDLE_VALUE;
#endif

bool TestRemoteProcessMemLeaksFunc(const char *filename, const char* file, int line)
{
	bool result = true;

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
		result = false;
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
			result = false;
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
		EMU_UNLINK(filename);
	}
#endif

	return result;
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

bool test_equal_lists(const std::vector<std::string>& expected_items,
	const std::vector<std::string>& actual_items)
{
	bool all_match = (expected_items.size() == actual_items.size());

	for(size_t i = 0; i < std::max(expected_items.size(), actual_items.size()); i++)
	{
		const std::string& expected = (i < expected_items.size()) ? expected_items[i] : "None";
		const std::string& actual   = (i < actual_items.size())   ? actual_items[i]   : "None";
		TEST_EQUAL_LINE(expected, actual, "Item " << i);
		all_match &= (expected == actual);
	}

	return all_match;
}

bool test_equal_maps(const str_map_t& expected_attrs, const str_map_t& actual_attrs)
{
	str_map_diff_t differences = compare_str_maps(expected_attrs, actual_attrs);
	for(str_map_diff_t::iterator i = differences.begin(); i != differences.end(); i++)
	{
		const std::string& name = i->first;
		const std::string& expected = i->second.first;
		const std::string& actual = i->second.second;
		TEST_EQUAL_LINE(expected, actual, "Wrong value for attribute " << name);
	}

	return differences.empty();
}
