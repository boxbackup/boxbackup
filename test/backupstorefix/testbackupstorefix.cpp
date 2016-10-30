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
#include "BackupClientCryptoKeys.h"
#include "BackupProtocol.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreCheck.h"
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
#include "ServerControl.h"
#include "StoreStructure.h"
#include "StoreTestUtils.h"
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

std::string accountRootDir("backup/01234567/");
int discSetNum = 0;

std::map<std::string, int32_t> nameToID;
std::map<int32_t, bool> objectIsDir;

#define RUN_CHECK	\
	::system(BBSTOREACCOUNTS " -c testfiles/bbstored.conf check 01234567"); \
	::system(BBSTOREACCOUNTS " -c testfiles/bbstored.conf check 01234567 fix");

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
void DeleteObject(const char *name)
{
	RaidFileWrite del(discSetNum, getObjectName(getID(name)));
	del.Delete();
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

void CorruptObject(const char *name, int start, const char *rubbish)
{
	int rubbish_len = ::strlen(rubbish);
	std::string fn(getObjectName(getID(name)));
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
		TEST_EQUAL_LINE(ck->depNewer, en->GetDependsNewer(),
			"Wrong Newer dependency for " << BOX_FORMAT_OBJECTID(ck->id));
		TEST_EQUAL_LINE(ck->depOlder, en->GetDependsOlder(),
			"Wrong Older dependency for " << BOX_FORMAT_OBJECTID(ck->id));
		++ck;
	}

	TEST_THAT(en == 0);
	TEST_THAT(ck->id == -1);
}

void test_dir_fixing()
{
	// Test that entries pointing to nonexistent entries are removed
	{
		BackupStoreDirectory dir;
		BackupStoreDirectory::Entry* e = dir.AddEntry(fnames[0], 12,
			2 /* id */, 1, BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		e->SetDependsNewer(3);

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
		e2->SetDependsNewer(3);
		BackupStoreDirectory::Entry *e3 = dir.AddEntry(fnames[0], 12,
			3 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e3 != 0);
		e3->SetDependsNewer(4); e3->SetDependsOlder(2);
		BackupStoreDirectory::Entry *e4 = dir.AddEntry(fnames[0], 12,
			4 /* id */, 1,
			BackupStoreDirectory::Entry::Flags_File |
			BackupStoreDirectory::Entry::Flags_OldVersion, 2);
		TEST_THAT(e4 != 0);
		e4->SetDependsNewer(5); e4->SetDependsOlder(3);
		BackupStoreDirectory::Entry *e5 = dir.AddEntry(fnames[0], 12,
			5 /* id */, 1, BackupStoreDirectory::Entry::Flags_File, 2);
		TEST_THAT(e5 != 0);
		e5->SetDependsOlder(4);

		// This should all be nice and valid
		TEST_THAT(dir.CheckAndFix() == false);
		static checkdepinfoen c1[] = {{2, 3, 0}, {3, 4, 2}, {4, 5, 3}, {5, 0, 4}, {-1, 0, 0}};
		check_dir_dep(dir, c1);

		// Check that dependency forwards are restored
		e4->SetDependsOlder(34343);
		TEST_THAT(dir.CheckAndFix() == true);
		TEST_THAT(dir.CheckAndFix() == false);
		check_dir_dep(dir, c1);

		// Check that a spurious depends older ref is undone
		e2->SetDependsOlder(1);
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
	TEST_EQUAL(0, check_account_for_errors());

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
	TEST_THAT(check_account_for_errors() > 0);
	check_root_dir_ok(after_entries, after_deps);
}

int test(int argc, const char *argv[])
{
	{
		MEMLEAKFINDER_NO_LEAKS;
		fnames[0].SetAsClearFilename("x1");
		fnames[1].SetAsClearFilename("x2");
		fnames[2].SetAsClearFilename("x3");
	}

	// Test the backupstore directory fixing
	test_dir_fixing();

	// Initialise the raidfile controller
	RaidFileController &rcontroller = RaidFileController::GetController();
	rcontroller.Initialise("testfiles/raidfile.conf");
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	// Create an account
	TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS 
		" -c testfiles/bbstored.conf "
		"create 01234567 0 10000B 20000B") == 0);
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

	// Run the perl script to create the initial directories
	TEST_THAT_ABORTONFAIL(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl init") == 0);

	BOX_INFO("  === Test that an entry pointing to a file that doesn't "
		"exist is really deleted");

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

	BOX_INFO("  === Test that an entry pointing to another that doesn't "
		"exist is really deleted");

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

	BOX_INFO("  === Test that an entry pointing to a directory whose "
		"raidfile is corrupted doesn't crash");

	// Start the bbstored server
	TEST_THAT_OR(StartServer(), return 1);

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

		for(getline.GetLine(line, true); line != ""; getline.GetLine(line, true))
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

	// Check that we're starting off with the right numbers of files and blocks.
	// Otherwise the test that check the counts after breaking things will fail
	// because the numbers won't match.
	TEST_EQUAL(0, check_account_for_errors());
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

	BOX_INFO("  === Add a reference to a file that doesn't exist, check "
		"that it's removed");
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
		TEST_EQUAL(2, check_account_for_errors());

		std::auto_ptr<RaidFileRead> file(RaidFileRead::Open(discSetNum,
			fn));
		dir.ReadFromStream(*file, IOStream::TimeOutInfinite);
		TEST_THAT(dir.FindEntryByID(0x1234567890123456LL) == 0);
	}

	// Generate a list of all the object IDs
	TEST_THAT_ABORTONFAIL(::system(BBACKUPQUERY " -Wwarning "
		"-c testfiles/bbackupd.conf \"list -R\" quit "
		"> testfiles/initial-listing.txt") == 0);

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
		}
		::fclose(f);
	}

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Delete store info, add random file");
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
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl check 0") == 0);
	// Check the random file doesn't exist
	{
		TEST_THAT(!RaidFileRead::FileExists(discSetNum, 
			accountRootDir + "01/randomfile"));
	}

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Delete an entry for an object from dir, change that "
		"object to be a patch, check it's deleted");
	{
		// Temporarily stop the server, so it doesn't repair the refcount error. Except 
		// on win32, where hard-killing the server can leave a lockfile in place,
		// breaking the rest of the test.
#ifdef WIN32
		// Wait for the server to finish housekeeping first, by getting a lock on
		// the account.
		std::auto_ptr<BackupStoreAccountDatabase> apAccounts(
			BackupStoreAccountDatabase::Read("testfiles/accounts.txt"));
		BackupStoreAccounts acc(*apAccounts);
		NamedLock lock;
		acc.LockAccount(0x1234567, lock);
#else
		TEST_THAT(StopServer());
#endif

		// Open dir and find entry
		int64_t delID = getID("Test1/cannes/ict/metegoguered/oats");
		{
			BackupStoreDirectory dir;
			LoadDirectory("Test1/cannes/ict/metegoguered", dir);
			TEST_THAT(dir.FindEntryByID(delID) != 0);
			dir.DeleteEntry(delID);
			SaveDirectory("Test1/cannes/ict/metegoguered", dir);
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
#ifdef WIN32
		lock.ReleaseLock();
#endif
		TEST_EQUAL(5, check_account_for_errors());
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

		// Start the server again, so testbackupstorefix.pl can run bbackupquery which
		// connects to it. Except on win32, where we didn't stop it earlier.
#ifndef WIN32
		TEST_THAT(StartServer());
#endif

		// Check
		TEST_THAT(::system(PERL_EXECUTABLE
			" testfiles/testbackupstorefix.pl check 1") 
			== 0);

		// Check the modified file doesn't exist
		TEST_THAT(!RaidFileRead::FileExists(discSetNum, fn));

		// Check that the missing RaidFiles were regenerated and
		// committed. FileExists returns NonRaid if it find a .rfw
		// file, so checking for AsRaid excludes this possibility.
		RaidFileController &rcontroller(RaidFileController::GetController());
		RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(discSetNum));
	}

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Delete directory, change container ID of another, "
		"duplicate entry in dir, spurious file size, delete file");
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		dir.SetContainerID(73773);
		SaveDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
	}
	int64_t duplicatedID = 0;
	int64_t notSpuriousFileSize = 0;
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/cannes/ict/peep", dir);
		// Duplicate the second entry
		{
			BackupStoreDirectory::Iterator i(dir);
			i.Next();
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);
			duplicatedID = en->GetObjectID();
			dir.AddEntry(*en);
		}
		// Adjust file size of first file
		{
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next(BackupStoreDirectory::Entry::Flags_File);
			TEST_THAT(en != 0);
			notSpuriousFileSize = en->GetSizeInBlocks();
			en->SetSizeInBlocks(3473874);
			TEST_THAT(en->GetSizeInBlocks() == 3473874);
		}
		SaveDirectory("Test1/cannes/ict/peep", dir);
	}

	// Delete a directory. The checker should be able to reconstruct it using the
	// ContainerID of the contained files.
	DeleteObject("Test1/pass/cacted/ming");

	// Delete a file
	DeleteObject("Test1/cannes/ict/scely");

	// We don't know quite how good the checker is (or will become) at
	// spotting errors! But asserting an exact number will help us catch
	// changes in checker behaviour, so it's not a bad thing to test.

	// The 12 errors that we currently expect are:
	// ERROR:   Directory ID 0xb references object 0x3e which does not exist.
	// ERROR:   Removing directory entry 0x3e from directory 0xb
	// ERROR:   Directory ID 0xc had invalid entries, fixed
	// ERROR:   Directory ID 0xc has wrong size for object 0x40
	// ERROR:   Directory ID 0x17 has wrong container ID.
	// ERROR:   Object 0x51 is unattached.
	// ERROR:   Object 0x52 is unattached.
	// ERROR:   BlocksUsed changed from 282 to 278
	// ERROR:   BlocksInCurrentFiles changed from 226 to 220
	// ERROR:   BlocksInDirectories changed from 56 to 54
	// ERROR:   NumFiles changed from 113 to 110
	// WARNING: Reference count of object 0x3e changed from 1 to 0

	TEST_EQUAL(12, check_account_for_errors());

	{
		std::auto_ptr<BackupProtocolAccountUsage2> usage =
			BackupProtocolLocal2(0x01234567, "test",
				"backup/01234567/", 0,
				false).QueryGetAccountUsage2();
		TEST_EQUAL(usage->GetBlocksUsed(), 278);
		TEST_EQUAL(usage->GetBlocksInCurrentFiles(), 220);
		TEST_EQUAL(usage->GetBlocksInDirectories(), 54);
		TEST_EQUAL(usage->GetNumCurrentFiles(), 110);
	}

	// Check everything is as it should be
	TEST_THAT(::system(PERL_EXECUTABLE
		" testfiles/testbackupstorefix.pl check 2") == 0);
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

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Modify the obj ID of dir, delete dir with no members, "
		"add extra reference to a file");
	// Set bad object ID
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		dir.TESTONLY_SetObjectID(73773);
		SaveDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
	}
	// Delete dir with no members
	DeleteObject("Test1/dir-no-members");
	// Add extra reference
	{
		BackupStoreDirectory dir;
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

	// Check everything is as it should be
	TEST_THAT(::system(PERL_EXECUTABLE
		" testfiles/testbackupstorefix.pl check 3") == 0);
	{
		BackupStoreDirectory dir;
		LoadDirectory("Test1/foreomizes/stemptinevidate/ict", dir);
		TEST_THAT(dir.GetObjectID() == getID("Test1/foreomizes/stemptinevidate/ict"));
	}

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Orphan files and dirs without being recoverable");
	DeleteObject("Test1/dir1");
	DeleteObject("Test1/dir1/dir2");

	// Fix it
	RUN_CHECK

	// Check everything is where it is predicted to be
	TEST_THAT(::system(PERL_EXECUTABLE
		" testfiles/testbackupstorefix.pl check 4") == 0);

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Corrupt file and dir");
	// File
	CorruptObject("Test1/foreomizes/stemptinevidate/algoughtnerge",
		33, "34i729834298349283479233472983sdfhasgs");
	// Dir
	CorruptObject("Test1/cannes/imulatrougge/foreomizes",23, 
		"dsf32489sdnadf897fd2hjkesdfmnbsdfcsfoisufio2iofe2hdfkjhsf");
	// Fix it
	RUN_CHECK
	// Check everything is where it should be
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl check 5") == 0);

	// ------------------------------------------------------------------------------------------------
	BOX_INFO("  === Overwrite root with a file");
	{
		std::auto_ptr<RaidFileRead> r(RaidFileRead::Open(discSetNum, getObjectName(getID("Test1/pass/shuted/brightinats/milamptimaskates"))));
		RaidFileWrite w(discSetNum, getObjectName(1 /* root */));
		w.Open(true /* allow overwrite */);
		r->CopyStreamTo(w);
		w.Commit(true /* convert now */);
	}
	// Fix it
	RUN_CHECK
	// Check everything is where it should be
	TEST_THAT(::system(PERL_EXECUTABLE 
		" testfiles/testbackupstorefix.pl reroot 6") == 0);


	// ---------------------------------------------------------
	// Stop server
	TEST_THAT(KillServer(bbstored_pid));

	#ifdef WIN32
		TEST_THAT(EMU_UNLINK("testfiles/bbstored.pid") == 0);
	#else
		TestRemoteProcessMemLeaks("bbstored.memleaks");
	#endif

	return 0;
}

