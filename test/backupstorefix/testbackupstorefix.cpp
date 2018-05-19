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

#include "Test.h"
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
	if (ServerIsAlive(bbstored_pid))
	{
		TEST_THAT_OR(StopServer(), FAIL);
	}

	ExpectedRefCounts.resize(BACKUPSTORE_ROOT_DIRECTORY_ID + 1);
	set_refcount(BACKUPSTORE_ROOT_DIRECTORY_ID, 1);

	TEST_THAT_OR(create_account(10000, 20000), FAIL);

	// Run the perl script to create the initial directories
	TEST_THAT_OR(::system(PERL_EXECUTABLE
		" testfiles/testbackupstorefix.pl init") == 0, FAIL);

	return true;
}

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP_TEST_UNIFIED() \
	SETUP(); \
	TEST_THAT(setup_test_backupstorefix_unified());

std::map<std::string, int32_t> nameToID;
std::map<int32_t, bool> objectIsDir;

bool list_uploaded_files()
{
	// Generate a list of all the object IDs
	TEST_THAT_OR(::system(BBACKUPQUERY " -Wwarning "
		"-c testfiles/bbackupd.conf \"list -R\" quit "
		"> testfiles/initial-listing.txt") == 0, FAIL);

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
			TEST_THAT(::sscanf(line, "%x %s %s", &id,
				flags, name) == 3);
			bool isDir = (::strcmp(flags, "-d---") == 0);
			//TRACE3("%x,%d,%s\n", id, isDir, name);
			MEMLEAKFINDER_NO_LEAKS;
			nameToID[std::string(name)] = id;
			objectIsDir[id] = isDir;
			set_refcount(id, 1);
		}
		::fclose(f);
	}

	TEST_EQUAL_OR(141, nameToID.size(), FAIL);

	return true;
}

bool upload_test_files()
{
	TEST_THAT_OR(StartClient("testfiles/bbackupd-backupstorefix.conf"), FAIL);
	sync_and_wait();
	TEST_THAT_OR(StopClient(), FAIL);
	TEST_THAT_OR(list_uploaded_files(), FAIL);
	return true;
}

bool setup_test_backupstorefix_unified_with_bbstored()
{
	TEST_THAT_OR(setup_test_backupstorefix_unified(), FAIL);
	TEST_THAT_OR(StartServer(), FAIL);
	TEST_THAT_OR(upload_test_files(), FAIL);
	return true;
}

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP_TEST_UNIFIED_WITH_BBSTORED() \
	SETUP(); \
	TEST_THAT(setup_test_backupstorefix_unified_with_bbstored());

//! Checks account for errors and shuts down daemons at end of every test.
bool teardown_test_backupstorefix_unified()
{
	bool status = true;

	if (FileExists("testfiles/0_0/backup/01234567/info.rf"))
	{
		TEST_THAT_OR(check_reference_counts(), status = false);
		TEST_EQUAL_OR(0, check_account_and_fix_errors(), status = false);
	}

	return status;
}

#define TEARDOWN_TEST_UNIFIED() \
	if (ServerIsAlive(bbstored_pid)) \
		StopServer(); \
	TEST_THAT(teardown_test_backupstorefix_unified()); \
	TEARDOWN();

// TODO: delete this code once it's unused:
std::string accountRootDir("backup/01234567/");
int discSetNum = 0;

TLSContext tls_context;

#define RUN_CHECK \
	BOX_INFO("Running bbstoreaccounts to check and then repair the account"); \
	::system(BBSTOREACCOUNTS " -c testfiles/bbstored.conf -Utbbstoreaccounts " \
		"-L/FileSystem/Locking=trace check 01234567"); \
	::system(BBSTOREACCOUNTS " -c testfiles/bbstored.conf -Utbbstoreaccounts " \
		"-L/FileSystem/Locking=trace check 01234567 fix");

// Get ID of an object given a filename
int32_t getID(const char *name)
{
	std::map<std::string, int32_t>::iterator i(nameToID.find(std::string(name)));
	TEST_THAT(i != nameToID.end());
	if(i == nameToID.end()) return -1;

	return i->second;
}

// Get the RAID filename of an object
std::string getObjectName(int32_t id)
{
	std::string fn;
	StoreStructure::MakeObjectFilename(id, accountRootDir, discSetNum, fn, false);
	return fn;
}

// Delete an object
int64_t DeleteObject(const char *name)
{
	int64_t id = getID(name);
	RaidFileWrite del(discSetNum, getObjectName(id));
	del.Delete();
	return id;
}

// Load a directory
void LoadDirectory(const char *name, BackupStoreDirectory &rDir)
{
	std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum, getObjectName(getID(name))));
	rDir.ReadFromStream(*file, IOStream::TimeOutInfinite);
}
// Save a directory back again
void SaveDirectory(const char *name, const BackupStoreDirectory &rDir)
{
	RaidFileWrite d(discSetNum, getObjectName(getID(name)));
	d.Open(true /* allow overwrite */);
	rDir.WriteToStream(d);
	d.Commit(true /* write now! */);
}

int64_t CorruptObject(const char *name, int start, const char *rubbish)
{
	int rubbish_len = ::strlen(rubbish);
	int64_t id = getID(name);
	std::string fn(getObjectName(id));
	std::auto_ptr<RaidFileRead> r(RaidFileRead::Open(discSetNum, fn));
	RaidFileWrite w(discSetNum, fn);
	w.Open(true /* allow overwrite */);
	// Copy beginning
	char buf[2048];
	r->Read(buf, start, IOStream::TimeOutInfinite);
	w.Write(buf, start);
	// Write rubbish
	r->Seek(rubbish_len, IOStream::SeekType_Relative);
	w.Write(rubbish, rubbish_len);
	// Copy rest of file
	r->CopyStreamTo(w);
	r->Close();
	// Commit
	w.Commit(true /* convert now */);
	return id;
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
	int64_t id, depNewer, depOlder;
} checkdepinfoen;

void check_dir_dep(BackupStoreDirectory &dir, checkdepinfoen *ck)
{
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en;

	while((en = i.Next()) != 0)
	{
		TEST_THAT(ck->id != -1);
		if(ck->id == -1)
		{
			break;
		}
		TEST_EQUAL_LINE(ck->id, en->GetObjectID(), "Wrong object ID "
			"for " << BOX_FORMAT_OBJECTID(ck->id));
		TEST_EQUAL_LINE(ck->depNewer, en->GetDependsOnObject(),
			"Wrong Newer dependency for " << BOX_FORMAT_OBJECTID(ck->id));
		TEST_EQUAL_LINE(ck->depOlder, en->GetRequiredByObject(),
			"Wrong Older dependency for " << BOX_FORMAT_OBJECTID(ck->id));
		++ck;
	}

	TEST_THAT(en == 0);
	TEST_THAT(ck->id == -1);
}

bool test_dir_fixing()
{
	SETUP_TEST_UNIFIED();

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
		check_dir_dep(dir, c1);

		// Check that dependency forwards are restored
		e4->SetRequiredByObject(34343);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir_dep(dir, c1);

		// Check that a spurious depends older ref is undone
		e2->SetRequiredByObject(1);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir_dep(dir, c1);

		// Now delete an entry, and check it's cleaned up nicely
		dir.DeleteEntry(3);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		static checkdepinfoen c2[] = {{4, 5, 0}, {5, 0, 4}, {-1, 0, 0}};
		check_dir_dep(dir, c2);
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

void read_bb_dir(int64_t objectId, BackupStoreDirectory& dir)
{
	std::string fn;
	StoreStructure::MakeObjectFilename(1 /* root */, accountRootDir,
		discSetNum, fn, true /* EnsureDirectoryExists */);

	std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum,
		fn));
	dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
}

void login_client_and_check_empty(BackupProtocolCallable& client)
{
	// Check that the initial situation matches our expectations.
	BackupStoreDirectory dir;
	read_bb_dir(1 /* root */, dir);

	dir_en_check start_entries[] = {{-1, 0, 0}};
	check_dir(dir, start_entries);
	static checkdepinfoen start_deps[] = {{-1, 0, 0}};
	check_dir_dep(dir, start_deps);

	read_bb_dir(1 /* root */, dir);

	// Everything should be OK at the moment
	TEST_THAT(dir.CheckAndFix() == false);

	// Check that we've ended up with the right preconditions
	// for the tests below.
	dir_en_check before_entries[] = {
		{-1, 0, 0}
	};
	check_dir(dir, before_entries);
	static checkdepinfoen before_deps[] = {{-1, 0, 0}};
	check_dir_dep(dir, before_deps);
}

void check_root_dir_ok(dir_en_check after_entries[],
	checkdepinfoen after_deps[])
{
	// Check the store, check that the error is detected and
	// repaired, by removing x1 from the directory.
	TEST_EQUAL(0, check_account_and_fix_errors());

	// Read the directory back in, check that it's empty
	BackupStoreDirectory dir;
	read_bb_dir(1 /* root */, dir);

	check_dir(dir, after_entries);
	check_dir_dep(dir, after_deps);
}

void check_and_fix_root_dir(dir_en_check after_entries[],
	checkdepinfoen after_deps[])
{
	// Check the store, check that the error is detected and
	// repaired.
	TEST_THAT(check_account_and_fix_errors() > 0);
	check_root_dir_ok(after_entries, after_deps);
}

bool compare_store_contents_with_expected(int phase)
{
	BOX_INFO("Running testbackupstorefix.pl to check contents of store (phase " <<
		phase << ")");
	std::ostringstream cmd;
	cmd << PERL_EXECUTABLE " testfiles/testbackupstorefix.pl ";
	cmd << ((phase == 6) ? "reroot" : "check") << " " << phase;
	return ::system(cmd.str().c_str()) == 0;
}

bool test_entry_pointing_to_missing_object_is_deleted()
{
	SETUP_TEST_UNIFIED();

	{
		BackupProtocolLocal2 client(0x01234567, "test", accountRootDir,
			discSetNum, false);
		login_client_and_check_empty(client);

		std::string file_path = "testfiles/TestDir1/cannes/ict/metegoguered/oats";
		int x1id = fake_upload(client, file_path, 0);
		client.QueryFinished();

		// Now break the reverse dependency by deleting x1 (the file,
		// not the directory entry)
		std::string x1FileName;
		StoreStructure::MakeObjectFilename(x1id, accountRootDir, discSetNum,
			x1FileName, true /* EnsureDirectoryExists */);
		RaidFileWrite deleteX1(discSetNum, x1FileName);
		deleteX1.Delete();

		dir_en_check after_entries[] = {{-1, 0, 0}};
		static checkdepinfoen after_deps[] = {{-1, 0, 0}};
		check_and_fix_root_dir(after_entries, after_deps);
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_entry_depending_on_missing_object_is_deleted()
{
	SETUP_TEST_UNIFIED();

	{
		BackupProtocolLocal2 client(0x01234567, "test", accountRootDir,
			discSetNum, false);
		login_client_and_check_empty(client);

		std::string file_path = "testfiles/TestDir1/cannes/ict/metegoguered/oats";
		int x1id = fake_upload(client, file_path, 0);

		// Make a small change to the file
		FileStream fs(file_path, O_WRONLY | O_APPEND);
		const char* more = " and more oats!";
		fs.Write(more, strlen(more));
		fs.Close();

		int x1aid = fake_upload(client, file_path, x1id);
		client.QueryFinished();

		// Check that we've ended up with the right preconditions
		// for the tests below.
		dir_en_check before_entries[] = {
			{0, x1id, BackupStoreDirectory::Entry::Flags_File |
				BackupStoreDirectory::Entry::Flags_OldVersion},
			{0, x1aid, BackupStoreDirectory::Entry::Flags_File},
			{-1, 0, 0}
		};
		static checkdepinfoen before_deps[] = {{x1id, x1aid, 0},
			{x1aid, 0, x1id}, {-1, 0, 0}};
		check_root_dir_ok(before_entries, before_deps);

		// Now break the reverse dependency by deleting x1a (the file,
		// not the directory entry)
		std::string x1aFileName;
		StoreStructure::MakeObjectFilename(x1aid, accountRootDir, discSetNum,
			x1aFileName, true /* EnsureDirectoryExists */);
		RaidFileWrite deleteX1a(discSetNum, x1aFileName);
		deleteX1a.Delete();

		// Check and fix the directory, and check that it's left empty
		dir_en_check after_entries[] = {{-1, 0, 0}};
		static checkdepinfoen after_deps[] = {{-1, 0, 0}};
		check_and_fix_root_dir(after_entries, after_deps);
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_entry_pointing_to_crazy_object_id()
{
	SETUP_TEST_UNIFIED();

	{
		BackupStoreDirectory dir;
		read_bb_dir(1 /* root */, dir);

		dir.AddEntry(fnames[0], 12, 0x1234567890123456LL /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File, 2);

		std::string fn;
		StoreStructure::MakeObjectFilename(1 /* root */, accountRootDir,
			discSetNum, fn, true /* EnsureDirectoryExists */);

		RaidFileWrite d(discSetNum, fn);
		d.Open(true /* allow overwrite */);
		dir.WriteToStream(d);
		d.Commit(true /* write now! */);

		read_bb_dir(1 /* root */, dir);
		TEST_THAT(dir.FindEntryByID(0x1234567890123456LL) != 0);

		// Should just be greater than 1 really, we don't know quite
		// how good the checker is (or will become) at spotting errors!
		// But this will help us catch changes in checker behaviour,
		// so it's not a bad thing to test.
		TEST_EQUAL(2, check_account_and_fix_errors());

		std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum,
			fn));
		dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
		TEST_THAT(dir.FindEntryByID(0x1234567890123456LL) == 0);
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_store_info_repaired_and_random_files_removed()
{
	SETUP_TEST_UNIFIED_WITH_BBSTORED();

	{
		// Delete store info
		RaidFileWrite del(discSetNum, accountRootDir + "info");
		del.Delete();
	}

	{
		// Add a spurious file
		RaidFileWrite random(discSetNum,
			accountRootDir + "randomfile");
		random.Open();
		random.Write("test", 4);
		random.Commit(true);
	}

	// Fix it
	RUN_CHECK

	// Check everything is as it was
	TEST_THAT(compare_store_contents_with_expected(0));

	// Check the random file doesn't exist
	{
		TEST_THAT(!RaidFileRead::FileExists(discSetNum,
			accountRootDir + "01/randomfile"));
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_entry_for_corrupted_directory()
{
	SETUP_TEST_UNIFIED();

	// Start the bbstored server. Enable logging to help debug if the store is unexpectedly
	// locked when we try to check or query it (race conditions):
	std::string daemon_args(bbstored_args_overridden ? bbstored_args :
		"-kT -Winfo -tbbstored -L/FileSystem/Locking=trace");
	TEST_THAT_OR(StartServer(daemon_args), return 1);

	// Instead of starting a client, read the file listing file created by
	// testbackupstorefix.pl and upload them in the correct order, so that the object
	// IDs will not vary depending on the order in which readdir() returns entries.
	{
		FileStream listing("testfiles/file-listing.txt", O_RDONLY);
		IOStreamGetLine getline(listing);
		std::map<std::string, int64_t> dirname_to_id;
		std::string line;
		BackupProtocolLocal2 client(0x01234567, "test", accountRootDir,
			discSetNum, false);

		for(line = getline.GetLine(true); line != ""; line = getline.GetLine(true))
		{
			std::string full_path = line;
			ASSERT(StartsWith("testfiles/TestDir1/", full_path));

			bool is_dir = (full_path[full_path.size() - 1] == '/');
			if(is_dir)
			{
				full_path = full_path.substr(0, full_path.size() - 1);
			}

			std::string::size_type last_slash = full_path.rfind('/');
			int64_t container_id;
			std::string filename;

			if(full_path == "testfiles/TestDir1")
			{
				container_id = BACKUPSTORE_ROOT_DIRECTORY_ID;
				filename = "Test1";
			}
			else
			{
				std::string containing_dir =
					full_path.substr(0, last_slash);
				container_id = dirname_to_id[containing_dir];
				filename = full_path.substr(last_slash + 1);
			}

			BackupStoreFilenameClear fn(filename);
			if(is_dir)
			{
				std::auto_ptr<IOStream> attr_stream(
					new CollectInBufferStream);
				((CollectInBufferStream &)
					*attr_stream).SetForReading();

				dirname_to_id[full_path] = client.QueryCreateDirectory(
					container_id, 0, // AttributesModTime
					fn, attr_stream)->GetObjectID();
			}
			else
			{
				fake_upload(client, line, 0, container_id, &fn);
			}
		}
	}

	TEST_THAT(list_uploaded_files());

	TEST_EQUAL(0, check_account_and_fix_errors());

	// Check that we're starting off with the right numbers of files and blocks.
	// Otherwise the test that check the counts after breaking things will fail
	// because the numbers won't match.

	{
		std::auto_ptr<BackupProtocolAccountUsage2> usage =
			BackupProtocolLocal2(0x01234567, "test",
				"backup/01234567/", 0,
				false).QueryGetAccountUsage2();
		TEST_EQUAL(usage->GetNumCurrentFiles(), 114);
		TEST_EQUAL(usage->GetNumDirectories(), 28);
		TEST_EQUAL(usage->GetBlocksUsed(), 284);
		TEST_EQUAL(usage->GetBlocksInCurrentFiles(), 228);
		TEST_EQUAL(usage->GetBlocksInDirectories(), 56);
	}

	// ------------------------------------------------------------------------------------------------

	RUN_CHECK

	// Check everything is as it was
	TEST_THAT(compare_store_contents_with_expected(0));

	RaidBackupFileSystem fs(0x01234567, accountRootDir, 0); // discSet

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
		int64_t delID = getID("Test1/cannes/ict/metegoguered/oats");
		{
			BackupStoreDirectory dir;
			LoadDirectory("Test1/cannes/ict/metegoguered", dir);
			TEST_THAT(dir.FindEntryByID(delID) != 0);
			dir.DeleteEntry(delID);
			SaveDirectory("Test1/cannes/ict/metegoguered", dir);
			set_refcount(delID, 0);
		}

		// Adjust that entry
		//
		// IMPORTANT NOTE: There's a special hack in testbackupstorefix.pl to make sure that
		// the file we're modifiying has at least two blocks so we can modify it and produce a valid file
		// which will pass the verify checks.
		//
		std::string fn(getObjectName(delID));
		{
			std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum, fn));
			RaidFileWrite f(discSetNum, fn);
			f.Open(true /* allow overwrite */);
			// Make a copy of the original
			file->CopyStreamTo(f);
			// Move to header in both
			file->Seek(0, IOStream::SeekType_Absolute);
			BackupStoreFile::MoveStreamPositionToBlockIndex(*file);
			f.Seek(file->GetPosition(), IOStream::SeekType_Absolute);
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
			// Write to modified file
			f.Write(&h, sizeof(h));
			// Commit new version
			f.Commit(true /* write now! */);
		}

		// Fix it
		// ERROR:   Object 0x44 is unattached.
		// ERROR:   BlocksUsed changed from 284 to 282
		// ERROR:   BlocksInCurrentFiles changed from 228 to 226
		// ERROR:   NumCurrentFiles changed from 114 to 113
		// WARNING: Reference count of object 0x44 changed from 1 to 0
		fs.ReleaseLock();

		TEST_EQUAL(5, check_account_and_fix_errors());
		{
			std::auto_ptr<BackupProtocolAccountUsage2> usage =
				BackupProtocolLocal2(0x01234567, "test",
					"backup/01234567/", 0,
					false).QueryGetAccountUsage2();
			TEST_EQUAL(usage->GetNumCurrentFiles(), 113);
			TEST_EQUAL(usage->GetNumDirectories(), 28);
			TEST_EQUAL(usage->GetBlocksUsed(), 282);
			TEST_EQUAL(usage->GetBlocksInCurrentFiles(), 226);
			TEST_EQUAL(usage->GetBlocksInDirectories(), 56);
		}

		// Check
		TEST_THAT(compare_store_contents_with_expected(1));

		// Check the modified file doesn't exist
		TEST_THAT(!RaidFileRead::FileExists(discSetNum, fn));

		// Check that the missing RaidFiles were regenerated and
		// committed. FileExists returns NonRaid if it find a .rfw
		// file, so checking for AsRaid excludes this possibility.
		RaidFileController &rcontroller(RaidFileController::GetController());
		RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(discSetNum));
	}

	TEARDOWN_TEST_UNIFIED();
}

bool check_for_errors(int expected_error_count, int blocks_used, int blocks_in_current,
	int blocks_in_old, int blocks_in_deleted, int blocks_in_dirs, int num_files)
{
	bool success = true;

	// We don't know quite how good the checker is (or will become) at
	// spotting errors! But asserting an exact number will help us catch
	// changes in checker behaviour, so it's not a bad thing to test.
	TEST_EQUAL_OR(expected_error_count, check_account_and_fix_errors(), success = false);

	{
		std::auto_ptr<BackupProtocolAccountUsage2> usage =
			BackupProtocolLocal2(0x01234567, "test",
				"backup/01234567/", 0,
				false).QueryGetAccountUsage2();
		TEST_EQUAL_OR(blocks_used, usage->GetBlocksUsed(), success = false);
		TEST_EQUAL_OR(blocks_in_current, usage->GetBlocksInCurrentFiles(), success = false);
		TEST_EQUAL_OR(blocks_in_old, usage->GetBlocksInOldFiles(), success = false);
		TEST_EQUAL_OR(blocks_in_deleted, usage->GetBlocksInDeletedFiles(), success = false);
		TEST_EQUAL_OR(blocks_in_dirs, usage->GetBlocksInDirectories(), success = false);
		TEST_EQUAL_OR(num_files, usage->GetNumCurrentFiles(), success = false);
	}

	return success;
}

bool test_delete_directory_change_container_id_duplicate_entry_delete_objects()
{
	SETUP_TEST_UNIFIED_WITH_BBSTORED();

	int64_t duplicatedID = 0;
	int64_t notSpuriousFileSize = 0;

	TEST_THAT(check_for_errors(0, 284, 228, 0, 0, 56, 114));

	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		dir.SetContainerID(73773);
		SaveDirectory("Test1/foreomizes/stemptinevidate/ict", dir);

		TEST_THAT(check_for_errors(1, 284, 228, 0, 0, 56, 114));

		// Duplicate the second entry
		{
			LoadDirectory("Test1/cannes/ict/peep", dir);
			BackupStoreDirectory::Iterator i(dir);
			i.Next();
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);
			duplicatedID = en->GetObjectID();
			dir.AddEntry(*en);
			SaveDirectory("Test1/cannes/ict/peep", dir);
		}

		TEST_THAT(check_for_errors(1, 284, 228, 0, 0, 56, 114));

		// Adjust file size of first file
		{
			LoadDirectory("Test1/cannes/ict/peep", dir);
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
			TEST_THAT(en != 0);
			notSpuriousFileSize = en->GetSizeInBlocks();
			en->SetSizeInBlocks(3473874);
			TEST_THAT(en->GetSizeInBlocks() == 3473874);
			SaveDirectory("Test1/cannes/ict/peep", dir);
		}

		TEST_THAT(check_for_errors(1, 284, 228, 0, 0, 56, 114));

		// Delete a directory. The checker should be able to reconstruct it using the
		// ContainerID of the contained files.
		DeleteObject("Test1/pass/cacted/ming");

		TEST_THAT(check_for_errors(2, 284, 228, 0, 0, 56, 114));

		// Delete a file. The checker should not be able to reconstruct it, so the number
		// of files and blocks used by current files should both drop by its size.
		int64_t id = DeleteObject("Test1/cannes/ict/scely");
		set_refcount(id, 0);

		TEST_THAT(check_for_errors(6, 282, 226, 0, 0, 56, 113));
	}

	// Check everything is as it should be
	TEST_THAT(compare_store_contents_with_expected(2));

	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		TEST_THAT(dir.GetContainerID() == getID("Test1/foreomizes/stemptinevidate"));
	}

	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/cannes/ict/peep", dir);
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

	TEARDOWN_TEST_UNIFIED();
}

bool test_directory_bad_object_id_delete_empty_dir_add_reference()
{
	SETUP_TEST_UNIFIED_WITH_BBSTORED();

	TEST_THAT(check_for_errors(0, 284, 228, 0, 0, 56, 114));

	{
		// Set bad object ID
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		dir.TESTONLY_SetObjectID(73773);
		SaveDirectory("Test1/foreomizes/stemptinevidate/ict", dir);

		// Delete dir with no members
		int64_t id = DeleteObject("Test1/dir-no-members");
		set_refcount(id, 0);

		// Add extra reference
		LoadDirectory("Test1/divel", dir);
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
		TEST_THAT(en != 0);
		BackupStoreDirectory dir2;
		LoadDirectory("Test1/divel/torsines/cruishery", dir2);
		dir2.AddEntry(*en);
		SaveDirectory("Test1/divel/torsines/cruishery", dir2);
	}

	// Fix it
	RUN_CHECK

	TEST_THAT(check_for_errors(0, 282, 228, 0, 0, 54, 114));

	// Check everything is as it should be
	TEST_THAT(compare_store_contents_with_expected(3));

	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		TEST_THAT(dir.GetObjectID() == getID("Test1/foreomizes/stemptinevidate/ict"));
	}

	TEARDOWN_TEST_UNIFIED();
}

bool set_refcount_for_lost_and_found_dir()
{
	BackupProtocolLocal2 protocol(0x01234567, "test", accountRootDir,
		discSetNum, false);
	std::auto_ptr<BackupProtocolSuccess> dirreply(protocol.QueryListDirectory(
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

bool test_orphan_files_and_directories_unrecoverably()
{
	SETUP_TEST_UNIFIED_WITH_BBSTORED();

	set_refcount(DeleteObject("Test1/dir1"), 0);
	set_refcount(DeleteObject("Test1/dir1/dir2"), 0);

	// Fix it
	RUN_CHECK

	// Fixing created a lost+found directory, which has a reference, so we should expect
	// it to exist and have a reference:
	TEST_THAT(set_refcount_for_lost_and_found_dir());

	// Check everything is where it is predicted to be
	TEST_THAT(compare_store_contents_with_expected(4));

	TEARDOWN_TEST_UNIFIED();
}

bool test_corrupt_file_and_dir()
{
	SETUP_TEST_UNIFIED_WITH_BBSTORED();

	{
		// Increase log level for locking errors to help debug random failures on AppVeyor
		LogLevelOverrideByFileGuard increase_lock_logging("", // rFileName
			BackupFileSystem::LOCKING.ToString(), Log::TRACE); // NewLevel
		increase_lock_logging.Install();

		// File. This one should be deleted.
		int64_t id = CorruptObject("Test1/foreomizes/stemptinevidate/algoughtnerge", 33,
			"34i729834298349283479233472983sdfhasgs");
		set_refcount(id, 0);
		// Dir. This one should be recovered.
		CorruptObject("Test1/cannes/imulatrougge/foreomizes", 23,
			"dsf32489sdnadf897fd2hjkesdfmnbsdfcsfoisufio2iofe2hdfkjhsf");
	}

	// Fix it
	RUN_CHECK

	// Check everything is where it should be
	TEST_THAT(compare_store_contents_with_expected(5));

	TEARDOWN_TEST_UNIFIED();
}

bool test_overwrite_root_directory_with_a_file()
{
	SETUP_TEST_UNIFIED_WITH_BBSTORED();

	{
		std::auto_ptr<RaidFileRead> r(RaidFileRead::Open(discSetNum, getObjectName(getID("Test1/pass/shuted/brightinats/milamptimaskates"))));
		RaidFileWrite w(discSetNum, getObjectName(1 /* root */));
		w.Open(true /* allow overwrite */);
		r->CopyStreamTo(w);
		w.Commit(true /* convert now */);
	}

	// Fix it
	RUN_CHECK

	// Fixing created a lost+found directory, which has a reference, so we should expect
	// it to exist and have a reference:
	TEST_THAT(set_refcount_for_lost_and_found_dir());

	// Check everything is where it should be
	TEST_THAT(compare_store_contents_with_expected(6));

	TEARDOWN_TEST_UNIFIED();
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

	/*
	std::auto_ptr<Configuration> s3config = load_config_file(
		DEFAULT_BBACKUPD_CONFIG_FILE, BackupDaemonConfigVerify);
	// Use an auto_ptr so we can release it, and thus the lock, before stopping the
	// daemon on which locking relies:
	std::auto_ptr<S3BackupAccountControl> ap_s3control(
		new S3BackupAccountControl(*s3config));
	*/

	std::auto_ptr<Configuration> storeconfig = load_config_file(
		DEFAULT_BBSTORED_CONFIG_FILE, BackupConfigFileVerify);
	BackupStoreAccountControl storecontrol(*storeconfig, 0x01234567);

	TEST_THAT(kill_running_daemons());

	TEST_THAT(test_dir_fixing());
	TEST_THAT(test_entry_pointing_to_missing_object_is_deleted())
	TEST_THAT(test_entry_depending_on_missing_object_is_deleted())
	TEST_THAT(test_entry_pointing_to_crazy_object_id())
	TEST_THAT(test_store_info_repaired_and_random_files_removed())
	TEST_THAT(test_entry_for_corrupted_directory())
	TEST_THAT(test_delete_directory_change_container_id_duplicate_entry_delete_objects())
	TEST_THAT(test_directory_bad_object_id_delete_empty_dir_add_reference())
	TEST_THAT(test_orphan_files_and_directories_unrecoverably())
	TEST_THAT(test_corrupt_file_and_dir())
	TEST_THAT(test_overwrite_root_directory_with_a_file())

	/*
	TEST_THAT(StartSimulator());

	typedef std::map<std::string, BackupAccountControl*> test_specialisation;
	test_specialisation specialisations;
	specialisations["s3"] = ap_s3control.get();
	specialisations["store"] = &storecontrol;

#define RUN_SPECIALISED_TEST(name, pControl, function) \
	TEST_THAT(function(name, *(pControl)));

	// Run all tests that take a BackupAccountControl argument twice, once with an
	// S3BackupAccountControl and once with a BackupStoreAccountControl.

	for(test_specialisation::iterator i = specialisations.begin();
		i != specialisations.end(); i++)
	{
		// RUN_SPECIALISED_TEST(i->first, i->second, test_bbstoreaccounts_create);
	}

	// Release lock before shutting down the simulator:
	ap_s3control.reset();
	TEST_THAT(StopSimulator());
	*/

	return finish_test_suite();
}

