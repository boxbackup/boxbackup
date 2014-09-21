// --------------------------------------------------------------------------
//
// File
//		Name:    StoreTestUtils.cpp
//		Purpose: Utilities for housekeeping and checking a test store
//		Created: 18/02/14
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <cstdio>
#include <vector>

#include "autogen_BackupProtocol.h"
#include "BoxPortsAndFiles.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreConstants.h"
#include "BackupStoreInfo.h"
#include "HousekeepStoreAccount.h"
#include "Logging.h"
#include "ServerControl.h"
#include "SocketStreamTLS.h"
#include "StoreTestUtils.h"
#include "TLSContext.h"
#include "Test.h"

bool create_account(int soft, int hard)
{
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			("testfiles/bbstored.conf", &BackupConfigFileVerify, errs));
	BackupStoreAccountsControl control(*config);
	
	Logger::LevelGuard guard(Logging::GetConsole(), Log::WARNING);
	int result = control.CreateAccount(0x01234567, 0, soft, hard);
	TEST_EQUAL(0, result);
	return (result == 0);
}

bool delete_account()
{
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			("testfiles/bbstored.conf", &BackupConfigFileVerify, errs));
	BackupStoreAccountsControl control(*config);
	Logger::LevelGuard guard(Logging::GetConsole(), Log::WARNING);
	TEST_THAT_THROWONFAIL(control.DeleteAccount(0x01234567, false) == 0);
	return true;
}

std::vector<uint32_t> ExpectedRefCounts;
int bbstored_pid = 0, bbackupd_pid = 0;
std::string OriginalWorkingDir;

bool setUp(const char* function_name)
{
	if (!run_only_named_tests.empty())
	{
		bool run_this_test = false;

		for (std::list<std::string>::iterator
			i = run_only_named_tests.begin();
			i != run_only_named_tests.end(); i++)
		{
			if (*i == function_name)
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

	if (ServerIsAlive(bbstored_pid))
	{
		StopServer();
	}

	if (OriginalWorkingDir == "")
	{
		char buf[1024];
		if (getcwd(buf, sizeof(buf)) == NULL)
		{
			BOX_LOG_SYS_ERROR("getcwd");
		}
		OriginalWorkingDir = buf;
	}
	else
	{
		if (chdir(OriginalWorkingDir.c_str()) != 0)
		{
			BOX_LOG_SYS_ERROR("chdir");
		}
	}

	TEST_THAT_THROWONFAIL(system(
		"rm -rf testfiles/TestDir* testfiles/0_0 testfiles/0_1 "
		"testfiles/0_2 testfiles/accounts.txt " // testfiles/test* .tgz!
		"testfiles/file* testfiles/notifyran testfiles/notifyran.* "
		"testfiles/notifyscript.tag* "
		"testfiles/restore* testfiles/bbackupd-data "
		"testfiles/syncallowscript.control "
		"testfiles/syncallowscript.notifyran.* "
		"testfiles/test2.downloaded") == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_0", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_1", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/0_2", 0755) == 0);
	TEST_THAT_THROWONFAIL(mkdir("testfiles/bbackupd-data", 0755) == 0);
	TEST_THAT_THROWONFAIL(system("touch testfiles/accounts.txt") == 0);
	TEST_THAT_THROWONFAIL(create_account(10000, 20000));

	ExpectedRefCounts.resize(BACKUPSTORE_ROOT_DIRECTORY_ID + 1);
	set_refcount(BACKUPSTORE_ROOT_DIRECTORY_ID, 1);

	return true;
}

bool tearDown()
{
	bool status = true;

	if (ServerIsAlive(bbstored_pid))
	{
		TEST_THAT_OR(StopServer(), status = false);
	}

	return status;
}

bool fail()
{
	TEST_THAT(tearDown());
	return false;
}

void set_refcount(int64_t ObjectID, uint32_t RefCount)
{
	if ((int64_t)ExpectedRefCounts.size() <= ObjectID)
	{
		ExpectedRefCounts.resize(ObjectID + 1, 0);
	}
	ExpectedRefCounts[ObjectID] = RefCount;
	for (size_t i = ExpectedRefCounts.size() - 1; i >= 1; i--)
	{
		if (ExpectedRefCounts[i] == 0)
		{
			// BackupStoreCheck and housekeeping will both
			// regenerate the refcount DB without any missing
			// items at the end, so we need to prune ourselves
			// of all items with no references to match.
			ExpectedRefCounts.resize(i);
		}
	}
}

void init_context(TLSContext& rContext)
{
	rContext.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");
}

std::auto_ptr<SocketStream> open_conn(const char *hostname,
	TLSContext& rContext)
{
	init_context(rContext);
	std::auto_ptr<SocketStreamTLS> conn(new SocketStreamTLS);
	conn->Open(rContext, Socket::TypeINET, hostname,
		BOX_PORT_BBSTORED_TEST);
	return static_cast<std::auto_ptr<SocketStream> >(conn);
}

std::auto_ptr<BackupProtocolCallable> connect_to_bbstored(TLSContext& rContext)
{
	// Make a protocol
	std::auto_ptr<BackupProtocolCallable> protocol(new
		BackupProtocolClient(open_conn("localhost", rContext)));
	
	// Check the version
	std::auto_ptr<BackupProtocolVersion> serverVersion(
		protocol->QueryVersion(BACKUP_STORE_SERVER_VERSION));
	TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);

	return protocol;
}

std::auto_ptr<BackupProtocolCallable> connect_and_login(TLSContext& rContext,
	int flags)
{
	// Make a protocol
	std::auto_ptr<BackupProtocolCallable> protocol =
		connect_to_bbstored(rContext);

	// Login
	protocol->QueryLogin(0x01234567, flags);

	return protocol;
}

bool check_num_files(int files, int old, int deleted, int dirs)
{
	std::auto_ptr<BackupStoreInfo> apInfo =
		BackupStoreInfo::Load(0x1234567,
		"backup/01234567/", 0, true);
	TEST_EQUAL_LINE(files, apInfo->GetNumCurrentFiles(),
		"current files");
	TEST_EQUAL_LINE(old, apInfo->GetNumOldFiles(),
		"old files");
	TEST_EQUAL_LINE(deleted, apInfo->GetNumDeletedFiles(),
		"deleted files");
	TEST_EQUAL_LINE(dirs, apInfo->GetNumDirectories(),
		"directories");

	return (files == apInfo->GetNumCurrentFiles() &&
		old == apInfo->GetNumOldFiles() &&
		deleted == apInfo->GetNumDeletedFiles() &&
		dirs == apInfo->GetNumDirectories());
}

bool check_num_blocks(BackupProtocolCallable& Client, int Current, int Old,
	int Deleted, int Dirs, int Total)
{
	std::auto_ptr<BackupProtocolAccountUsage2> usage =
		Client.QueryGetAccountUsage2();
	TEST_EQUAL_LINE(Total, usage->GetBlocksUsed(), "wrong BlocksUsed");
	TEST_EQUAL_LINE(Current, usage->GetBlocksInCurrentFiles(),
		"wrong BlocksInCurrentFiles");
	TEST_EQUAL_LINE(Old, usage->GetBlocksInOldFiles(),
		"wrong BlocksInOldFiles");
	TEST_EQUAL_LINE(Deleted, usage->GetBlocksInDeletedFiles(),
		"wrong BlocksInDeletedFiles");
	TEST_EQUAL_LINE(Dirs, usage->GetBlocksInDirectories(),
		"wrong BlocksInDirectories");
	return (Total == usage->GetBlocksUsed() &&
		Current == usage->GetBlocksInCurrentFiles() &&
		Old == usage->GetBlocksInOldFiles() &&
		Deleted == usage->GetBlocksInDeletedFiles() &&
		Dirs == usage->GetBlocksInDirectories());
}

bool change_account_limits(const char* soft, const char* hard)
{
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			("testfiles/bbstored.conf", &BackupConfigFileVerify, errs));
	BackupStoreAccountsControl control(*config);
	int result = control.SetLimit(0x01234567, soft, hard);
	TEST_EQUAL(0, result);
	return (result == 0);
}

int check_account_for_errors(Log::Level log_level)
{
	Logger::LevelGuard guard(Logging::GetConsole(), log_level);
	Logging::Tagger tag("check fix", true);
	Logging::ShowTagOnConsole show;
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify("testfiles/bbstored.conf",
			&BackupConfigFileVerify, errs));
	BackupStoreAccountsControl control(*config);
	int errors_fixed = control.CheckAccount(0x01234567,
		true, // FixErrors
		false); // Quiet
	return errors_fixed;
}

bool check_account(Log::Level log_level)
{
	int errors_fixed = check_account_for_errors(log_level);
	TEST_EQUAL(0, errors_fixed);
	return (errors_fixed == 0);
}

int64_t run_housekeeping(BackupStoreAccountDatabase::Entry& rAccount)
{
	std::string rootDir = BackupStoreAccounts::GetAccountRoot(rAccount);
	int discSet = rAccount.GetDiscSet();

	// Do housekeeping on this account
	HousekeepStoreAccount housekeeping(rAccount.GetID(), rootDir,
		discSet, NULL);
	housekeeping.DoHousekeeping(true /* keep trying forever */);
	return housekeeping.GetErrorCount();
}

// Run housekeeping (for which we need to disconnect ourselves) and check
// that it doesn't change the numbers of files.
//
// Also check that bbstoreaccounts doesn't change anything

bool run_housekeeping_and_check_account()
{
	int error_count;

	{
		Logging::Tagger tag("", true);
		Logging::ShowTagOnConsole show;
		std::auto_ptr<BackupStoreAccountDatabase> apAccounts(
			BackupStoreAccountDatabase::Read("testfiles/accounts.txt"));
		BackupStoreAccountDatabase::Entry account =
			apAccounts->GetEntry(0x1234567);
		error_count = run_housekeeping(account);
	}

	TEST_EQUAL_LINE(0, error_count, "housekeeping errors");

	bool check_account_is_ok = check_account();
	TEST_THAT(check_account_is_ok);

	return error_count == 0 && check_account_is_ok;
}

bool check_reference_counts()
{
	std::auto_ptr<BackupStoreAccountDatabase> apAccounts(
		BackupStoreAccountDatabase::Read("testfiles/accounts.txt"));
	BackupStoreAccountDatabase::Entry account =
		apAccounts->GetEntry(0x1234567);

	std::auto_ptr<BackupStoreRefCountDatabase> apReferences(
		BackupStoreRefCountDatabase::Load(account, true));
	TEST_EQUAL(ExpectedRefCounts.size(),
		apReferences->GetLastObjectIDUsed() + 1);

	bool counts_ok = true;

	for (unsigned int i = BackupProtocolListDirectory::RootDirectory;
		i < ExpectedRefCounts.size(); i++)
	{
		TEST_EQUAL_LINE(ExpectedRefCounts[i],
			apReferences->GetRefCount(i),
			"object " << BOX_FORMAT_OBJECTID(i));
		if (ExpectedRefCounts[i] != apReferences->GetRefCount(i))
		{
			counts_ok = false;
		}
	}

	return counts_ok;
}

bool StartServer()
{
	TEST_THAT_OR(bbstored_pid == 0, return false);

	std::string cmd = BBSTORED " " + bbstored_args +
		" testfiles/bbstored.conf";
	bbstored_pid = LaunchServer(cmd.c_str(), "testfiles/bbstored.pid");

	TEST_THAT_OR(bbstored_pid != -1 && bbstored_pid != 0, return false);

	::sleep(1);
	TEST_THAT_OR(ServerIsAlive(bbstored_pid), return false);
	return true;
}

bool StopServer(bool wait_for_process)
{
	TEST_THAT_OR(bbstored_pid != 0, return false);
	TEST_THAT_OR(ServerIsAlive(bbstored_pid), return false);
	TEST_THAT_OR(KillServer(bbstored_pid, wait_for_process), return false);
	::sleep(1);

	TEST_THAT_OR(!ServerIsAlive(bbstored_pid), return false);
	bbstored_pid = 0;

	#ifdef WIN32
		int unlink_result = unlink("testfiles/bbstored.pid");
		TEST_EQUAL_LINE(0, unlink_result, "unlink testfiles/bbstored.pid");
		if(unlink_result != 0)
		{
			return false;
		}
	#else
		TestRemoteProcessMemLeaks("bbstored.memleaks");
	#endif

	return true;
}

#define FAIL { \
	/* \
	std::ostringstream os; \
	os << "failed at " << __FUNCTION__ << ":" << __LINE__; \
	s_test_status[current_test_name] = os.str(); \
	return fail(); \
	*/ \
	return false; \
}

bool StartClient(const std::string& bbackupd_conf_file)
{
	TEST_THAT_OR(bbackupd_pid == 0, FAIL);

	std::string cmd = BBACKUPD " " + bbackupd_args + " " + bbackupd_conf_file;
	bbackupd_pid = LaunchServer(cmd.c_str(), "testfiles/bbackupd.pid");

	TEST_THAT_OR(bbackupd_pid != -1 && bbackupd_pid != 0, FAIL);
	::sleep(1);
	TEST_THAT_OR(ServerIsAlive(bbackupd_pid), FAIL);

	return true;
}

bool StopClient(bool wait_for_process)
{
	TEST_THAT_OR(bbackupd_pid != 0, FAIL);
	TEST_THAT_OR(ServerIsAlive(bbackupd_pid), FAIL);
	TEST_THAT_OR(KillServer(bbackupd_pid, wait_for_process), FAIL);
	::sleep(1);

	TEST_THAT_OR(!ServerIsAlive(bbackupd_pid), FAIL);
	bbackupd_pid = 0;

	#ifdef WIN32
		int unlink_result = unlink("testfiles/bbackupd.pid");
		TEST_EQUAL_LINE(0, unlink_result, "unlink testfiles/bbackupd.pid");
		if(unlink_result != 0)
		{
			FAIL;
		}
	#else
		TestRemoteProcessMemLeaks("bbackupd.memleaks");
	#endif

	return true;
}
