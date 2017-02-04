// --------------------------------------------------------------------------
//
// File
//		Name:    StoreTestUtils.h
//		Purpose: Utilities for housekeeping and checking a test store
//		Created: 18/02/14
//
// --------------------------------------------------------------------------

#ifndef STORETESTUTILS__H
#define STORETESTUTILS__H

#include "Test.h"

class BackupAccountControl;
class BackupProtocolCallable;
class BackupProtocolClient;
class BackupFileSystem;
class SocketStreamTLS;
class TLSContext;

//! Holds the expected reference counts of each object.
extern std::vector<uint32_t> ExpectedRefCounts;

//! Holds the PID of the currently running bbstored test server.
extern int bbstored_pid, bbackupd_pid;

//! Sets the expected refcount of an object, resizing vector if necessary.
void set_refcount(int64_t ObjectID, uint32_t RefCount = 1);

//! Initialises a TLSContext object using the standard certficate filenames.
void init_context(TLSContext& rContext);

//! Opens a connection to the server (bbstored).
std::auto_ptr<SocketStream> open_conn(const char *hostname,
	TLSContext& rContext);

//! Opens a connection to the server (bbstored) without logging in.
std::auto_ptr<BackupProtocolCallable> connect_to_bbstored(TLSContext& rContext);

//! Opens a connection to the server (bbstored) and logs in.
std::auto_ptr<BackupProtocolCallable> connect_and_login(TLSContext& rContext,
	int flags = 0);

//! Checks the number of files of each type in the store against expectations.
bool check_num_files(BackupFileSystem& fs, int files, int old, int deleted, int dirs);

//! Checks the number of blocks in files of each type against expectations.
bool check_num_blocks(BackupProtocolCallable& Client, int Current, int Old,
	int Deleted, int Dirs, int Total);

//! Change the soft and hard limits on the test account.
//! Old interface, only works with RaidBackupFileSystem, deprecated.
bool change_account_limits(const char* soft, const char* hard);

//! Change the soft and hard limits on the test account.
//! New interface, takes a BackupAccountControl.
bool change_account_limits(BackupAccountControl& control, const char* soft,
	const char* hard);

//! Checks an account for errors, returning the number of errors found and fixed.
int check_account_for_errors(Log::Level log_level = Log::WARNING);

//! Checks an account for errors, returning number of errors found.
int check_account_for_errors(BackupFileSystem& filesystem,
	Log::Level log_level = Log::WARNING);

//! Runs housekeeping on an account, to remove old and deleted files if necessary.
//! Old interface, only works with RaidBackupFileSystem, deprecated.
int64_t run_housekeeping(BackupStoreAccountDatabase::Entry& rAccount);

//! Runs housekeeping on an account, to remove old and deleted files if necessary.
//! New interface, takes a BackupFileSystem.
int64_t run_housekeeping(BackupFileSystem& filesystem);

//! Runs housekeeping and checks the account, returning true if it's OK.
//! Old interface, only works with RaidBackupFileSystem, deprecated.
bool run_housekeeping_and_check_account();

//! Runs housekeeping and checks the account, returning true if it's OK.
//! New interface, takes a BackupFileSystem.
bool run_housekeeping_and_check_account(BackupFileSystem& filesystem);

//! Tests that all object reference counts have the expected values.
//! Old interface, only works with RaidBackupFileSystem, deprecated.
bool check_reference_counts();

//! Tests that all object reference counts have the expected values.
bool check_reference_counts(BackupStoreRefCountDatabase& references);

//! Starts the bbstored test server running, which must not already be running.
bool StartServer();

//! Stops the currently running bbstored test server.
bool StopServer(bool wait_for_process = false);

//! Starts the bbackupd client running, which must not already be running.
bool StartClient(const std::string& bbackupd_conf_file = "testfiles/bbackupd.conf");

//! Stops the currently running bbackupd client.
bool StopClient(bool wait_for_process = false);

//! Creates the standard test account, for example after delete_account().
bool create_account(int soft, int hard);

//! Deletes the standard test account, for testing behaviour with no account.
bool delete_account();

// You might want to use TEST_COMMAND_RETURNS_ERROR instead of TEST_PROTOCOL_ERROR_OR for new code:
#define TEST_PROTOCOL_ERROR_OR(protocol, error, or_statements) \
	{ \
		int type, subtype; \
		(protocol).GetLastError(type, subtype); \
		if (type == BackupProtocolError::ErrorType) \
		{ \
			TEST_EQUAL_LINE(BackupProtocolError::error, subtype, \
				"command returned error: " << \
				(protocol).GetLastErrorMessage()); \
			if (subtype != BackupProtocolError::error) \
			{ \
				or_statements; \
			} \
		} \
		else \
		{ \
			TEST_FAIL_WITH_MESSAGE("command did not return an error, " \
				"but " << (protocol).GetLastErrorMessage() << " " \
				"instead (" << type << "/" << subtype << ")"); \
			or_statements; \
		} \
	}

#define TEST_COMMAND_RETURNS_ERROR_OR(protocol, command, error, or_statements) \
	TEST_CHECK_THROWS_AND_OR((protocol) . command, ConnectionException, \
		Protocol_UnexpectedReply, /* and_command */, or_statements); \
	TEST_PROTOCOL_ERROR_OR(protocol, error, or_statements)

#define TEST_COMMAND_RETURNS_ERROR(protocol, command, error) \
	TEST_COMMAND_RETURNS_ERROR_OR(protocol, command, error,)

#endif // STORETESTUTILS__H

