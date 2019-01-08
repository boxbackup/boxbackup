// --------------------------------------------------------------------------
//
// File
//		Name:    testbackupstorefix.cpp
//		Purpose: Test BackupStoreCheck functionality
//		Created: 23/4/04
//
// --------------------------------------------------------------------------


#include "Box.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <map>

#include "BackupAccountControl.h"
#include "BackupClientCryptoKeys.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupProtocol.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreCheck.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreFileEncodeStream.h"
#include "BackupStoreInfo.h"
#include "BufferedWriteStream.h"
#include "ClientTestUtils.h"
#include "FileStream.h"
#include "IOStreamGetLine.h"
#include "RaidFileController.h"
#include "RaidFileException.h"
#include "RaidFileRead.h"
#include "RaidFileUtil.h"
#include "RaidFileWrite.h"
#include "SSLLib.h"
#include "ServerControl.h"
#include "StoreStructure.h"
#include "StoreTestUtils.h"
#include "Test.h"
#include "TLSContext.h"
#include "ZeroStream.h"

#include "MemLeakFindOn.h"

/*

Errors checked:

make some BackupDirectoryStore objects, CheckAndFix(), then verify
	- multiple objects with same ID
	- wrong order of old flags
	- all old flags

delete store info
add spurious file
delete directory (should appear again)
change container ID of directory
delete a file
double reference to a file inside a single dir
modify the object ID of a directory
delete directory, which has no members (will be removed)
extra reference to a file in another dir (higher ID to allow consistency -- use something in subti)
delete dir + dir2 in dir/dir2/file where nothing in dir2 except file, file should end up in lost+found
similarly with a dir, but that should get a dirxxx name
corrupt dir
corrupt file
delete root, copy a file to it instead (equivalent to deleting it too)

*/

bool setup_test_backupstorefix_unified()
{
	TEST_THAT_OR(setup_test_unified(), FAIL);

	// Run the perl script to create the initial directories
	TEST_THAT_OR(::system(PERL_EXECUTABLE
		" testfiles/testbackupstorefix.pl init") == 0, FAIL);

	return true;
}

bool setup_test_backupstorefix_specialised(RaidAndS3TestSpecs::Specialisation& spec)
{
	// Account already created by setup_test_specialised/SETUP()

	// Run the perl script to create the initial directories
	TEST_THAT_OR(::system(PERL_EXECUTABLE
		" testfiles/testbackupstorefix.pl init") == 0, FAIL);

	return true;
}

std::map<std::string, int32_t> nameToID;
std::map<int32_t, bool> objectIsDir;

bool list_uploaded_files(RaidAndS3TestSpecs::Specialisation* pSpec)
{
	// Generate a list of all the object IDs
	if(pSpec == NULL || pSpec->name() == "store")
	{
		TEST_THAT_OR(::system(BBACKUPQUERY " -Wwarning "
			"-c testfiles/bbackupd.bbstored.conf \"list -R\" quit "
			"> testfiles/initial-listing.txt") == 0, FAIL);
	}
	else
	{
		TEST_THAT_OR(::system(BBACKUPQUERY " -Wwarning "
			"-c testfiles/bbackupd.s3.conf \"list -R\" quit "
			"> testfiles/initial-listing.txt") == 0, FAIL);
	}

	// And load it in
	{
		FILE *f = ::fopen("testfiles/initial-listing.txt", "r");
		TEST_THAT_ABORTONFAIL(f != 0);
		char line[512];
		int32_t id;
		char flags[32];
		char name[256];
		while(::fgets(line, sizeof(line), f) != 0)
		{
			int items_assigned = ::sscanf(line, "%x %s %s", &id, flags, name);
			TEST_EQUAL_LINE(3, items_assigned, line);
			if(items_assigned == 3)
			{
				bool isDir = (::strcmp(flags, "-d---") == 0);
				//TRACE3("%x,%d,%s\n", id, isDir, name);
				MEMLEAKFINDER_NO_LEAKS;
				nameToID[std::string(name)] = id;
				objectIsDir[id] = isDir;
				set_refcount(id, 1);
			}
		}
		::fclose(f);
	}

	TEST_EQUAL_OR(141, nameToID.size(), FAIL);

	return true;
}

bool setup_test_backupstorefix_unified_with_bbstored()
{
	TEST_THAT_OR(setup_test_backupstorefix_unified(), FAIL);
	TEST_THAT_OR(StartServer(), FAIL);

	TEST_THAT_OR(StartClient("testfiles/bbackupd.bbstored.conf"), FAIL);
	sync_and_wait();
	TEST_THAT_OR(StopClient(), FAIL);
	TEST_THAT_OR(list_uploaded_files(NULL), FAIL);

	return true;
}

bool setup_test_backupstorefix_specialised_with_bbstored(RaidAndS3TestSpecs::Specialisation& spec)
{
	TEST_THAT_OR(setup_test_backupstorefix_specialised(spec), FAIL);
	spec.control().GetFileSystem().ReleaseLock();

	// BackupDaemon bbackupd;
	// Add extra logging to help debug random test failures on Travis due to inability to
	// connect to the daemon at StopClient() time:
	std::string bbackupd_args_final(bbackupd_args_overridden ? bbackupd_args :
		"-kT -Wnotice -tbbackupd");

	if(spec.name() == "s3")
	{
		TEST_THAT(StartClient("testfiles/bbackupd.s3.conf", bbackupd_args_final));
		// TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd.s3.conf"));
	}
	else
	{
		// Start the bbstored server. Enable logging to help debug if the store is
		// unexpectedly locked when we try to check or query it (race conditions):
		std::string bbstored_args_final(bbstored_args_overridden ? bbstored_args :
			"-kT -Winfo -tbbstored -L/FileSystem/Locking=trace");
		TEST_THAT_OR(StartServer(bbstored_args_final), FAIL);
		TEST_THAT(StartClient("testfiles/bbackupd.bbstored.conf", bbackupd_args_final));
		// TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd.bbstored.conf"));
	}

	sync_and_wait();
	TEST_THAT_OR(StopClient(), FAIL);
	// bbackupd.RunSyncNow();

	TEST_THAT_OR(list_uploaded_files(&spec), FAIL);

	return true;
}

std::string accountRootDir("backup/01234567/");
int discSetNum = 0;

TLSContext tls_context;

// Get ID of an object given a filename
int32_t getID(const char *name)
{
	std::map<std::string, int32_t>::iterator i(nameToID.find(std::string(name)));
	TEST_LINE(i != nameToID.end(), "Failed to find object ID for test file: " << name);
	if(i == nameToID.end()) return -1;
	return i->second;
}

BackupStoreFilename fnames[3];

typedef struct
{
	int name;
	int64_t id;
	int flags;
} dir_en_check;

bool check_dir(BackupStoreDirectory &dir, dir_en_check *ck)
{
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en;
	bool ok = true;

	while((en = i.Next()) != 0)
	{
		BackupStoreFilenameClear clear(en->GetName());
		TEST_LINE(ck->name != -1, "Unexpected entry found in "
			"directory: " << clear.GetClearFilename());
		if(ck->name == -1)
		{
			break;
		}
		TEST_THAT(en->GetName() == fnames[ck->name]);
		TEST_THAT(en->GetObjectID() == ck->id);
		TEST_THAT(en->GetFlags() == ck->flags);
		++ck;
	}

	TEST_EQUAL_OR((void *)NULL, (void *)en, ok = false);
	TEST_EQUAL_OR(ck->name, -1, ok = false);
	return ok;
}

typedef struct
{
	int64_t id, depends_on, required_by;
} checkdepinfoen;

bool check_dir_dep(BackupStoreDirectory &dir, checkdepinfoen *ck)
{
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en;
	bool status_ok = true;

	while((en = i.Next()) != 0)
	{
		TEST_THAT_OR(ck->id != -1, status_ok = false);
		if(ck->id == -1)
		{
			break;
		}
		TEST_EQUAL_LINE_OR(ck->id, en->GetObjectID(), "Wrong object ID "
			"for " << BOX_FORMAT_OBJECTID(ck->id), status_ok = false);
		TEST_EQUAL_LINE_OR(ck->depends_on, en->GetDependsOnObject(),
			"Wrong DependsOnObject for " << BOX_FORMAT_OBJECTID(ck->id),
			status_ok = false);
		TEST_EQUAL_LINE_OR(ck->required_by, en->GetRequiredByObject(),
			"Wrong RequiredByObject for " << BOX_FORMAT_OBJECTID(ck->id),
			status_ok = false);
		++ck;
	}

	TEST_THAT_OR(en == 0, status_ok = false);
	TEST_THAT_OR(ck->id == -1, status_ok = false);

	return status_ok;
}

bool test_dir_fixing()
{
	SETUP();
	TEST_THAT(setup_test_backupstorefix_unified());

	// Test that entries pointing to nonexistent entries are removed
	{
		BackupStoreDirectory dir;
		BackupStoreDirectory::Entry* e = dir.AddEntry(fnames[0], 12,
			2 /* id */, 1, BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		e->SetDependsOnObject(3);

		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);

		dir_en_check ck[] = {
			{-1, 0, 0}
		};

		TEST_THAT(check_dir(dir, ck));
	}

	{
		BackupStoreDirectory dir;
		/*
		Entry *AddEntry(const BackupStoreFilename &rName,
			box_time_t ModificationTime, int64_t ObjectID,
			int64_t SizeInBlocks, int16_t Flags,
			uint64_t AttributesHash);
		*/
		dir.AddEntry(fnames[0], 12, 2 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[1], 12, 2 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[0], 12, 3 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[0], 12, 5 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);

		/*
		typedef struct
		{
			int name;
			int64_t id;
			int flags;
		} dir_en_check;
		*/

		dir_en_check ck[] = {
			{1, 2, BackupStoreDirectory::Entry::Flags_File},
			{0, 3, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion},
			{0, 5, BackupStoreDirectory::Entry::Flags_File},
			{-1, 0, 0}
		};

		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir(dir, ck);
	}

	{
		BackupStoreDirectory dir;
		dir.AddEntry(fnames[0], 12, 2 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		dir.AddEntry(fnames[1], 12, 10 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_Dir | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		dir.AddEntry(fnames[0], 12, 3 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		dir.AddEntry(fnames[0], 12, 5 /* id */, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 2);

		dir_en_check ck[] = {
			{0, 2, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion},
			{1, 10, BackupStoreDirectory::Entry::Flags_Dir},
			{0, 3, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion},
			{0, 5, BackupStoreDirectory::Entry::Flags_File},
			{-1, 0, 0}
		};

		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir(dir, ck);
	}

	// Test dependency fixing
	{
		BackupStoreDirectory dir;
		BackupStoreDirectory::Entry *e2 = dir.AddEntry(fnames[0], 12,
			2 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e2 != 0);
		e2->SetDependsOnObject(3);
		BackupStoreDirectory::Entry *e3 = dir.AddEntry(fnames[0], 12,
			3 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e3 != 0);
		e3->SetDependsOnObject(4); e3->SetRequiredByObject(2);
		BackupStoreDirectory::Entry *e4 = dir.AddEntry(fnames[0], 12,
			4 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e4 != 0);
		e4->SetDependsOnObject(5); e4->SetRequiredByObject(3);
		BackupStoreDirectory::Entry *e5 = dir.AddEntry(fnames[0], 12,
			5 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		TEST_THAT(e5 != 0);
		e5->SetRequiredByObject(4);

		// This should all be nice and valid
		TEST_THAT(dir.CheckAndFix() == false);
		static checkdepinfoen c1[] = {{2, 3, 0}, {3, 4, 2}, {4, 5, 3}, {5, 0, 4}, {-1, 0, 0}};
		TEST_THAT(check_dir_dep(dir, c1));

		// Check that dependency forwards are restored
		e4->SetRequiredByObject(34343);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		TEST_THAT(check_dir_dep(dir, c1));

		// Check that a spurious depends older ref is undone
		e2->SetRequiredByObject(1);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		TEST_THAT(check_dir_dep(dir, c1));

		// Now delete an entry, and check it's cleaned up nicely
		dir.DeleteEntry(3);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		static checkdepinfoen c2[] = {{4, 5, 0}, {5, 0, 4}, {-1, 0, 0}};
		TEST_THAT(check_dir_dep(dir, c2));
	}

	TEARDOWN_TEST_UNIFIED();
}

int64_t fake_upload(BackupProtocolLocal& client, const std::string& file_path,
	int64_t diff_from_id, int64_t container_id = BACKUPSTORE_ROOT_DIRECTORY_ID,
	BackupStoreFilename* fn = NULL)
{
	if(fn == NULL)
	{
		fn = &fnames[0];
	}

	std::auto_ptr<IOStream> upload;
	if(diff_from_id)
	{
		std::auto_ptr<BackupProtocolSuccess> getBlockIndex(
			client.QueryGetBlockIndexByName(
				BACKUPSTORE_ROOT_DIRECTORY_ID, fnames[0]));
		std::auto_ptr<IOStream> blockIndexStream(client.ReceiveStream());
		upload = BackupStoreFile::EncodeFileDiff(
			file_path,
			container_id,
			*fn,
			diff_from_id,
			*blockIndexStream,
			IOStream::TimeOutInfinite,
			NULL, // DiffTimer implementation
			0 /* not interested in the modification time */,
			NULL // isCompletelyDifferent
			);
	}
	else
	{
		upload = BackupStoreFile::EncodeFile(
			file_path,
			container_id,
			*fn,
			NULL,
			NULL, // pLogger
			NULL // pRunStatusProvider
			);
	}

	return client.QueryStoreFile(container_id,
		1, // ModificationTime
		2, // AttributesHash
		diff_from_id, // DiffFromFileID
		*fn, // rFilename
		upload)->GetObjectID();
}

void check_initial_dir(BackupFileSystem& fs)
{
	// Check that the initial situation matches our expectations.
	BackupStoreDirectory dir;
	fs.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, dir);

	dir_en_check start_entries[] = {{-1, 0, 0}};
	check_dir(dir, start_entries);
	static checkdepinfoen start_deps[] = {{-1, 0, 0}};
	TEST_THAT(check_dir_dep(dir, start_deps));

	fs.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, dir);

	// Everything should be OK at the moment
	TEST_THAT(dir.CheckAndFix() == false);

	// Check that we've ended up with the right preconditions
	// for the tests below.
	dir_en_check before_entries[] = {
		{-1, 0, 0}
	};
	check_dir(dir, before_entries);
	static checkdepinfoen before_deps[] = {{-1, 0, 0}};
	TEST_THAT(check_dir_dep(dir, before_deps));
}

bool check_root_dir_ok(BackupFileSystem& fs, dir_en_check after_entries[],
	checkdepinfoen after_deps[])
{
	bool status_ok = true;

	// Check the store, check that the error is detected and
	// repaired, by removing x1 from the directory.
	TEST_EQUAL_OR(0, check_account_and_fix_errors(fs), status_ok = false);

	// Read the directory back in, check that it's empty
	BackupStoreDirectory dir;
	fs.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, dir);

	check_dir(dir, after_entries);
	TEST_THAT_OR(check_dir_dep(dir, after_deps), status_ok = false);

	return status_ok;
}

bool check_and_fix_root_dir(BackupFileSystem& fs, dir_en_check after_entries[],
	checkdepinfoen after_deps[])
{
	bool status_ok = true;

	// Check the store, check that the error is detected and
	// repaired.
	TEST_THAT_OR(check_account_and_fix_errors(fs) > 0, status_ok = false);
	TEST_THAT_OR(check_root_dir_ok(fs, after_entries, after_deps), status_ok = false);

	return status_ok;
}

bool compare_store_contents_with_expected(int phase, RaidAndS3TestSpecs::Specialisation* pSpec = NULL)
{
	if(pSpec != NULL)
	{
		// Release any locks, especially on the S3 cache directory
		pSpec->control().GetFileSystem().ReleaseLock();
	}

	BOX_INFO("Running testbackupstorefix.pl to check contents of store (phase " <<
		phase << ")");

	std::ostringstream cmd;
	cmd << PERL_EXECUTABLE " testfiles/testbackupstorefix.pl ";
	cmd << ((phase == 6) ? "reroot" : "check") << " ";
	if(pSpec == NULL)
	{
		cmd << "store";
	}
	else
	{
		cmd << pSpec->name();
	}
	cmd << " " << phase;

	return ::system(cmd.str().c_str()) == 0;
}

bool test_entry_pointing_to_missing_object_is_deleted(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	{
		check_initial_dir(fs);

		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly

		std::string file_path = "testfiles/TestDir1/cannes/ict/metegoguered/oats";
		int x1id = fake_upload(protocol, file_path, 0);
		protocol.QueryFinished();

		// Now break the reverse dependency by deleting x1 (the file,
		// not the directory entry)
		fs.DeleteFile(x1id);

		dir_en_check after_entries[] = {{-1, 0, 0}};
		static checkdepinfoen after_deps[] = {{-1, 0, 0}};
		TEST_THAT(check_and_fix_root_dir(fs, after_entries, after_deps));
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_entry_depending_on_missing_object_is_deleted(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	{
		check_initial_dir(fs);

		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly

		std::string file_path = "testfiles/TestDir1/cannes/ict/metegoguered/oats";
		int x1id = fake_upload(protocol, file_path, 0);

		// Make a small change to the file
		FileStream file(file_path, O_WRONLY | O_APPEND);
		const char* more = " and more oats!";
		file.Write(more, strlen(more));
		file.Close();

		int x1aid = fake_upload(protocol, file_path, x1id);
		protocol.QueryFinished();

		// Check that we've ended up with the right preconditions
		// for the tests below.
		dir_en_check before_entries[] = {
			{0, x1id, BackupStoreDirectory::Entry::Flags_File |
				BackupStoreDirectory::Entry::Flags_OldVersion},
			{0, x1aid, BackupStoreDirectory::Entry::Flags_File},
			{-1, 0, 0}
		};
		static checkdepinfoen before_deps_s3[] = {{x1id, 0, x1aid},
			{x1aid, x1id, 0}, {-1, 0, 0}};
		static checkdepinfoen before_deps_store[] = {{x1id, x1aid, 0},
			{x1aid, 0, x1id}, {-1, 0, 0}};
		TEST_THAT(check_root_dir_ok(fs, before_entries,
			(spec.name() == "s3") ? before_deps_s3 : before_deps_store));

		// Now break the reverse dependency by deleting the object that is depended upon:
		// x1aid for raid stores, x1id for S3 stores. The directory entry for the object
		// is left intact, but pointing to an object that does not exist.
		fs.DeleteFile((spec.name() == "s3") ? x1id : x1aid);

		// Check and fix the directory, and check that it's left empty, as the dependent
		// (now incomplete) object should be deleted too:
		dir_en_check after_entries[] = {{-1, 0, 0}};
		static checkdepinfoen after_deps[] = {{-1, 0, 0}};
		TEST_THAT(check_and_fix_root_dir(fs, after_entries, after_deps));
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_entry_pointing_to_crazy_object_id(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	{
		BackupStoreDirectory dir;
		fs.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, dir);

		dir.AddEntry(fnames[0], 12, 0x1234567890123456LL /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File, 2);

		fs.PutDirectory(dir);
		fs.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, dir);
		TEST_THAT(dir.FindEntryByID(0x1234567890123456LL) != 0);

		// Should just be greater than 1 really, we don't know quite
		// how good the checker is (or will become) at spotting errors!
		// But this will help us catch changes in checker behaviour,
		// so it's not a bad thing to test.
		TEST_EQUAL(2, check_account_and_fix_errors(fs));

		fs.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, dir);
		TEST_THAT(dir.FindEntryByID(0x1234567890123456LL) == 0);
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_store_info_repaired_and_random_files_removed(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised_with_bbstored(spec));

	// Delete store info, add a couple of spurious files
	if(spec.name() == "s3")
	{
		TEST_THAT(EMU_UNLINK("testfiles/store/subdir/" S3_INFO_FILE_NAME) == 0);
		{
			FileStream fs("testfiles/store/subdir/randomfile", O_CREAT | O_WRONLY);
			fs.Write("test", 4);
		}
		{
			// In release builds, we haven't created enough files to start a second
			// level directory structure yet, so we need to create it ourselves before
			// we can "populate" it:
			if(!TestDirExists("testfiles/store/subdir/00"))
			{
				TEST_THAT(mkdir("testfiles/store/subdir/00", 0755) == 0);
			}
			FileStream fs("testfiles/store/subdir/00/another", O_CREAT | O_WRONLY);
			fs.Write("test", 4);
		}
	}
	else
	{
		RaidFileWrite del(discSetNum, accountRootDir + "info");
		del.Delete();

		{
			RaidFileWrite random(discSetNum, accountRootDir + "randomfile");
			random.Open();
			random.Write("test", 4);
			random.Commit(true);
		}

		{
			// In release builds, we haven't created enough files to start a second
			// level directory structure yet, so we need to create it ourselves before
			// we can "populate" it:
			RaidFileWrite::CreateDirectory(0, accountRootDir + "01", true); // Recursive
			RaidFileWrite random(discSetNum, accountRootDir + "01/another");
			random.Open();
			random.Write("test", 4);
			random.Commit(true);
		}
	}

	// Fix it
	TEST_EQUAL(3, check_account_and_fix_errors(spec.control().GetFileSystem()));

	// Check everything is as it was
	TEST_THAT(compare_store_contents_with_expected(0, &spec));

	// Check the random file doesn't exist
	if(spec.name() == "s3")
	{
		TEST_THAT(!TestFileExists("testfiles/store/subdir/randomfile"));
		TEST_THAT(!TestFileExists("testfiles/store/subdir/00/another"));
	}
	else
	{
		TEST_THAT(!RaidFileRead::FileExists(discSetNum, accountRootDir + "01/randomfile"));
		TEST_THAT(!RaidFileRead::FileExists(discSetNum, accountRootDir + "01/00/another"));
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_entry_for_corrupted_directory(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised_with_bbstored(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	TEST_EQUAL(0, check_account_and_fix_errors(fs));

	// Check that we're starting off with the right numbers of files and blocks.
	// Otherwise the test that check the counts after breaking things will fail
	// because the numbers won't match.

	int block_multiplier = (spec.name() == "s3" ? 1 : 2);

	{
		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, client, false); // !ReadOnly
		std::auto_ptr<BackupProtocolAccountUsage2> usage = client.QueryGetAccountUsage2();
		TEST_EQUAL(usage->GetNumCurrentFiles(), 114);
		TEST_EQUAL(usage->GetNumDirectories(), 28);
		TEST_EQUAL(usage->GetBlocksUsed(), 142 * block_multiplier);
		TEST_EQUAL(usage->GetBlocksInCurrentFiles(), 114 * block_multiplier);
		TEST_EQUAL(usage->GetBlocksInDirectories(), 28 * block_multiplier);
	}

	// Check that everything is in the correct initial state:
	TEST_THAT(compare_store_contents_with_expected(0, &spec));

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Delete an entry for an object from dir, change that "
		"object to be a patch, check it's deleted");
	{
		// Wait for the server to finish housekeeping (if any) and then lock the account
		// before damaging it, to prevent housekeeping from repairing the damage in the
		// background. We could just stop the server, but on Windows that can leave the
		// account half-cleaned and always leaves a PID file lying around, which breaks
		// the rest of the test, so we do it this way on all platforms instead.
		fs.GetLock();

		// Open dir and find entry
		int64_t oats_file_id = getID("Test1/cannes/ict/metegoguered/oats");

		{
			BackupStoreDirectory dir;
			int64_t dir_id = getID("Test1/cannes/ict/metegoguered");
			fs.GetDirectory(dir_id, dir);
			TEST_THAT(dir.FindEntryByID(oats_file_id) != 0);
			dir.DeleteEntry(oats_file_id);
			fs.PutDirectory(dir);
			set_refcount(oats_file_id, 0);
		}

		// Adjust that entry
		//
		// IMPORTANT NOTE: There's a special hack in testbackupstorefix.pl to make sure that
		// the file we're modifiying has at least two blocks so we can modify it and produce a valid file
		// which will pass the verify checks.
		//
		std::string fn(get_raid_file_path(oats_file_id));
		{
			std::auto_ptr<IOStream> file = fs.GetFile(oats_file_id);
			CollectInBufferStream buf;
			file->CopyStreamTo(buf);

			// Move to header in both
			file->Seek(0, IOStream::SeekType_Absolute);
			BackupStoreFile::MoveStreamPositionToBlockIndex(*file);
			buf.Seek(file->GetPosition(), IOStream::SeekType_Absolute);

			// Read header
			struct
			{
				file_BlockIndexHeader hdr;
				file_BlockIndexEntry e[2];
			} h;
			TEST_THAT(file->Read(&h, sizeof(h)) == sizeof(h));
			file->Close();

			// Modify
			TEST_THAT(box_ntoh64(h.hdr.mOtherFileID) == 0);
			TEST_THAT(box_ntoh64(h.hdr.mNumBlocks) >= 2);
			h.hdr.mOtherFileID = box_hton64(2345); // don't worry about endianness
			h.e[0].mEncodedSize = box_hton64((box_ntoh64(h.e[0].mEncodedSize)) + (box_ntoh64(h.e[1].mEncodedSize)));
			h.e[1].mOtherBlockIndex = box_hton64(static_cast<uint64_t>(-2));

			// Write to modified file (buffer in memory)
			buf.Write(&h, sizeof(h));

			// Commit new version
			buf.SetForReading();
			std::auto_ptr<BackupFileSystem::Transaction> trans =
				fs.PutFileComplete(oats_file_id, buf, 1, true); // allow overwriting
			trans->Commit();
		}

		// Fix it
		// ERROR:   Object 0x86 is unattached, and is a patch. Deleting, cannot reliably recover.
		// ERROR:   BlocksUsed changed from 284 to 282
		// ERROR:   BlocksInCurrentFiles changed from 228 to 226
		// ERROR:   NumCurrentFiles changed from 114 to 113
		// WARNING: Reference count of object 0x44 changed from 1 to 0
		fs.ReleaseLock();

		TEST_EQUAL(5, check_account_and_fix_errors(fs));

		{
			CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, client, false); // !ReadOnly
			std::auto_ptr<BackupProtocolAccountUsage2> usage =
				client.QueryGetAccountUsage2();
			TEST_EQUAL(usage->GetNumCurrentFiles(), 113);
			TEST_EQUAL(usage->GetNumDirectories(), 28);
			TEST_EQUAL(usage->GetBlocksUsed(), 141 * block_multiplier);
			TEST_EQUAL(usage->GetBlocksInCurrentFiles(), 113 * block_multiplier);
			TEST_EQUAL(usage->GetBlocksInDirectories(), 28 * block_multiplier);
		}

		// Check
		TEST_THAT(compare_store_contents_with_expected(1, &spec));

		// Check the modified file doesn't exist
		TEST_THAT(!fs.ObjectExists(oats_file_id));
	}

	if(spec.name() == "store")
	{
		// Check that the missing RaidFiles were regenerated and
		// committed. FileExists returns NonRaid if it find a .rfw
		// file, so checking for AsRaid excludes this possibility.
		RaidFileController &rcontroller(RaidFileController::GetController());
		RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(discSetNum));
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool check_for_errors(int expected_error_count, int blocks_used, int blocks_in_current,
	int blocks_in_old, int blocks_in_deleted, int blocks_in_dirs, int num_files,
	RaidAndS3TestSpecs::Specialisation* pSpec = NULL)
{
	std::auto_ptr<BackupProtocolAccountUsage2> usage;
	bool success = true;

	// We don't know quite how good the checker is (or will become) at
	// spotting errors! But asserting an exact number will help us catch
	// changes in checker behaviour, so it's not a bad thing to test.
	if(pSpec == NULL)
	{
		TEST_EQUAL_OR(expected_error_count, check_account_and_fix_errors(),
			success = false);
		usage = BackupProtocolLocal2(0x01234567, "test",
			"backup/01234567/", 0,
			false).QueryGetAccountUsage2();
	}
	else
	{
		BackupFileSystem& fs(pSpec->control().GetFileSystem());
		TEST_EQUAL_OR(expected_error_count, check_account_and_fix_errors(fs),
			success = false);
		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, client, false); // !ReadOnly
		usage = client.QueryGetAccountUsage2();
	}

	{
		int block_multiplier = (pSpec != NULL && pSpec->name() == "s3" ? 1 : 2);
		TEST_EQUAL_OR(blocks_used * block_multiplier,
			usage->GetBlocksUsed(), success = false);
		TEST_EQUAL_OR(blocks_in_current * block_multiplier,
			usage->GetBlocksInCurrentFiles(), success = false);
		TEST_EQUAL_OR(blocks_in_old * block_multiplier,
			usage->GetBlocksInOldFiles(), success = false);
		TEST_EQUAL_OR(blocks_in_deleted * block_multiplier,
			usage->GetBlocksInDeletedFiles(), success = false);
		TEST_EQUAL_OR(blocks_in_dirs * block_multiplier,
			usage->GetBlocksInDirectories(), success = false);
		TEST_EQUAL_OR(num_files, usage->GetNumCurrentFiles(), success = false);
	}

	return success;
}

bool test_delete_directory_change_container_id_duplicate_entry_delete_objects(
	RaidAndS3TestSpecs::Specialisation& spec
)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised_with_bbstored(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	int64_t duplicatedID = 0;
	int64_t notSpuriousFileSize = 0;

	TEST_THAT(check_for_errors(0, 142, 114, 0, 0, 28, 114, &spec));

	int64_t ict_id = getID("Test1/foreomizes/stemptinevidate/ict");
	int64_t peep_id = getID("Test1/cannes/ict/peep");

	{
		BackupStoreDirectory dir;
		fs.GetDirectory(ict_id, dir);
		dir.SetContainerID(73773);
		fs.PutDirectory(dir);

		TEST_THAT(check_for_errors(1, 142, 114, 0, 0, 28, 114, &spec));

		// Duplicate the second entry
		{
			fs.GetDirectory(peep_id, dir);
			BackupStoreDirectory::Iterator i(dir);
			i.Next();
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);
			duplicatedID = en->GetObjectID();
			dir.AddEntry(*en);
			fs.PutDirectory(dir);
		}

		TEST_THAT(check_for_errors(1, 142, 114, 0, 0, 28, 114, &spec));

		// Adjust file size of first file
		{
			fs.GetDirectory(peep_id, dir);
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
			TEST_THAT(en != 0);
			notSpuriousFileSize = en->GetSizeInBlocks();
			en->SetSizeInBlocks(3473874);
			TEST_THAT(en->GetSizeInBlocks() == 3473874);
			fs.PutDirectory(dir);
		}

		TEST_THAT(check_for_errors(1, 142, 114, 0, 0, 28, 114, &spec));

		// Delete a directory. The checker should be able to reconstruct it using the
		// ContainerID of the contained files.
		int64_t ming_id = getID("Test1/pass/cacted/ming");
		fs.DeleteDirectory(ming_id);

		TEST_THAT(check_for_errors(2, 142, 114, 0, 0, 28, 114, &spec));

		// Delete a file. The checker should not be able to reconstruct it, so the number
		// of files and blocks used by current files should both drop by its size.
		int64_t scely_id = getID("Test1/cannes/ict/scely");
		fs.DeleteFile(scely_id);
		set_refcount(scely_id, 0);

		TEST_THAT(check_for_errors(6, 141, 113, 0, 0, 28, 113, &spec));
	}

	// Check everything is as it should be
	TEST_THAT(compare_store_contents_with_expected(2, &spec));

	{
		BackupStoreDirectory dir;
		fs.GetDirectory(ict_id, dir);
		TEST_THAT(dir.GetContainerID() == getID("Test1/foreomizes/stemptinevidate"));
	}

	{
		BackupStoreDirectory dir;
		fs.GetDirectory(peep_id, dir);
		BackupStoreDirectory::Iterator i(dir);

		// Count the number of entries with the ID which was duplicated
		int count = 0;
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next()) != 0)
		{
			if(en->GetObjectID() == duplicatedID)
			{
				++count;
			}
		}
		TEST_THAT(count == 1);

		// Check file size has changed
		{
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
			TEST_THAT(en != 0);
			TEST_THAT(en->GetSizeInBlocks() == notSpuriousFileSize);
		}
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

// Used by test_directory_bad_object_id_delete_empty_dir_add_reference: this directory writes itself
// with different ObjectID (0x400) inside, but still returns the original ID from GetObjectID, so
// that BackupFileSystem writes it as its normal ID (but with the wrong ID inside).
class FakeDirectory : public BackupStoreDirectory
{
	void WriteToStream(IOStream &rStream,
		int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING,
		int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING,
		bool StreamAttributes = true, bool StreamDependencyInfo = true) const
	{
		BackupStoreDirectory temp;
		// No copy constructor, so we need to write it and read again:
		CollectInBufferStream buf;
		BackupStoreDirectory::WriteToStream(buf, FlagsMustBeSet, FlagsNotToBeSet,
			StreamAttributes, StreamDependencyInfo);
		buf.SetForReading();
		temp.ReadFromStream(buf, IOStream::TimeOutInfinite);
		temp.TESTONLY_SetObjectID(0x400);
		temp.WriteToStream(rStream, FlagsMustBeSet, FlagsNotToBeSet,
			StreamAttributes, StreamDependencyInfo);
	}
};

bool test_directory_bad_object_id_delete_empty_dir_add_reference(
	RaidAndS3TestSpecs::Specialisation& spec
)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised_with_bbstored(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	TEST_THAT(check_for_errors(0, 142, 114, 0, 0, 28, 114, &spec));
	int64_t ict_id = getID("Test1/foreomizes/stemptinevidate/ict");
	int64_t cruishery_id = getID("Test1/divel/torsines/cruishery");
	int64_t copied_id;

	{
		// Set bad object ID. We have to use special tricks (FakeDirectory) to get
		// BackupFileSystem to write this directory as a different ID than its own,
		// creating the error "Directory 0x35 has a different internal object ID than
		// expected: 0x400" when read back.
		FakeDirectory fd;
		fs.GetDirectory(ict_id, fd);
		fs.PutDirectory(fd);

		// Delete dir with no members
		int64_t dir_no_members_id = getID("Test1/dir-no-members");
		fs.DeleteDirectory(dir_no_members_id);
		set_refcount(dir_no_members_id, 0);

		// Add extra reference
		BackupStoreDirectory dir;
		int64_t divel_id = getID("Test1/divel");
		fs.GetDirectory(divel_id, dir);
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
		TEST_THAT_OR(en != 0, FAIL);
		copied_id = en->GetObjectID();

		BackupStoreDirectory dir2;
		fs.GetDirectory(cruishery_id, dir2);
		dir2.AddEntry(*en);
		fs.PutDirectory(dir2);
	}

	// Fix it
	TEST_THAT(check_for_errors(14, 141, 114, 0, 0, 27, 114, &spec));

	// Check everything is as it should be
	TEST_THAT(compare_store_contents_with_expected(3, &spec));

	{
		BackupStoreDirectory dir;
		fs.GetDirectory(ict_id, dir);
		TEST_THAT(dir.GetObjectID() == ict_id);

		// Check that the second reference to copied_id was removed:
		fs.GetDirectory(cruishery_id, dir);
		BackupStoreDirectory::Entry* en = dir.FindEntryByID(copied_id);
		TEST_THAT(en == 0);
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool set_refcount_for_lost_and_found_dir(BackupProtocolCallable& protocol)
{
	std::auto_ptr<BackupProtocolSuccess> dirreply(
		protocol.QueryListDirectory(
			BACKUPSTORE_ROOT_DIRECTORY_ID,
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));

	BackupStoreDirectory dir(protocol.ReceiveStream(), 5000); // timeout in ms
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreFilenameClear lost_found_0("lost+found0");
	BackupStoreDirectory::Entry *en = i.FindMatchingClearName(lost_found_0);
	TEST_THAT_OR(en != NULL, FAIL);

	set_refcount(en->GetObjectID(), 1);
	return true;
}

bool test_orphan_files_and_directories_unrecoverably(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised_with_bbstored(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	int64_t dir1_id = getID("Test1/dir1");
	int64_t dir2_id = getID("Test1/dir1/dir2");
	fs.GetLock();
	fs.DeleteDirectory(dir1_id);
	fs.DeleteDirectory(dir2_id);
	fs.ReleaseLock();
	set_refcount(dir1_id, 0);
	set_refcount(dir2_id, 0);

	TEST_EQUAL(8, check_account_and_fix_errors(fs));

	// Fixing created a lost+found directory, which has a reference, so we should expect
	// it to exist and have a reference:
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, client, false); // !ReadOnly
	TEST_THAT(set_refcount_for_lost_and_found_dir(client));

	// Check everything is where it is predicted to be
	TEST_THAT(compare_store_contents_with_expected(4, &spec));

	TEARDOWN_TEST_SPECIALISED(spec);
}

int64_t CorruptObject(RaidAndS3TestSpecs::Specialisation& spec, const char *name, int start,
	const char *rubbish, bool is_directory)
{
	int rubbish_len = ::strlen(rubbish);
	int64_t id = getID(name);

	CollectInBufferStream buf;
	BackupFileSystem& fs(spec.control().GetFileSystem());
	fs.GetObject(id)->CopyStreamTo(buf);

	buf.Seek(start, IOStream::SeekType_Absolute);
	buf.Write(rubbish, rubbish_len);
	buf.SetForReading();

	// Can't just do fs.PutFileComplete(id, buf, 1)->Commit(), even if it allowed overwriting
	// existing objects (which it currently does not), because it also verifies the uploaded
	// stream, which we've corrupted!

	if(spec.name() == "s3")
	{
		S3BackupFileSystem& s3fs(dynamic_cast<S3BackupFileSystem &>(fs));
		FileStream out("testfiles/store" +
			(is_directory ? s3fs.GetDirectoryURI(id) : s3fs.GetFileURI(id)), O_WRONLY);
		buf.CopyStreamTo(out);
	}
	else
	{
		std::string fn(get_raid_file_path(id));
		RaidFileWrite out(discSetNum, fn);
		out.Open(true); // allow overwrite
		buf.CopyStreamTo(out);
		out.Commit(true); // convert now
	}

	return id;
}

bool test_corrupt_file_and_dir(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised_with_bbstored(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	{
		// Increase log level for locking errors to help debug random failures on AppVeyor
		LogLevelOverrideByFileGuard increase_lock_logging("", // rFileName
			BackupFileSystem::LOCKING.ToString(), Log::TRACE); // NewLevel
		increase_lock_logging.Install();

		// File. This one should be deleted.
		int64_t id = CorruptObject(spec, "Test1/foreomizes/stemptinevidate/algoughtnerge",
			33, "34i729834298349283479233472983sdfhasgs", false); // !is_directory
		BOX_INFO("Corrupted file: " << BOX_FORMAT_OBJECTID(id));
		set_refcount(id, 0);

		// Dir. This one should be recovered.
		id = CorruptObject(spec, "Test1/cannes/imulatrougge/foreomizes", 23,
			"dsf32489sdnadf897fd2hjkesdfmnbsdfcsfoisufio2iofe2hdfkjhsf",
			true); // is_directory
		BOX_INFO("Corrupted directory: " << BOX_FORMAT_OBJECTID(id));
	}

	TEST_EQUAL(11, check_account_and_fix_errors(fs));

	// Check everything is where it should be
	TEST_THAT(compare_store_contents_with_expected(5, &spec));

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_overwrite_root_directory_with_a_file(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	TEST_THAT(setup_test_backupstorefix_specialised_with_bbstored(spec));
	BackupFileSystem& fs(spec.control().GetFileSystem());

	std::auto_ptr<IOStream> file_data =
		fs.GetFile(getID("Test1/pass/shuted/brightinats/milamptimaskates"));

	if(spec.name() == "s3")
	{
		S3BackupFileSystem& s3fs(dynamic_cast<S3BackupFileSystem &>(fs));
		FileStream out("testfiles/store" +
			s3fs.GetDirectoryURI(BACKUPSTORE_ROOT_DIRECTORY_ID), O_WRONLY);
		file_data->CopyStreamTo(out);
	}
	else
	{
		RaidFileWrite out(discSetNum, get_raid_file_path(BACKUPSTORE_ROOT_DIRECTORY_ID));
		out.Open(true); // allow overwrite
		file_data->CopyStreamTo(out);
		out.Commit(true); // convert now
	}

	TEST_EQUAL(6, check_account_and_fix_errors(fs));

	// Fixing created a lost+found directory, which has a reference, so we should expect
	// it to exist and have a reference:
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, client, false); // !ReadOnly
	TEST_THAT(set_refcount_for_lost_and_found_dir(client));

	// Check everything is where it should be
	TEST_THAT(compare_store_contents_with_expected(6, &spec));

	TEARDOWN_TEST_SPECIALISED(spec);
}

int test(int argc, const char *argv[])
{
	// Enable logging timestamps to help debug race conditions on AppVeyor
	Console::SetShowTimeMicros(true);

	{
		MEMLEAKFINDER_NO_LEAKS;
		fnames[0].SetAsClearFilename("x1");
		fnames[1].SetAsClearFilename("x2");
		fnames[2].SetAsClearFilename("x3");
	}

	// Initialise the raid file controller
	RaidFileController &rcontroller = RaidFileController::GetController();
	rcontroller.Initialise("testfiles/raidfile.conf");

	// SSL library
	SSLLib::Initialise();

	// Use the setup crypto command to set up all these keys, so that the bbackupquery command can be used
	// for seeing what's going on.
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	tls_context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	std::auto_ptr<RaidAndS3TestSpecs> specs(
		new RaidAndS3TestSpecs("testfiles/bbackupd.s3.conf"));

	TEST_THAT(kill_running_daemons());
	TEST_THAT(StartSimulator());

	TEST_THAT(test_dir_fixing());

	// Run all tests that take a RaidAndS3TestSpecs::Specialisation argument twice, once with
	// each specialisation that we have (S3 and BackupStore).

	for(auto i = specs->specs().begin(); i != specs->specs().end(); i++)
	{
		TEST_THAT(test_entry_pointing_to_missing_object_is_deleted(*i));
		TEST_THAT(test_entry_depending_on_missing_object_is_deleted(*i));
		TEST_THAT(test_entry_pointing_to_crazy_object_id(*i));
		TEST_THAT(test_store_info_repaired_and_random_files_removed(*i));
		TEST_THAT(test_entry_for_corrupted_directory(*i));
		TEST_THAT(test_delete_directory_change_container_id_duplicate_entry_delete_objects(*i));
		TEST_THAT(test_directory_bad_object_id_delete_empty_dir_add_reference(*i));
		TEST_THAT(test_orphan_files_and_directories_unrecoverably(*i));
		TEST_THAT(test_corrupt_file_and_dir(*i));
		TEST_THAT(test_overwrite_root_directory_with_a_file(*i));
	}

	TEST_THAT(StopSimulator());

	return finish_test_suite();
}

