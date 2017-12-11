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
#include "BackupAccountControl.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreConstants.h"
#include "BackupStoreInfo.h"
#include "BoxPortsAndFiles.h"
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
	BackupStoreAccountControl control(*config, 0x01234567);
	
	Logger::LevelGuard guard(Logging::GetConsole(), Log::WARNING);
	int result = control.CreateAccount(0, soft, hard);
	TEST_EQUAL(0, result);
	return (result == 0);
}

bool delete_account()
{
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			("testfiles/bbstored.conf", &BackupConfigFileVerify, errs));
	BackupStoreAccountControl control(*config, 0x01234567);
	Logger::LevelGuard guard(Logging::GetConsole(), Log::WARNING);
	TEST_THAT_THROWONFAIL(control.DeleteAccount(false) == 0);
	return true;
}

std::vector<uint32_t> ExpectedRefCounts;
int bbstored_pid = 0, bbackupd_pid = 0;

void set_refcount(int64_t ObjectID, uint32_t RefCount)
{
	if ((int64_t)ExpectedRefCounts.size() <= ObjectID)
	{
		ExpectedRefCounts.resize(ObjectID + 1, 0);
	}

	ExpectedRefCounts[ObjectID] = RefCount;

	// BackupStoreCheck and housekeeping will both regenerate the refcount
	// DB without any missing items at the end, so we need to prune
	// ourselves of all items with no references to match.
	for (size_t i = ExpectedRefCounts.size() - 1; i >= 1; i--)
	{
		if (ExpectedRefCounts[i] == 0)
		{
			ExpectedRefCounts.resize(i);
		}
		else
		{
			// Don't keep going back up the list, as if we found a
			// zero-referenced file higher up, we'd end up deleting
			// the refcounts of referenced files further down the
			// list (higher IDs).
			break;
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

bool check_num_files(BackupFileSystem& fs, int files, int old, int deleted, int dirs)
{
	std::auto_ptr<BackupStoreInfo> apInfo = fs.GetBackupStoreInfoUncached();
	return check_num_files(*apInfo, files, old, deleted, dirs);
}

bool check_num_files(BackupStoreInfo& info, int files, int old, int deleted, int dirs)
{
	TEST_EQUAL_LINE(files, info.GetNumCurrentFiles(), "current files");
	TEST_EQUAL_LINE(old, info.GetNumOldFiles(), "old files");
	TEST_EQUAL_LINE(deleted, info.GetNumDeletedFiles(), "deleted files");
	TEST_EQUAL_LINE(dirs, info.GetNumDirectories(), "directories");

	return (files == info.GetNumCurrentFiles() &&
		old == info.GetNumOldFiles() &&
		deleted == info.GetNumDeletedFiles() &&
		dirs == info.GetNumDirectories());
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
	BackupStoreAccountControl control(*config, 0x01234567);
	return change_account_limits(control, soft, hard);
}

bool change_account_limits(BackupAccountControl& control, const char* soft,
	const char* hard)
{
	int result = control.SetLimit(soft, hard);
	TEST_EQUAL(0, result);
	return (result == 0);
}

int check_account_for_errors(Log::Level log_level)
{
	// TODO FIXME remove this backward-compatibility function
	RaidBackupFileSystem fs(0x1234567, "backup/01234567/", 0);
	return check_account_for_errors(fs, log_level);
}

int check_account_for_errors(BackupFileSystem& filesystem, Log::Level log_level)
{
	Logger::LevelGuard guard(Logging::GetConsole(), log_level);
	Logging::Tagger tag("check fix", true);
	Logging::ShowTagOnConsole show;

	// The caller may already have opened a permanent read-write database, but
	// BackupStoreCheck needs to open a temporary one, so we need to close the
	// permanent one in that case.
	if(filesystem.GetCurrentRefCountDatabase() &&
		!filesystem.GetCurrentRefCountDatabase()->IsReadOnly())
	{
		filesystem.CloseRefCountDatabase(filesystem.GetCurrentRefCountDatabase());
	}

	BackupStoreCheck check(filesystem,
		true, // FixErrors
		false); // Quiet
	check.Check();
	return check.GetNumErrorsFound();
}

int64_t run_housekeeping(BackupStoreAccountDatabase::Entry& rAccount)
{
	// TODO FIXME remove this backward-compatibility function
	std::string rootDir = BackupStoreAccounts::GetAccountRoot(rAccount);
	int discSet = rAccount.GetDiscSet();
	RaidBackupFileSystem fs(rAccount.GetID(), rootDir, discSet);

	// Do housekeeping on this account
	return run_housekeeping(fs);
}

int64_t run_housekeeping(BackupFileSystem& filesystem)
{
	// Do housekeeping on this account
	HousekeepStoreAccount housekeeping(filesystem, NULL);
	// Take a lock before calling DoHousekeeping, because although it does try to get a lock
	// itself, it doesn't give us much control over how long it retries for. We want to retry
	// for ~30 seconds, but not forever, because we don't want tests to hang.
	// filesystem.GetLock(30);
	TEST_THAT(housekeeping.DoHousekeeping(true)); // keep trying forever
	return housekeeping.GetErrorCount();
}

// Run housekeeping (for which we need to disconnect ourselves) and check
// that it doesn't change the numbers of files.
//
// Also check that bbstoreaccounts doesn't change anything

bool run_housekeeping_and_check_account()
{
	// TODO FIXME remove this backward-compatibility function
	RaidBackupFileSystem fs(0x1234567, "backup/01234567/", 0);
	return run_housekeeping_and_check_account(fs);
}

bool run_housekeeping_and_check_account(BackupFileSystem& filesystem)
{
	if(filesystem.GetCurrentRefCountDatabase() != NULL)
	{
		filesystem.CloseRefCountDatabase(filesystem.GetCurrentRefCountDatabase());
	}

	int num_housekeeping_errors;

	{
		Logging::Tagger tag("", true);
		Logging::ShowTagOnConsole show;
		num_housekeeping_errors = run_housekeeping(filesystem);
	}
	TEST_EQUAL_LINE(0, num_housekeeping_errors, "run_housekeeping");

	filesystem.CloseRefCountDatabase(filesystem.GetCurrentRefCountDatabase());

	int num_check_errors = check_account_for_errors(filesystem);
	TEST_EQUAL_LINE(0, num_check_errors, "check_account_for_errors");

	filesystem.CloseRefCountDatabase(filesystem.GetCurrentRefCountDatabase());

	return num_housekeeping_errors == 0 && num_check_errors == 0;
}

bool check_reference_counts()
{
	std::auto_ptr<BackupStoreAccountDatabase> apAccounts(
		BackupStoreAccountDatabase::Read("testfiles/accounts.txt"));
	BackupStoreAccountDatabase::Entry account =
		apAccounts->GetEntry(0x1234567);

	std::auto_ptr<BackupStoreRefCountDatabase> apReferences =
		BackupStoreRefCountDatabase::Load(account, true);
	TEST_EQUAL(ExpectedRefCounts.size(),
		apReferences->GetLastObjectIDUsed() + 1);

	return check_reference_counts(*apReferences);
}

bool check_reference_counts(BackupStoreRefCountDatabase& references)
{
	bool counts_ok = true;

	TEST_EQUAL_OR(ExpectedRefCounts.size(),
		references.GetLastObjectIDUsed() + 1,
		counts_ok = false);

	for (unsigned int i = BackupProtocolListDirectory::RootDirectory;
		i < ExpectedRefCounts.size(); i++)
	{
		TEST_EQUAL_LINE(ExpectedRefCounts[i],
			references.GetRefCount(i),
			"object " << BOX_FORMAT_OBJECTID(i));
		if (ExpectedRefCounts[i] != references.GetRefCount(i))
		{
			counts_ok = false;
		}
	}

	return counts_ok;
}

bool StartServer(const std::string& daemon_args)
{
	const std::string& daemon_args_final(daemon_args.size() ? daemon_args : bbstored_args);
	bbstored_pid = StartDaemon(bbstored_pid, BBSTORED " " + daemon_args_final +
		" testfiles/bbstored.conf", "testfiles/bbstored.pid");
	return bbstored_pid != 0;
}

bool StopServer(bool wait_for_process)
{
	bool result = StopDaemon(bbstored_pid, "testfiles/bbstored.pid",
		"bbstored.memleaks", wait_for_process);
	bbstored_pid = 0;
	return result;
}

bool StartClient(const std::string& bbackupd_conf_file, const std::string& daemon_args)
{
	const std::string& daemon_args_final(daemon_args.size() ? daemon_args : bbackupd_args);
	bbackupd_pid = StartDaemon(bbackupd_pid, BBACKUPD " " + daemon_args_final +
		" -c " + bbackupd_conf_file, "testfiles/bbackupd.pid");
	return bbackupd_pid != 0;
}

bool StopClient(bool wait_for_process)
{
	bool result = StopDaemon(bbackupd_pid, "testfiles/bbackupd.pid",
		"bbackupd.memleaks", wait_for_process);
	bbackupd_pid = 0;
	return result;
}

