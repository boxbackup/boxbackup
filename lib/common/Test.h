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

#include <cstring>

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
	BOX_ERROR("**** TEST FAILURE: " << msg << " at " << __FILE__ << \
		":" << __LINE__); \
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

// utility macro for comparing two strings in a line
#define TEST_EQUAL(_expected, _found) \
{ \
	std::ostringstream _oss1; \
	_oss1 << _expected; \
	std::string _exp_str = _oss1.str(); \
	\
	std::ostringstream _oss2; \
	_oss2 << _found; \
	std::string _found_str = _oss2.str(); \
	\
	if(_exp_str != _found_str) \
	{ \
		BOX_WARNING("Expected <" << _exp_str << "> but found <" << \
			_found_str << ">"); \
		\
		std::ostringstream _oss3; \
		_oss3 << #_found << " != " << #_expected; \
		\
		TEST_FAIL_WITH_MESSAGE(_oss3.str().c_str()); \
	} \
}

// utility macro for comparing two strings in a line
#define TEST_EQUAL_LINE(_expected, _found, _line) \
{ \
	std::ostringstream _oss1; \
	_oss1 << _expected; \
	std::string _exp_str = _oss1.str(); \
	\
	std::ostringstream _oss2; \
	_oss2 << _found; \
	std::string _found_str = _oss2.str(); \
	\
	if(_exp_str != _found_str) \
	{ \
		std::string _line_str = _line; \
		printf("Expected <%s> but found <%s> in <%s>\n", \
			_exp_str.c_str(), _found_str.c_str(), _line_str.c_str()); \
		\
		std::ostringstream _oss3; \
		_oss3 << #_found << " != " << #_expected << " in " << _line; \
		\
		TEST_FAIL_WITH_MESSAGE(_oss3.str().c_str()); \
	} \
}


// utility macro for testing a line
#define TEST_LINE(_condition, _line) \
	TEST_THAT(_condition); \
	if (!(_condition)) \
	{ \
		printf("Test failed on <%s>\n", _line.c_str()); \
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
void wait_for_operation(int seconds, char* message);
void safe_sleep(int seconds);

#endif // TEST__H
