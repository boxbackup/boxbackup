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

#include <string>

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
#define BBSTORED        "../../bin/bbstored/bbstored"
#define BBACKUPQUERY    "../../bin/bbackupquery/bbackupquery"
#define BBSTOREACCOUNTS "../../bin/bbstoreaccounts/bbstoreaccounts"
#define TEST_RETURN(actual, expected) TEST_THAT(actual == expected*256);
#endif

extern int failures;
extern int first_fail_line;
extern std::string first_fail_file;
extern std::string bbackupd_args, bbstored_args, bbackupquery_args, test_args;

#define TEST_FAIL_WITH_MESSAGE(msg) \
{ \
	if (failures == 0) \
	{ \
		first_fail_file = __FILE__; \
		first_fail_line = __LINE__; \
	} \
	failures++; \
	printf("FAILURE: %s at " __FILE__ ":%d\n", msg, __LINE__); \
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

bool TestFileExists(const char *Filename);
bool TestDirExists(const char *Filename);

// -1 if doesn't exist
int TestGetFileSize(const char *Filename);
std::string ConvertPaths(const std::string& rOriginal);
int RunCommand(const std::string& rCommandLine);
bool ServerIsAlive(int pid);
int ReadPidFile(const char *pidFile);
int LaunchServer(const std::string& rCommandLine, const char *pidFile);
int WaitForServerStartup(const char *pidFile, int pidIfKnown);

#define TestRemoteProcessMemLeaks(filename) \
	TestRemoteProcessMemLeaksFunc(filename, __FILE__, __LINE__)

void TestRemoteProcessMemLeaksFunc(const char *filename,
	const char* file, int line);

void force_sync();
void wait_for_sync_start();
void wait_for_sync_end();
void sync_and_wait();
void terminate_bbackupd(int pid);

// Wait a given number of seconds for something to complete
void wait_for_operation(int seconds);
void safe_sleep(int seconds);

#endif // TEST__H
