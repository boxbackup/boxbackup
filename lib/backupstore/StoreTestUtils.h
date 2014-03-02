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

class BackupProtocolCallable;
class BackupProtocolClient;
class SocketStreamTLS;
class TLSContext;

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP() if (!setUp(__FUNCTION__)) return true; // skip this test

//! Holds the expected reference counts of each object.
extern std::vector<uint32_t> ExpectedRefCounts;

//! Holds the PID of the currently running bbstored test server.
extern int bbstored_pid;

//! Sets up (cleans up) test environment at the start of every test.
bool setUp(const char* function_name);

//! Checks account for errors and shuts down daemons at end of every test.
void tearDown();

//! Sets the expected refcount of an object, resizing vector if necessary.
void set_refcount(int64_t ObjectID, uint32_t RefCount = 1);

//! Initialises a TLSContext object using the standard certficate filenames.
void init_context(TLSContext& rContext);

//! Opens a connection to the server (bbstored).
std::auto_ptr<SocketStream> open_conn(const char *hostname,
	TLSContext& rContext);

//! Opens a connection to the server (bbstored) and logs in.
std::auto_ptr<BackupProtocolCallable> test_server_login(const char *hostname,
	TLSContext& rContext, int flags = 0);

//! Checks the number of files of each type in the store against expectations.
bool check_num_files(int files, int old, int deleted, int dirs);

//! Checks the number of blocks in files of each type against expectations.
bool check_num_blocks(BackupProtocolCallable& Client, int Current, int Old,
	int Deleted, int Dirs, int Total);

//! Checks an account for errors, returning true if it's OK, for use in assertions.
bool check_account(Log::Level log_level = Log::WARNING);

//! Runs housekeeping on an account, to remove old and deleted files if necessary.
int64_t run_housekeeping(BackupStoreAccountDatabase::Entry& rAccount);

//! Runs housekeeping and checks the account, returning true if it's OK.
bool run_housekeeping_and_check_account();

//! Tests that all object reference counts have the expected values.
bool check_reference_counts();

//! Starts the bbstored test server running, which must not already be running.
bool StartServer();

//! Stops the currently running bbstored test server.
bool StopServer(bool wait_for_process = false);

//! Creates the standard test account, for example after delete_account().
bool create_account(int soft, int hard);

//! Deletes the standard test account, for testing behaviour with no account.
bool delete_account();

#define TEST_COMMAND_RETURNS_ERROR(command, error) \
	TEST_CHECK_THROWS(command, ConnectionException, \
		Conn_Protocol_UnexpectedReply); \
	TEST_EQUAL(BackupProtocolError::error, protocol.GetLastErrorType());

#endif // STORETESTUTILS__H

