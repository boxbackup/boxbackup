// --------------------------------------------------------------------------
//
// File
//		Name:    tests3store.cpp
//		Purpose: Test Amazon S3 storage VFS API and utilities
//		Created: 2015/06/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifndef WIN32
#	include <csignal>
#endif

#include "BackupAccountControl.h"
#include "BackupClientCryptoKeys.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupStoreInfo.h"
#include "Configuration.h"
#include "RaidFileController.h"
#include "ServerControl.h"
#include "SSLLib.h"
#include "Test.h"

#include "MemLeakFindOn.h"

#define DEFAULT_BBACKUPD_CONFIG_FILE "testfiles/bbackupd.conf"

int s3simulator_pid = 0;

bool StartSimulator()
{
	s3simulator_pid = StartDaemon(s3simulator_pid,
		"../../bin/s3simulator/s3simulator " + bbstored_args +
		" testfiles/s3simulator.conf", "testfiles/s3simulator.pid");
	return s3simulator_pid != 0;
}

bool StopSimulator()
{
	bool result = StopDaemon(s3simulator_pid, "testfiles/s3simulator.pid",
		"s3simulator.memleaks", true);
	s3simulator_pid = 0;
	return result;
}

bool kill_running_daemons()
{
	TEST_THAT_OR(::system("test ! -r testfiles/s3simulator.pid || "
		"kill `cat testfiles/s3simulator.pid`") == 0, FAIL);
	TEST_THAT_OR(::system("rm -f testfiles/s3simulator.pid") == 0, FAIL);
	return true;
}

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP_TEST_S3SIMULATOR() \
	SETUP(); \
	TEST_THAT(kill_running_daemons()); \
	TEST_THAT(StartSimulator()); \

#define TEARDOWN_TEST_S3SIMULATOR() \
	TEST_THAT(s3simulator_pid == 0 || StopSimulator()); \
	TEST_THAT(kill_running_daemons()); \
	TEARDOWN();

bool test_create_account_with_account_control()
{
	SETUP_TEST_S3SIMULATOR();

	std::auto_ptr<Configuration> config = load_config_file(DEFAULT_BBACKUPD_CONFIG_FILE,
		BackupDaemonConfigVerify);
	S3BackupAccountControl control(*config);
	control.CreateAccount("test", 1000, 2000);

	FileStream fs("testfiles/store/subdir/" S3_INFO_FILE_NAME);
	std::auto_ptr<BackupStoreInfo> info = BackupStoreInfo::Load(fs, fs.GetFileName(),
		true); // ReadOnly
	TEST_EQUAL(0, info->GetAccountID());
	TEST_EQUAL(1, info->GetLastObjectIDUsed());
	TEST_EQUAL(1, info->GetBlocksUsed());
	TEST_EQUAL(0, info->GetBlocksInCurrentFiles());
	TEST_EQUAL(0, info->GetBlocksInOldFiles());
	TEST_EQUAL(0, info->GetBlocksInDeletedFiles());
	TEST_EQUAL(1, info->GetBlocksInDirectories());
	TEST_EQUAL(0, info->GetDeletedDirectories().size());
	TEST_EQUAL(1000, info->GetBlocksSoftLimit());
	TEST_EQUAL(2000, info->GetBlocksHardLimit());
	TEST_EQUAL(0, info->GetNumCurrentFiles());
	TEST_EQUAL(0, info->GetNumOldFiles());
	TEST_EQUAL(0, info->GetNumDeletedFiles());
	TEST_EQUAL(1, info->GetNumDirectories());
	TEST_EQUAL(true, info->IsAccountEnabled());
	TEST_EQUAL(true, info->IsReadOnly());
	TEST_EQUAL(0, info->GetClientStoreMarker());
	TEST_EQUAL("test", info->GetAccountName());

	TEARDOWN_TEST_S3SIMULATOR();
}

int test(int argc, const char *argv[])
{
	// SSL library
	SSLLib::Initialise();

	// Use the setup crypto command to set up all these keys, so that the bbackupquery command can be used
	// for seeing what's going on.
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	TEST_THAT(test_create_account_with_account_control());

	return finish_test_suite();
}

