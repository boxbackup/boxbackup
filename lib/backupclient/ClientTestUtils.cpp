// --------------------------------------------------------------------------
//
// File
//		Name:    ClientTestUtils.cpp
//		Purpose: Utilities for specialised client tests
//		Created: 2018-05-29
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <cstdio>
#include <string>
#include <vector>

#include "autogen_BackupProtocol.h"
#include "BackupAccountControl.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreConstants.h"
#include "BackupStoreInfo.h"
#include "BoxPortsAndFiles.h"
#include "ClientTestUtils.h"
#include "Logging.h"
#include "StoreStructure.h"
#include "StoreTestUtils.h"
#include "Test.h"

RaidAndS3TestSpecs::RaidAndS3TestSpecs(const std::string& bbackupd_config)
{
	auto ap_s3_config = load_config_file(bbackupd_config, BackupDaemonConfigVerify);
	std::auto_ptr<BackupAccountControl> ap_s3_control(new S3BackupAccountControl(*ap_s3_config));

	auto ap_store_config = load_config_file(DEFAULT_BBSTORED_CONFIG_FILE, BackupConfigFileVerify);
	std::auto_ptr<BackupAccountControl> ap_store_control(
		new BackupStoreAccountControl(*ap_store_config, 0x01234567));

	mSpecialisations.push_back(Specialisation("s3", ap_s3_config, ap_s3_control));
	mpS3 = &(*mSpecialisations.rbegin());
	mSpecialisations.push_back(Specialisation("store", ap_store_config, ap_store_control));
	mpStore = &(*mSpecialisations.rbegin());
}

bool create_test_account_specialised(const std::string& spec_name,
	BackupAccountControl& control)
{
	if(spec_name == "s3")
	{
		TEST_THAT_OR(
			dynamic_cast<S3BackupAccountControl&>(control).
			CreateAccount("test", 10000, 20000) == 0, FAIL);
	}
	else if(spec_name == "store")
	{
		TEST_THAT_OR(
			dynamic_cast<BackupStoreAccountControl&>(control).
			CreateAccount(0, 10000, 20000) == 0, FAIL);
	}
	else
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Don't know how to create accounts for store type: " <<
			spec_name);
	}

	return true;
}

bool setup_test_specialised(const std::string& spec_name,
	BackupAccountControl& control)
{
	if (ServerIsAlive(bbstored_pid))
	{
		TEST_THAT_OR(StopServer(), FAIL);
	}

	ExpectedRefCounts.resize(BACKUPSTORE_ROOT_DIRECTORY_ID + 1);
	set_refcount(BACKUPSTORE_ROOT_DIRECTORY_ID, 1);

	TEST_THAT_OR(create_test_account_specialised(spec_name, control), FAIL);

	return true;
}

//! Checks account for errors and shuts down daemons at end of every test.
bool teardown_test_specialised(const std::string& spec_name,
	BackupAccountControl& control, bool check_for_errors)
{
	bool status = true;

	if (ServerIsAlive(bbstored_pid))
	{
		TEST_THAT_OR(StopServer(), status = false);
	}

	BackupFileSystem& fs(control.GetFileSystem());
	if(check_for_errors)
	{
		TEST_THAT_OR(check_reference_counts(
			fs.GetPermanentRefCountDatabase(true)), // ReadOnly
			status = false);
		TEST_EQUAL_OR(0, check_account_and_fix_errors(fs), status = false);
	}

	// Release the lock (acquired by check_account_and_fix_errors if it wasn't already held)
	// before the next test's setUp deletes all files in the store, including the lockfile:
	fs.ReleaseLock();

	return status;
}

std::string get_raid_file_path(int64_t ObjectID)
{
	std::string filename;
	StoreStructure::MakeObjectFilename(ObjectID,
		"backup/01234567/" /* mStoreRoot */, 0 /* mStoreDiscSet */,
		filename, false /* EnsureDirectoryExists */);
	return filename;
}

std::string get_s3_file_path(int64_t ObjectID, bool IsDirectory,
	RaidAndS3TestSpecs::Specialisation& spec)
{
	ASSERT(spec.name() == "s3");
	S3BackupFileSystem& fs(dynamic_cast<S3BackupFileSystem&>(spec.control().GetFileSystem()));
	std::string local_path = "testfiles/store" +
		(IsDirectory ? fs.GetDirectoryURI(ObjectID) :
		 fs.GetFileURI(ObjectID));
	return local_path;
}
