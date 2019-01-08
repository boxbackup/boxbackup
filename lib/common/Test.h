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

// for printf:
#include <stdio.h>

#include <cstring>
#include <list>
#include <map>
#include <string>

#include "Configuration.h"

#ifndef TEST_EXECUTABLE
#	ifdef _MSC_VER
		// Our CMakeFiles compile tests to different executable filenames,
		// e.g. test_common.exe instead of _test.exe.
		#define TEST_EXECUTABLE BOX_MODULE ".exe"
#	else
		#define TEST_EXECUTABLE "./_test"
#	endif
#endif // TEST_EXECUTABLE

#ifdef WIN32
#define BBACKUPCTL      "..\\..\\bin\\bbackupctl\\bbackupctl.exe"
#define BBACKUPD        "..\\..\\bin\\bbackupd\\bbackupd.exe"
#define BBSTORED        "..\\..\\bin\\bbstored\\bbstored.exe"
#define BBACKUPQUERY    "..\\..\\bin\\bbackupquery\\bbackupquery.exe"
#define BBSTOREACCOUNTS "..\\..\\bin\\bbstoreaccounts\\bbstoreaccounts.exe"
#define TEST_RETURN_OR(actual, expected, or_command) TEST_EQUAL_OR(expected, actual, or_command);
#else
#define BBACKUPCTL      "../../bin/bbackupctl/bbackupctl"
#define BBACKUPD        "../../bin/bbackupd/bbackupd"
#define BBSTORED        "../../bin/bbstored/bbstored"
#define BBACKUPQUERY    "../../bin/bbackupquery/bbackupquery"
#define BBSTOREACCOUNTS "../../bin/bbstoreaccounts/bbstoreaccounts"
#define TEST_RETURN_OR(actual, expected, or_command) TEST_EQUAL_OR((expected << 8), actual, or_command);
#endif

#define TEST_RETURN(actual, expected) TEST_RETURN_OR(actual, expected,)

#define DEFAULT_BBSTORED_CONFIG_FILE "testfiles/bbstored.conf"
#define DEFAULT_BBACKUPD_CONFIG_FILE "testfiles/bbackupd.conf"
#define DEFAULT_S3_CACHE_DIR "testfiles/bbackupd-cache"

extern int num_failures;
extern int first_fail_line;
extern int num_tests_selected;
extern int old_failure_count;
extern std::string first_fail_file;
extern std::string bbackupd_args, bbstored_args, bbackupquery_args, test_args;
extern bool bbackupd_args_overridden, bbstored_args_overridden;
extern std::list<std::string> run_only_named_tests;
extern std::string current_test_name;
extern std::map<std::string, std::string> s_test_status;

#ifdef WIN32
extern HANDLE sTestChildDaemonJobObject;
#endif

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP() \
	if (!setUp(__FUNCTION__, "")) return true; \
	try \
	{ // left open for TEARDOWN()

#define SETUP_SPECIALISED(specialisation) \
	if (!setUp(__FUNCTION__, specialisation)) return true; \
	try \
	{ // left open for TEARDOWN()

#define TEARDOWN() \
		return tearDown(); \
	} \
	catch (BoxException &e) \
	{ \
		BOX_NOTICE(current_test_name << " errored: " << e.what()); \
		num_failures++; \
		tearDown(); \
		s_test_status[current_test_name] = "ERRORED"; \
		return false; \
	}

//! End the current test. Only use within a test function, because it just returns false!
#define FAIL { \
	std::ostringstream os; \
	os << "failed at " << current_test_name << ":" << __LINE__; \
	s_test_status[current_test_name] = os.str(); \
	return fail(); \
}

#define TEST_FAIL_WITH_MESSAGE(msg) \
{ \
	if (num_failures == 0) \
	{ \
		first_fail_file = __FILE__; \
		first_fail_line = __LINE__; \
	} \
	num_failures++; \
	BOX_ERROR("**** TEST FAILURE: " << msg << " at " << __FILE__ << \
		":" << __LINE__); \
}

#define TEST_ABORT_WITH_MESSAGE(msg) {TEST_FAIL_WITH_MESSAGE(msg); return 1;}

#define TEST_THAT_OR(condition, or_command) \
	if(!(condition)) \
	{ \
		TEST_FAIL_WITH_MESSAGE("Condition [" #condition "] failed"); \
		or_command; \
	}
#define TEST_THAT(condition) TEST_THAT_OR(condition,)
#define TEST_THAT_ABORTONFAIL(condition) {if(!(condition)) TEST_ABORT_WITH_MESSAGE("Condition [" #condition "] failed")}
#define TEST_THAT_THROWONFAIL(condition) \
	TEST_THAT_OR(condition, THROW_EXCEPTION_MESSAGE(CommonException, \
		AssertFailed, "Condition [" #condition "] failed"));

// NOTE: The 0- bit is to allow this to work with stuff which has negative constants for flags (eg ConnectionException)
#define TEST_CHECK_THROWS_AND_OR(statement, excepttype, subtype, and_command, or_command) \
	{ \
		bool didthrow = false; \
		HideExceptionMessageGuard hide; \
		BOX_TRACE("Exception logging disabled at " __FILE__ ":" \
			<< __LINE__); \
		try \
		{ \
			statement; \
		} \
		catch(excepttype &e) \
		{ \
			if(e.GetSubType() != ((unsigned int)excepttype::subtype) \
					&& e.GetSubType() != (unsigned int)(0-excepttype::subtype)) \
			{ \
				throw; \
			} \
			didthrow = true; \
			and_command; \
		} \
		catch(...) \
		{ \
			throw; \
		} \
		if(!didthrow) \
		{ \
			TEST_FAIL_WITH_MESSAGE("Didn't throw exception " #excepttype "(" #subtype ")"); \
			or_command; \
		} \
	}
#define TEST_CHECK_THROWS(statement, excepttype, subtype) \
	TEST_CHECK_THROWS_AND_OR(statement, excepttype, subtype,,)

// utility macro for comparing two strings in a line
#define TEST_EQUAL_OR(_expected, _found, or_command) \
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
		BOX_ERROR("Expected <" << _exp_str << "> but found <" << \
			_found_str << ">"); \
		\
		std::ostringstream _oss3; \
		_oss3 << #_found << " != " << #_expected; \
		\
		TEST_FAIL_WITH_MESSAGE(_oss3.str().c_str()); \
		or_command; \
	} \
}
#define TEST_EQUAL(_expected, _found) \
	TEST_EQUAL_OR(_expected, _found,)

// utility macro for comparing two strings in a line
#define TEST_EQUAL_LINE_OR(_expected, _found, _line, or_command) \
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
		std::ostringstream _oss3; \
		_oss3 << #_found << " != " << #_expected << ": " \
			"expected <" << _exp_str << "> " \
			"but found <" << _found_str << "> " \
			"in <" << _line << ">"; \
		TEST_FAIL_WITH_MESSAGE(_oss3.str()); \
		or_command; \
	} \
}

#define TEST_EQUAL_LINE(_expected, _found, _line) \
	TEST_EQUAL_LINE_OR(_expected, _found, _line,)

// utility macros for testing a string/output line
#define TEST_LINE(_condition, _line) \
	TEST_THAT(_condition); \
	if (!(_condition)) \
	{ \
		std::ostringstream _ossl; \
		_ossl << _line; \
		std::string _line_str = _ossl.str(); \
		printf("Test failed on <%s>\n", _line_str.c_str()); \
	}

#define TEST_LINE_OR(_condition, _line, _or_command) \
	TEST_LINE(_condition, _line); \
	if(!(_condition)) \
	{ \
		_or_command; \
	}

#define TEST_STARTSWITH(expected, actual) \
	TEST_EQUAL_LINE(expected, actual.substr(0, std::string(expected).size()), actual);

//! Checks whether this test should run, and if so sets up (cleans up) test environment.
bool setUp(const std::string& function_name, const std::string& specialisation);

//! Checks account for errors and shuts down daemons at end of every test.
bool tearDown();

//! Like tearDown() but returns false, because a test failure was detected.
bool fail();

//! Cleans up the test environment at the start of every test.
void cleanup_test_environment(bool delete_pid_files = false);

//! Report final status of all tests, and return the correct value to test main().
int finish_test_suite();

bool TestFileExists(const char *Filename);
bool TestDirExists(const char *Filename);
bool TestFileNotEmpty(const char *Filename);

// -1 if doesn't exist
int TestGetFileSize(const std::string& Filename);
std::string ConvertPaths(const std::string& rOriginal);
int RunCommand(const std::string& rCommandLine);
bool ServerIsAlive(int pid);
int ReadPidFile(const char *pidFile);

#define TestRemoteProcessMemLeaks(filename) \
	TestRemoteProcessMemLeaksFunc(filename, __FILE__, __LINE__)

bool TestRemoteProcessMemLeaksFunc(const char *filename,
	const char* file, int line);

void force_sync();
void wait_for_sync_start();
void wait_for_sync_end();
void sync_and_wait();
void terminate_bbackupd(int pid);

// Wait a given number of seconds for something to complete
void wait_for_operation(float seconds, const char* message);
void safe_sleep(int seconds);
std::auto_ptr<Configuration> load_config_file(const std::string& config_file,
	const ConfigurationVerify& verify);

bool test_equal_lists(const std::vector<std::string>& expected_items,
	const std::vector<std::string>& actual_items);

typedef std::map<std::string, std::string> str_map_t;
bool test_equal_maps(const str_map_t& expected_attrs, const str_map_t& actual_attrs);

#endif // TEST__H
