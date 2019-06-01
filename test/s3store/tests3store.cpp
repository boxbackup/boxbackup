// --------------------------------------------------------------------------
//
// File
//		Name:    tests3store.cpp
//		Purpose: Test Amazon S3 storage VFS API and utilities
//		Created: 2015/06/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_DIRENT_H
#	include <dirent.h> // for opendir(), struct DIR
#endif

#ifndef WIN32
#	include <csignal>
#endif

#include "BackupAccountControl.h"
#include "BackupClientCryptoKeys.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreInfo.h"
#include "Configuration.h"
#include "RaidFileController.h"
#include "ServerControl.h"
#include "StoreTestUtils.h"
#include "SSLLib.h"
#include "Test.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#define DEFAULT_BBACKUPD_CONFIG_FILE "testfiles/bbackupd.conf"

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP_TEST_S3SIMULATOR() \
	SETUP(); \
	TEST_THAT(kill_running_daemons()); \
	TEST_THAT(StartSimulator()); \

#define TEARDOWN_TEST_S3SIMULATOR() \
	TEST_THAT(s3simulator_pid == 0 || StopSimulator()); \
	TEST_THAT(kill_running_daemons()); \
	TEARDOWN();

bool check_new_account_info();

bool test_create_and_delete_account_with_account_control()
{
	SETUP_TEST_S3SIMULATOR();

	std::auto_ptr<Configuration> config = load_config_file(DEFAULT_BBACKUPD_CONFIG_FILE,
		BackupDaemonConfigVerify);
	TEST_LINE_OR(config.get(), "Failed to load configuration, aborting", FAIL);

	{
		S3BackupAccountControl control(*config);
		control.CreateAccount("test", 1000, 2000);
		TEST_THAT(check_new_account_info());

		TEST_THAT(FileExists("testfiles/store/subdir/0x1.dir"));
		TEST_THAT(FileExists("testfiles/store/subdir/boxbackup.info"));
		TEST_THAT(FileExists("testfiles/store/subdir/boxbackup.refcount.db"));

		control.DeleteAccount(false); // !AskForConfirmation

		TEST_THAT(!FileExists("testfiles/store/subdir/0x1.dir"));
		TEST_THAT(!FileExists("testfiles/store/subdir/boxbackup.info"));
		TEST_THAT(!FileExists("testfiles/store/subdir/boxbackup.refcount.db"));

		// Exit scope to release S3BackupFileSystem now, writing the refcount db back to the
		// store, before stopping the simulator daemon!
	}

	DIR* pDir = opendir("testfiles/store/subdir");
	if(!pDir)
	{
		THROW_SYS_FILE_ERROR("Failed to open test temporary directory",
			"testfiles/store/subdir", CommonException, Internal);
	}

	struct dirent* pEntry;
	for(pEntry = readdir(pDir); pEntry; pEntry = readdir(pDir))
	{
		std::string filename = pEntry->d_name;
		if(filename != "." && filename != "..")
		{
			TEST_FAIL_WITH_MESSAGE("Unexpected file remained in testfiles/store/subdir "
				"after account deletion: " + filename);
		}
	}

	closedir(pDir);

	TEARDOWN_TEST_S3SIMULATOR();
}

bool check_new_account_info()
{
	int old_failure_count_local = num_failures;

	FileStream fs("testfiles/store/subdir/" S3_INFO_FILE_NAME);
	std::auto_ptr<BackupStoreInfo> info = BackupStoreInfo::Load(fs, fs.GetFileName(),
		true); // ReadOnly
	TEST_EQUAL(S3_FAKE_ACCOUNT_ID, info->GetAccountID());
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
	// ClientStoreMarker is now initialised to a pseudorandom number, so cannot test its value
	TEST_EQUAL("test", info->GetAccountName());

	FileStream root_stream("testfiles/store/subdir/0x1.dir");
	BackupStoreDirectory root_dir(root_stream);
	TEST_EQUAL(0, root_dir.GetNumberOfEntries());

	// Return true if no new failures.
	return (old_failure_count_local == num_failures);
}

#define BBSTOREACCOUNTS_COMMAND BBSTOREACCOUNTS " -3 -c " \
	DEFAULT_BBACKUPD_CONFIG_FILE "  "

bool test_bbstoreaccounts_commands()
{
	SETUP_TEST_S3SIMULATOR();

	TEST_RETURN(system(BBSTOREACCOUNTS_COMMAND "create test 1000B 2000B"), 0);
	TEST_THAT(check_new_account_info());

	TEST_RETURN(system(BBSTOREACCOUNTS_COMMAND "name foo"), 0);
	FileStream fs("testfiles/store/subdir/" S3_INFO_FILE_NAME);
	std::auto_ptr<BackupStoreInfo> apInfo = BackupStoreInfo::Load(fs, fs.GetFileName(),
		true); // ReadOnly
	TEST_EQUAL("foo", apInfo->GetAccountName());

	TEST_RETURN(system(BBSTOREACCOUNTS_COMMAND "enabled no"), 0);
	fs.Seek(0, IOStream::SeekType_Absolute);
	apInfo = BackupStoreInfo::Load(fs, fs.GetFileName(), true); // ReadOnly
	TEST_EQUAL(false, apInfo->IsAccountEnabled());

	TEST_RETURN(system(BBSTOREACCOUNTS_COMMAND "info"), 0);

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

	TEST_THAT(test_create_and_delete_account_with_account_control());
	TEST_THAT(test_bbstoreaccounts_commands());

	return finish_test_suite();
}

