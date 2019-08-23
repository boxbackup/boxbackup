// --------------------------------------------------------------------------
//
// File
//		Name:    testbackupstore.cpp
//		Purpose: Test backup store server
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_PROCESS_H
#	include <process.h> // for getpid() on Windows
#endif

#include "Archive.h"
#include "BackupAccountControl.h"
#include "BackupClientCryptoKeys.h"
#include "BackupClientFileAttributes.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupProtocol.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFilenameClear.h"
#include "BackupStoreFileEncodeStream.h"
#include "BackupStoreInfo.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreRefCountDatabase.h"
#include "BoxPortsAndFiles.h"
#include "ClientTestUtils.h"
#include "CollectInBufferStream.h"
#include "Configuration.h"
#include "FileStream.h"
#include "HousekeepStoreAccount.h"
#include "MemBlockStream.h"
#include "NamedLock.h" // for BOX_LOCK_TYPE_F_SETLK
#include "RaidFileController.h"
#include "RaidFileException.h"
#include "RaidFileRead.h"
#include "RaidFileWrite.h"
#include "S3Simulator.h"
#include "SSLLib.h"
#include "ServerControl.h"
#include "SimpleDBClient.h"
#include "Socket.h"
#include "SocketStreamTLS.h"
#include "StoreTestUtils.h"
#include "TLSContext.h"
#include "Test.h"
#include "ZeroStream.h"

#include "MemLeakFindOn.h"

#define ENCFILE_SIZE	2765

// Make some test attributes
#define ATTR1_SIZE	245
#define ATTR2_SIZE	23
#define ATTR3_SIZE	122

#define SHORT_TIMEOUT 5000

int attr1[ATTR1_SIZE];
int attr2[ATTR2_SIZE];
int attr3[ATTR3_SIZE];

typedef struct
{
	BackupStoreFilenameClear fn;
	box_time_t mod;
	int64_t id;
	int64_t size;
	int16_t flags;
	box_time_t attrmod;
} dirtest;

static dirtest ens[] =
{
	{BackupStoreFilenameClear(), 324324, 3432, 324, BackupStoreDirectory::Entry::Flags_File, 458763243422LL},
	{BackupStoreFilenameClear(), 3432, 32443245645LL, 78, BackupStoreDirectory::Entry::Flags_Dir, 3248972347LL},
	{BackupStoreFilenameClear(), 544435, 234234, 23324, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_Deleted, 2348974782LL},
	{BackupStoreFilenameClear(), 234, 235436, 6523, BackupStoreDirectory::Entry::Flags_File, 32458923175634LL},
	{BackupStoreFilenameClear(), 0x3242343532144LL, 8978979789LL, 21345, BackupStoreDirectory::Entry::Flags_File, 329483243432LL},
	{BackupStoreFilenameClear(), 324265765734LL, 12312312321LL, 324987324329874LL, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_Deleted, 32489747234LL},
	{BackupStoreFilenameClear(), 3452134, 7868578768LL, 324243, BackupStoreDirectory::Entry::Flags_Dir, 34786457432LL},
	{BackupStoreFilenameClear(), 43543543, 324234, 21432, BackupStoreDirectory::Entry::Flags_Dir | BackupStoreDirectory::Entry::Flags_Deleted, 3489723478327LL},
	{BackupStoreFilenameClear(), 325654765874324LL, 4353543, 1, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 32489734789237LL},
	{BackupStoreFilenameClear(), 32144325, 436547657, 9, BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion, 234897347234LL}
};
static const char *ens_filenames[] = {"obj1ertewt", "obj2", "obj3", "obj4dfedfg43", "obj5", "obj6dfgs", "obj7", "obj8xcvbcx", "obj9", "obj10fgjhfg"};
#define DIR_NUM 10
#define DIR_DIRS 3
#define DIR_FILES 7
#define DIR_OLD 2
#define DIR_DELETED 3

typedef struct
{
	const char *fnextra;
	BackupStoreFilenameClear name;
	int seed;
	int size;
	box_time_t mod_time;
	int64_t allocated_objid;
	bool should_be_old_version;
	bool delete_file;
} uploadtest;

#define TEST_FILE_FOR_PATCHING	"testfiles/test2"
// a few bytes will be inserted at this point:
#define TEST_FILE_FOR_PATCHING_PATCH_AT	((64*1024)-128)
#define TEST_FILE_FOR_PATCHING_SIZE ((128*1024)+2564)
#define UPLOAD_PATCH_EN	2

uploadtest uploads[] =
{
	{"0", BackupStoreFilenameClear(), 324, 455, 0, 0, false, false},
	{"1", BackupStoreFilenameClear(), 3232432, 2674, 0, 0, true, false},			// old ver
	{"2", BackupStoreFilenameClear(), 234, TEST_FILE_FOR_PATCHING_SIZE, 0, 0, false, false},
	{"3", BackupStoreFilenameClear(), 324324, 6763, 0, 0, false, false},
	{"4", BackupStoreFilenameClear(), 23456, 124, 0, 0, true, false},			// old ver
	{"5", BackupStoreFilenameClear(), 675745, 1, 0, 0, false, false},			// will upload new attrs for this one!
	{"6", BackupStoreFilenameClear(), 345213, 0, 0, 0, false, false},
	{"7", BackupStoreFilenameClear(), 12313, 3246, 0, 0, true, true},		// old ver, will get deleted
	{"8", BackupStoreFilenameClear(), 457, 3434, 0, 0, false, false},			// overwrites
	{"9", BackupStoreFilenameClear(), 12315, 446, 0, 0, false, false},
	{"a", BackupStoreFilenameClear(), 3476, 2466, 0, 0, false, false},
	{"b", BackupStoreFilenameClear(), 124334, 4562, 0, 0, false, false},
	{"c", BackupStoreFilenameClear(), 45778, 234, 0, 0, false, false},		// overwrites
	{"d", BackupStoreFilenameClear(), 2423425, 435, 0, 0, false, true}		// overwrites, will be deleted
};
static const char *uploads_filenames[] = {"49587fds", "cvhjhj324", "sdfcscs324", "dsfdsvsdc3214", "XXsfdsdf2342", "dsfdsc232",
	"sfdsdce2345", "YYstfbdtrdf76", "cvhjhj324", "fbfd098.ycy", "dfs98732hj", "svd987kjsad", "XXsfdsdf2342", "YYstfbdtrdf76"};
#define UPLOAD_NUM	14
#define UPLOAD_LATEST_FILES	12
// file we'll upload some new attributes for
#define UPLOAD_ATTRS_EN		5
#define UPLOAD_DELETE_EN	13
// file which will be moved (as well as it's old version)
#define UPLOAD_FILE_TO_MOVE	8

#define UNLINK_IF_EXISTS(filename) \
	if (FileExists(filename)) { TEST_THAT(EMU_UNLINK(filename) == 0); }

// Nice random data for testing written files
class R250 {
public:
	// Set up internal state table with 32-bit random numbers.
	// The bizarre bit-twiddling is because rand() returns 16 bits of which
	// the bottom bit is always zero!  Hence, I use only some of the bits.
	// You might want to do something better than this....

	R250(int seed) : posn1(0), posn2(103)
	{
		// populate the state and incr tables
		srand(seed);

		for (int i = 0; i != stateLen; ++i)	{
			state[i] = ((rand() >> 2) << 19) ^ ((rand() >> 2) << 11) ^ (rand() >> 2);
			incrTable[i] = i == stateLen - 1 ? 0 : i + 1;
		}

		// stir up the numbers to ensure they're random

		for (int j = 0; j != stateLen * 4; ++j)
			(void) next();
	}

	// Returns the next random number.  Xor together two elements separated
	// by 103 mod 250, replacing the first element with the result.  Then
	// increment the two indices mod 250.
	inline int next()
	{
		int ret = (state[posn1] ^= state[posn2]);	// xor and replace element

		posn1 = incrTable[posn1];		// increment indices using lookup table
		posn2 = incrTable[posn2];

		return ret;
	}
private:
	enum { stateLen = 250 };	// length of the state table
	int state[stateLen];		// holds the random number state
	int incrTable[stateLen];	// lookup table: maps i to (i+1) % stateLen
	int posn1, posn2;			// indices into the state table
};


int SkipEntries(int e, int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet)
{
	if(e >= DIR_NUM) return e;

	bool skip = false;
	do
	{
		skip = false;

		if(FlagsMustBeSet != BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING)
		{
			if((ens[e].flags & FlagsMustBeSet) != FlagsMustBeSet)
			{
				skip = true;
			}
		}
		if((ens[e].flags & FlagsNotToBeSet) != 0)
		{
			skip = true;
		}

		if(skip)
		{
			++e;
		}
	} while(skip && e < DIR_NUM);

	return e;
}

void CheckEntries(BackupStoreDirectory &rDir, int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet)
{
	int e = 0;

	BackupStoreDirectory::Iterator i(rDir);
	BackupStoreDirectory::Entry *en = 0;
	while((en = i.Next()) != 0)
	{
		TEST_THAT(e < DIR_NUM);

		// Skip to entry in the ens array which matches
		e = SkipEntries(e, FlagsMustBeSet, FlagsNotToBeSet);

		// Does it match?
		TEST_THAT(en->GetName() == ens[e].fn && en->GetModificationTime() == ens[e].mod && en->GetObjectID() == ens[e].id && en->GetFlags() == ens[e].flags && en->GetSizeInBlocks() == ens[e].size);

		// next
		++e;
	}

	// Got them all?
	TEST_THAT(en == 0);
	TEST_THAT(DIR_NUM == SkipEntries(e, FlagsMustBeSet, FlagsNotToBeSet));
}

std::auto_ptr<RaidFileRead> get_raid_file(int64_t ObjectID)
{
	return RaidFileRead::Open(0, get_raid_file_path(ObjectID));
}

int get_disc_usage_in_blocks(bool IsDirectory, int64_t ObjectID,
	RaidAndS3TestSpecs::Specialisation& spec)
{
	if(spec.name() == "s3")
	{
		std::string local_path = get_s3_file_path(ObjectID, IsDirectory, spec);
		int size = TestGetFileSize(local_path);
		TEST_LINE(size != -1, "File does not exist: " << local_path);
		S3BackupFileSystem& fs(dynamic_cast<S3BackupFileSystem&>(spec.control().GetFileSystem()));
		return fs.GetSizeInBlocks(size);
	}
	else
	{
		return get_raid_file(ObjectID)->GetDiscUsageInBlocks();
	}
}

std::auto_ptr<IOStream> get_object_stream(bool IsDirectory, int64_t ObjectID,
	RaidAndS3TestSpecs::Specialisation& spec)
{
	if(spec.name() == "s3")
	{
		std::string local_path = get_s3_file_path(ObjectID, IsDirectory, spec);
		return std::auto_ptr<IOStream>(new FileStream(local_path));
	}
	else
	{
		return std::auto_ptr<IOStream>(get_raid_file(ObjectID).release());
	}
}

bool run_bbstoreaccounts_specialised(const std::string& specialisation_name,
	const std::string& command, const std::string& common_args,
	const std::string& s3_args = "", const std::string& store_args = "")
{
	if(specialisation_name == "s3")
	{
		// Note: this is very similar to test_s3store/test_bbstoreaccounts_commands
		TEST_THAT_OR(::system((BBSTOREACCOUNTS " -3 -c " DEFAULT_BBACKUPD_CONFIG_FILE " "
			"-Wwarning " + command + " " + common_args + " " +
			s3_args).c_str()) == 0, FAIL);
	}
	else
	{
		TEST_THAT_OR(::system((BBSTOREACCOUNTS " -c testfiles/bbstored.conf "
			"-Wwarning " + command + " 01234567 " + common_args + " " +
			store_args).c_str()) == 0, FAIL);
	}

	TEST_THAT_OR(TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks"), FAIL);

	return true;
}

bool test_filename_encoding()
{
	SETUP_TEST_UNIFIED();

	// test some basics -- encoding and decoding filenames
	{
		// Make some filenames in various ways
		BackupStoreFilenameClear fn1;
		fn1.SetClearFilename(std::string("filenameXYZ"));
		BackupStoreFilenameClear fn2(std::string("filenameXYZ"));
		BackupStoreFilenameClear fn3(fn1);
		TEST_THAT(fn1 == fn2);
		TEST_THAT(fn1 == fn3);

		// Check that it's been encrypted
		std::string name(fn2.GetEncodedFilename());
		TEST_THAT(name.find("name") == name.npos);

		// Bung it in a stream, get it out in a Clear filename
		{
			CollectInBufferStream stream;
			fn1.WriteToStream(stream);
			stream.SetForReading();
			BackupStoreFilenameClear fn4;
			fn4.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(fn4.GetClearFilename() == "filenameXYZ");
			TEST_THAT(fn4 == fn1);
		}

		// Bung it in a stream, get it out in a server non-Clear filename (two of them into the same var)
		{
			BackupStoreFilenameClear fno("pinglet dksfnsf jksjdf ");
			CollectInBufferStream stream;
			fn1.WriteToStream(stream);
			fno.WriteToStream(stream);
			stream.SetForReading();
			BackupStoreFilename fn5;
			fn5.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(fn5 == fn1);
			fn5.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(fn5 == fno);
		}

		// Same again with clear strings
		{
			BackupStoreFilenameClear fno("pinglet dksfnsf jksjdf ");
			CollectInBufferStream stream;
			fn1.WriteToStream(stream);
			fno.WriteToStream(stream);
			stream.SetForReading();
			BackupStoreFilenameClear fn5;
			fn5.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(fn5.GetClearFilename() == "filenameXYZ");
			fn5.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(fn5.GetClearFilename() == "pinglet dksfnsf jksjdf ");
		}

		// Test a very big filename
		{
			const char *fnr = "01234567890123456789012345678901234567890123456789"
				"01234567890123456789012345678901234567890123456789"
				"01234567890123456789012345678901234567890123456789"
				"01234567890123456789012345678901234567890123456789"
				"01234567890123456789012345678901234567890123456789"
				"01234567890123456789012345678901234567890123456789"
				"01234567890123456789012345678901234567890123456789"
				"01234567890123456789012345678901234567890123456789";
			BackupStoreFilenameClear fnLong(fnr);
			CollectInBufferStream stream;
			fnLong.WriteToStream(stream);
			stream.SetForReading();
			BackupStoreFilenameClear fn9;
			fn9.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(fn9.GetClearFilename() == fnr);
			TEST_THAT(fn9 == fnLong);
		}

		// Test a filename which went wrong once
		{
			BackupStoreFilenameClear dodgy("content-negotiation.html");
		}
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_backupstore_directory()
{
	SETUP_TEST_UNIFIED();

	{
		// Now play with directories

		// Fill in...
		BackupStoreDirectory dir1(12, 98);
		for(int e = 0; e < DIR_NUM; ++e)
		{
			dir1.AddEntry(ens[e].fn, ens[e].mod, ens[e].id, ens[e].size, ens[e].flags, ens[e].attrmod);
		}
		// Got the right number
		TEST_THAT(dir1.GetNumberOfEntries() == DIR_NUM);

		// Stick it into a stream and get it out again
		{
			CollectInBufferStream stream;
			dir1.WriteToStream(stream);
			stream.SetForReading();
			BackupStoreDirectory dir2(stream);
			TEST_THAT(dir2.GetNumberOfEntries() == DIR_NUM);
			TEST_THAT(dir2.GetObjectID() == 12);
			TEST_THAT(dir2.GetContainerID() == 98);
			CheckEntries(dir2, BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING, BackupStoreDirectory::Entry::Flags_EXCLUDE_NOTHING);
		}

		// Then do selective writes and reads
		{
			CollectInBufferStream stream;
			dir1.WriteToStream(stream, BackupStoreDirectory::Entry::Flags_File);
			stream.SetForReading();
			BackupStoreDirectory dir2(stream);
			TEST_THAT(dir2.GetNumberOfEntries() == DIR_FILES);
			CheckEntries(dir2, BackupStoreDirectory::Entry::Flags_File, BackupStoreDirectory::Entry::Flags_EXCLUDE_NOTHING);
		}
		{
			CollectInBufferStream stream;
			dir1.WriteToStream(stream, BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING, BackupStoreDirectory::Entry::Flags_File);
			stream.SetForReading();
			BackupStoreDirectory dir2(stream);
			TEST_THAT(dir2.GetNumberOfEntries() == DIR_DIRS);
			CheckEntries(dir2, BackupStoreDirectory::Entry::Flags_Dir, BackupStoreDirectory::Entry::Flags_EXCLUDE_NOTHING);
		}
		{
			CollectInBufferStream stream;
			dir1.WriteToStream(stream, BackupStoreDirectory::Entry::Flags_File, BackupStoreDirectory::Entry::Flags_OldVersion);
			stream.SetForReading();
			BackupStoreDirectory dir2(stream);
			TEST_THAT(dir2.GetNumberOfEntries() == DIR_FILES - DIR_OLD);
			CheckEntries(dir2, BackupStoreDirectory::Entry::Flags_File, BackupStoreDirectory::Entry::Flags_OldVersion);
		}

		// Finally test deleting items
		{
			dir1.DeleteEntry(12312312321LL);
			// Verify
			TEST_THAT(dir1.GetNumberOfEntries() == DIR_NUM - 1);
			CollectInBufferStream stream;
			dir1.WriteToStream(stream, BackupStoreDirectory::Entry::Flags_File);
			stream.SetForReading();
			BackupStoreDirectory dir2(stream);
			TEST_THAT(dir2.GetNumberOfEntries() == DIR_FILES - 1);
		}

		// Check attributes
		{
			int attrI[4] = {1, 2, 3, 4};
			StreamableMemBlock attr(attrI, sizeof(attrI));
			BackupStoreDirectory d1(16, 546);
			d1.SetAttributes(attr, 56234987324232LL);
			TEST_THAT(d1.GetAttributes() == attr);
			TEST_THAT(d1.GetAttributesModTime() == 56234987324232LL);
			CollectInBufferStream stream;
			d1.WriteToStream(stream);
			stream.SetForReading();
			BackupStoreDirectory d2(stream);
			TEST_THAT(d2.GetAttributes() == attr);
			TEST_THAT(d2.GetAttributesModTime() == 56234987324232LL);
		}
	}

	TEARDOWN_TEST_UNIFIED();
}

void write_test_file(int t)
{
	std::string filename("testfiles/test");
	filename += uploads[t].fnextra;
	BOX_TRACE("Writing " << filename);

	FileStream write(filename.c_str(), O_WRONLY | O_CREAT);

	R250 r(uploads[t].seed);

	unsigned char *data = (unsigned char*)malloc(uploads[t].size);
	for(int l = 0; l < uploads[t].size; ++l)
	{
		data[l] = r.next() & 0xff;
	}
	write.Write(data, uploads[t].size);

	free(data);
}

void test_test_file(int t, IOStream &rStream)
{
	// Decode to a file
	BackupStoreFile::DecodeFile(rStream, "testfiles/test_download", SHORT_TIMEOUT);

	// Compare...
	FileStream in("testfiles/test_download");
	TEST_THAT(in.BytesLeftToRead() == uploads[t].size);

	R250 r(uploads[t].seed);

	unsigned char *data = (unsigned char*)malloc(uploads[t].size);
	TEST_THAT(in.ReadFullBuffer(data, uploads[t].size, 0 /* not interested in bytes read if this fails */));

	for(int l = 0; l < uploads[t].size; ++l)
	{
		TEST_THAT(data[l] == (r.next() & 0xff));
	}

	free(data);
	in.Close();
	TEST_THAT(EMU_UNLINK("testfiles/test_download") == 0);
}

void assert_everything_deleted(BackupProtocolCallable &protocol, int64_t DirID)
{
	BOX_TRACE("Test for del: " << BOX_FORMAT_OBJECTID(DirID));

	// Command
	std::auto_ptr<BackupProtocolSuccess> dirreply(protocol.QueryListDirectory(
			DirID,
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
	// Stream
	BackupStoreDirectory dir(protocol.ReceiveStream(), SHORT_TIMEOUT);
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	int files = 0;
	int dirs = 0;
	while((en = i.Next()) != 0)
	{
		if(en->GetFlags() & BackupProtocolListDirectory::Flags_Dir)
		{
			dirs++;
			// Recurse
			assert_everything_deleted(protocol, en->GetObjectID());
		}
		else
		{
			files++;
		}

		// Check it's deleted
		TEST_THAT(en->IsDeleted());
	}

	// Check there were the right number of files and directories
	TEST_THAT(files == 3);
	TEST_THAT(dirs == 0 || dirs == 2);
}

void create_file_in_dir(std::string name, std::string source, int64_t parentId,
	BackupProtocolCallable &protocol, BackupStoreRefCountDatabase* pRefCount)
{
	BackupStoreFilenameClear name_encoded("file_One");
	std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile(
		source.c_str(), parentId, name_encoded));
	std::auto_ptr<BackupProtocolSuccess> stored(
		protocol.QueryStoreFile(
			parentId,
			0x123456789abcdefLL,		/* modification time */
			0x7362383249872dfLL,		/* attr hash */
			0,				/* diff from ID */
			name_encoded,
			upload));
	int64_t objectId = stored->GetObjectID();
	if(pRefCount)
	{
		TEST_EQUAL(objectId, pRefCount->GetLastObjectIDUsed());
		TEST_EQUAL(1, pRefCount->GetRefCount(objectId))
	}
	set_refcount(objectId, 1);
}

const box_time_t FAKE_MODIFICATION_TIME = 0xfeedfacedeadbeefLL;
const box_time_t FAKE_ATTR_MODIFICATION_TIME = 0xdeadbeefcafebabeLL;

int64_t create_test_data_subdirs(BackupProtocolCallable &protocol,
	int64_t indir, const char *name, int depth,
	BackupStoreRefCountDatabase* pRefCount)
{
	// Create a directory
	int64_t subdirid = 0;
	BackupStoreFilenameClear dirname(name);
	{
		// Create with dummy attributes
		int attrS = 0;
		std::auto_ptr<IOStream> attr(new MemBlockStream(&attrS, sizeof(attrS)));
		std::auto_ptr<BackupProtocolSuccess> dirCreate(protocol.QueryCreateDirectory(
			indir, FAKE_ATTR_MODIFICATION_TIME, dirname, attr));
		subdirid = dirCreate->GetObjectID();
	}

	BOX_TRACE("Creating subdirs, depth = " << depth << ", dirid = " <<
		BOX_FORMAT_OBJECTID(subdirid));

	if(pRefCount)
	{
		TEST_EQUAL(subdirid, pRefCount->GetLastObjectIDUsed());
		TEST_EQUAL(1, pRefCount->GetRefCount(subdirid))
	}

	set_refcount(subdirid, 1);

	// Put more directories in it, if we haven't gone down too far
	if(depth > 0)
	{
		create_test_data_subdirs(protocol, subdirid, "dir_One",
			depth - 1, pRefCount);
		create_test_data_subdirs(protocol, subdirid, "dir_Two",
			depth - 1, pRefCount);
	}

	// Stick some files in it
	create_file_in_dir("file_One", "testfiles/test1", subdirid, protocol,
		pRefCount);
	create_file_in_dir("file_Two", "testfiles/test1", subdirid, protocol,
		pRefCount);
	create_file_in_dir("file_Three", "testfiles/test1", subdirid, protocol,
		pRefCount);
	return subdirid;
}

void check_dir_after_uploads(BackupProtocolCallable &protocol,
	const StreamableMemBlock &Attributes)
{
	// Command
	std::auto_ptr<BackupProtocolSuccess> dirreply(protocol.QueryListDirectory(
			BACKUPSTORE_ROOT_DIRECTORY_ID,
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
	TEST_THAT(dirreply->GetObjectID() == BACKUPSTORE_ROOT_DIRECTORY_ID);
	// Stream
	BackupStoreDirectory dir(protocol.ReceiveStream(), SHORT_TIMEOUT);
	TEST_EQUAL(UPLOAD_NUM, dir.GetNumberOfEntries());
	TEST_THAT(!dir.HasAttributes());

	// Check them!
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en;

	for(int t = 0; t < UPLOAD_NUM; ++t)
	{
		en = i.Next();
		TEST_THAT(en != 0);
		if (en == 0) continue;
		TEST_LINE(uploads[t].name == en->GetName(),
			"uploaded file " << t << " name");
		BackupStoreFilenameClear clear(en->GetName());
		TEST_EQUAL_LINE(uploads[t].name.GetClearFilename(),
			clear.GetClearFilename(),
			"uploaded file " << t << " name");
		TEST_EQUAL_LINE(uploads[t].allocated_objid, en->GetObjectID(),
			"uploaded file " << t << " ID");
		TEST_EQUAL_LINE(uploads[t].mod_time, en->GetModificationTime(),
			"uploaded file " << t << " modtime");
		int correct_flags = BackupProtocolListDirectory::Flags_File;
		if(uploads[t].should_be_old_version) correct_flags |= BackupProtocolListDirectory::Flags_OldVersion;
		if(uploads[t].delete_file) correct_flags |= BackupProtocolListDirectory::Flags_Deleted;
		TEST_EQUAL_LINE(correct_flags, en->GetFlags(),
			"uploaded file " << t << " flags");
		if(t == UPLOAD_ATTRS_EN)
		{
			TEST_THAT(en->HasAttributes());
			TEST_THAT(en->GetAttributesHash() == 32498749832475LL);
			TEST_THAT(en->GetAttributes() == Attributes);
		}
		else
		{
			// No attributes on this one
			TEST_THAT(!en->HasAttributes());
		}
	}
	en = i.Next();
	TEST_THAT(en == 0);
}

typedef struct
{
	int objectsNotDel;
	int deleted;
	int old;
} recursive_count_objects_results;

void recursive_count_objects_r(BackupProtocolCallable &protocol, int64_t in_directory_id,
	recursive_count_objects_results &results)
{
	// Command
	std::auto_ptr<BackupProtocolSuccess> dirreply(protocol.QueryListDirectory(
			in_directory_id,
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
	// Stream
	BackupStoreDirectory dir(protocol.ReceiveStream(), SHORT_TIMEOUT);

	// Check them!
	BackupStoreDirectory::Iterator i(dir);
	// Discard first
	BackupStoreDirectory::Entry *en = 0;

	while((en = i.Next()) != 0)
	{
		if((en->GetFlags() & (BackupStoreDirectory::Entry::Flags_Deleted | BackupStoreDirectory::Entry::Flags_OldVersion)) == 0) results.objectsNotDel++;
		if(en->GetFlags() & BackupStoreDirectory::Entry::Flags_Deleted) results.deleted++;
		if(en->GetFlags() & BackupStoreDirectory::Entry::Flags_OldVersion) results.old++;

		if(en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir)
		{
			recursive_count_objects_r(protocol, en->GetObjectID(), results);
		}
	}
}

TLSContext tls_context;

void recursive_count_objects(BackupProtocolCallable &protocol, int64_t in_directory_id,
	recursive_count_objects_results &results)
{
	recursive_count_objects_r(protocol, in_directory_id, results);
}

bool check_block_index(const char *encoded_file, IOStream &rBlockIndex)
{
	// Open file, and move to the right position
	FileStream enc(encoded_file);
	BackupStoreFile::MoveStreamPositionToBlockIndex(enc);

	bool same = true;
	int pos = 0;

	// Now compare the two...
	while(enc.StreamDataLeft())
	{
		char buffer1[2048];
		char buffer2[2048];
		int s = enc.Read(buffer1, sizeof(buffer1), SHORT_TIMEOUT);
		int s2 = rBlockIndex.Read(buffer2, s, SHORT_TIMEOUT);
		if(s2 != s)
		{
			TEST_FAIL_WITH_MESSAGE("Short read from rBlockIndex at position " << pos <<
				": expected " << s << " bytes but read " << s2);
			same = false;
			break;
		}
		if(::memcmp(buffer1, buffer2, s) != 0)
		{
			TEST_FAIL_WITH_MESSAGE("Blocks differ at " << pos << "-" << (pos+s) <<
				" bytes");
			same = false;
			break;
		}
		pos += s;
	}

	if(rBlockIndex.StreamDataLeft())
	{
		TEST_FAIL_WITH_MESSAGE("Unexpected extra data found in rBlockIndex at position " <<
			pos);
		same = false;

		// Absorb all this excess data so protocol is in the first state
		char buffer[2048];
		while(rBlockIndex.StreamDataLeft())
		{
			rBlockIndex.Read(buffer, sizeof(buffer), SHORT_TIMEOUT);
		}
	}

	return same;
}

bool check_files_same(const char *f1, const char *f2)
{
	// Open file, and move to the right position
	FileStream f1s(f1);
	FileStream f2s(f2);

	bool same = true;

	// Now compare the two...
	while(f1s.StreamDataLeft())
	{
		char buffer1[2048];
		char buffer2[2048];
		int s = f1s.Read(buffer1, sizeof(buffer1));
		if(f2s.Read(buffer2, s) != s)
		{
			same = false;
			break;
		}
		if(::memcmp(buffer1, buffer2, s) != 0)
		{
			same = false;
			break;
		}
	}

	if(f2s.StreamDataLeft())
	{
		same = false;
	}

	return same;
}

int64_t create_directory(BackupProtocolCallable& protocol,
	int64_t parent_dir_id = BACKUPSTORE_ROOT_DIRECTORY_ID);
int64_t create_file(BackupProtocolCallable& protocol, int64_t subdirid,
	const std::string& remote_filename = "");

bool run_housekeeping_and_check_account(BackupProtocolLocal2& protocol)
{
	protocol.QueryFinished();
	bool check_account_status;
	TEST_THAT(check_account_status = run_housekeeping_and_check_account());
	bool check_refcount_status;
	TEST_THAT(check_refcount_status = check_reference_counts());
	protocol.Reopen();
	return check_account_status & check_refcount_status;
}

bool test_refcount_db_versions()
{
	SETUP_TEST_UNIFIED();

	// Old version
	std::auto_ptr<BackupStoreRefCountDatabase> db = BackupStoreRefCountDatabase::Create(
		"testfiles/refcount.db.1", 0x123456, false, // !reuse_existing_file
		BackupStoreRefCountDatabase::Version_1);
	TEST_CHECK_THROWS(db->GetClientStoreMarker(), CommonException, NotSupported);
	TEST_CHECK_THROWS(db->SetClientStoreMarker(10), CommonException, NotSupported);
	db->Commit();
	db.reset();

	{
		FileStream file1("testfiles/refcount.db.1", O_RDONLY | O_BINARY);
		file1.Seek(0, IOStream::SeekType_End);
		TEST_EQUAL(12, file1.GetPosition());
		file1.Seek(0, IOStream::SeekType_Absolute);
		uint32_t dword;
		Archive arc(file1, IOStream::TimeOutInfinite);
		arc.ReadExact(dword); TEST_EQUAL(dword, REFCOUNT_MAGIC_VALUE);
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x123456); // account number
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x1); // refcount of object 1 (root dir)

		db = BackupStoreRefCountDatabase::Load(file1.GetFileName(), 0x123456,
			false); // !ReadOnly
		db->AddReference(3);
		db->AddReference(3);
		db->AddReference(3);
		db.reset();

		file1.Seek(0, IOStream::SeekType_End);
		TEST_EQUAL(20, file1.GetPosition());
		file1.Seek(0, IOStream::SeekType_Absolute);
		arc.ReadExact(dword); TEST_EQUAL(dword, REFCOUNT_MAGIC_VALUE);
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x123456); // account number
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x1); // refcount of object 1 (root dir)
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x0); // refcount of object 2
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x3); // refcount of object 3

		db = BackupStoreRefCountDatabase::Load(file1.GetFileName(), 0x123456,
			true); // ReadOnly
		TEST_EQUAL(3, db->GetRefCount(3));
		db.reset();
	}

	// New version
	db = BackupStoreRefCountDatabase::Create("testfiles/refcount.db.2", 0x123456,
		false, // !reuse_existing_file
		BackupStoreRefCountDatabase::Version_2);
	TEST_EQUAL(0, db->GetClientStoreMarker());
	db->Commit();
	db.reset();

	{
		FileStream file1("testfiles/refcount.db.2", O_RDONLY | O_BINARY);
		file1.Seek(0, IOStream::SeekType_End);
		TEST_EQUAL(20, file1.GetPosition());
		file1.Seek(0, IOStream::SeekType_Absolute);
		uint32_t dword;
		Archive arc(file1, IOStream::TimeOutInfinite);
		arc.ReadExact(dword); TEST_EQUAL(dword, REFCOUNT_MAGIC_VALUE_2);
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x123456); // account number
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x0); // client store marker (1/2)
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x0); // client store marker (2/2)
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x1); // refcount of object 1 (root dir)

		db = BackupStoreRefCountDatabase::Load(file1.GetFileName(), 0x123456,
			false); // !ReadOnly
		db->AddReference(3);
		db->AddReference(3);
		db->AddReference(3);
		db->SetClientStoreMarker(0x123456789LL);
		db.reset();

		file1.Seek(0, IOStream::SeekType_End);
		TEST_EQUAL(28, file1.GetPosition());
		file1.Seek(0, IOStream::SeekType_Absolute);
		arc.ReadExact(dword); TEST_EQUAL(dword, REFCOUNT_MAGIC_VALUE_2);
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x123456); // account number
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x1); // client store marker (1/2)
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x23456789); // client store marker (2/2)
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x1); // refcount of object 1 (root dir)
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x0); // refcount of object 2
		arc.ReadExact(dword); TEST_EQUAL(dword, 0x3); // refcount of object 3

		db = BackupStoreRefCountDatabase::Load(file1.GetFileName(), 0x123456,
			true); // ReadOnly
		TEST_EQUAL(3, db->GetRefCount(3));
		TEST_EQUAL(0x123456789LL, db->GetClientStoreMarker());
		db.reset();
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_temporary_refcount_db_is_independent()
{
	SETUP_TEST_UNIFIED();

	std::auto_ptr<BackupStoreAccountDatabase> apAccounts(
		BackupStoreAccountDatabase::Read("testfiles/accounts.txt"));
	std::auto_ptr<BackupStoreRefCountDatabase> temp(
		BackupStoreRefCountDatabase::Create(
			apAccounts->GetEntry(0x1234567)));
	std::auto_ptr<BackupStoreRefCountDatabase> perm(
		BackupStoreRefCountDatabase::Load(
			apAccounts->GetEntry(0x1234567),
			true // ReadOnly
			));

	TEST_CHECK_THROWS(temp->GetRefCount(2),
		BackupStoreException, UnknownObjectRefCountRequested);
	TEST_CHECK_THROWS(perm->GetRefCount(2),
		BackupStoreException, UnknownObjectRefCountRequested);
	temp->AddReference(2);
	TEST_EQUAL(1, temp->GetRefCount(2));
	TEST_CHECK_THROWS(perm->GetRefCount(2),
		BackupStoreException, UnknownObjectRefCountRequested);
	temp->Discard();

	// Need to delete perm object so it doesn't keep a filehandle open,
	// preventing tearDown from rewriting the refcount DB and thus causing
	// test failure.
	perm.reset();

	TEARDOWN_TEST_UNIFIED();
}

bool test_server_housekeeping(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// We need complete control over housekeeping, so use a local client instead of a network
	// client + daemon. (Also that's a requirement for specialised tests anyway).
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, rwContext, protocol, false); // !ReadOnly
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, roContext, protocol_read_only, true); // ReadOnly

	int encfile[ENCFILE_SIZE];
	{
		for(int l = 0; l < ENCFILE_SIZE; ++l)
		{
			encfile[l] = l * 173;
		}

		// Write this to a file
		{
			FileStream f("testfiles/file1", O_WRONLY | O_CREAT);
			f.Write(encfile, sizeof(encfile));
		}
	}

	int64_t root_dir_blocks = get_disc_usage_in_blocks(true, // IsDirectory
		BACKUPSTORE_ROOT_DIRECTORY_ID, spec);
	TEST_THAT(check_num_files(fs, 0, 0, 0, 1));
	TEST_THAT(check_num_blocks(protocol, 0, 0, 0, root_dir_blocks,
		root_dir_blocks));

	// Store a file -- first make the encoded file
	BackupStoreFilenameClear store1name("file1");
	{
		FileStream out("testfiles/file1_upload1", O_WRONLY | O_CREAT);
		std::auto_ptr<IOStream> encoded(
			BackupStoreFile::EncodeFile("testfiles/file1",
				BACKUPSTORE_ROOT_DIRECTORY_ID, store1name));
		encoded->CopyStreamTo(out);
	}

	#define COMMAND(command, objectid) \
		TEST_EQUAL(objectid, protocol.command->GetObjectID());

	// Then send it
	int64_t store1objid = BACKUPSTORE_ROOT_DIRECTORY_ID + 1;
	{
		std::auto_ptr<IOStream> upload(new FileStream("testfiles/file1_upload1"));
		COMMAND(QueryStoreFile(
			BACKUPSTORE_ROOT_DIRECTORY_ID,
			0x123456789abcdefLL,		/* modification time */
			0x7362383249872dfLL,		/* attr hash */
			0,							/* diff from ID */
			store1name,
			upload), store1objid);
	}

	// Update expected reference count of this new object
	set_refcount(store1objid, 1);

	// And retrieve it
	{
		// Retrieve as object
		COMMAND(QueryGetObject(store1objid), store1objid);

		// BLOCK
		{
			// Get stream
			std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
			// Need to put it in another stream, because it's not in stream order
			CollectInBufferStream f;
			filestream->CopyStreamTo(f);
			f.SetForReading();
			// Get and decode
			UNLINK_IF_EXISTS("testfiles/file1_upload_retrieved");
			BackupStoreFile::DecodeFile(f, "testfiles/file1_upload_retrieved", IOStream::TimeOutInfinite);
		}

		// Retrieve as file
		COMMAND(QueryGetFile(BACKUPSTORE_ROOT_DIRECTORY_ID, store1objid), store1objid);

		// BLOCK
		{
			UNLINK_IF_EXISTS("testfiles/file1_upload_retrieved_str");

			// Get stream
			std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
			// Get and decode
			BackupStoreFile::DecodeFile(*filestream, "testfiles/file1_upload_retrieved_str", IOStream::TimeOutInfinite);
		}

		// Read in rebuilt original, and compare contents
		{
			FileStream in("testfiles/file1_upload_retrieved");
			int encfile_i[ENCFILE_SIZE];
			in.Read(encfile_i, sizeof(encfile_i));
			TEST_THAT(memcmp(encfile, encfile_i, sizeof(encfile)) == 0);
		}

		{
			FileStream in("testfiles/file1_upload_retrieved_str");
			int encfile_i[ENCFILE_SIZE];
			in.Read(encfile_i, sizeof(encfile_i));
			TEST_THAT(memcmp(encfile, encfile_i, sizeof(encfile)) == 0);
		}

		// Retrieve the block index, by ID
		{
			COMMAND(QueryGetBlockIndexByID(store1objid), store1objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
			// Check against uploaded file
			TEST_THAT(check_block_index("testfiles/file1_upload1", *blockIndexStream));
		}

		// and again, by name
		{
			COMMAND(QueryGetBlockIndexByName(BACKUPSTORE_ROOT_DIRECTORY_ID, store1name),
				store1objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
			// Check against uploaded file
			TEST_THAT(check_block_index("testfiles/file1_upload1", *blockIndexStream));
		}
	}

	// Get the directory again, and see if the entry is in it
	{
		// Command
		COMMAND(QueryListDirectory(
			BACKUPSTORE_ROOT_DIRECTORY_ID,
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
			false /* no attributes */),
			BACKUPSTORE_ROOT_DIRECTORY_ID);

		// Download the response-following stream (the directory):
		BackupStoreDirectory dir(protocol.ReceiveStream(), SHORT_TIMEOUT);
		TEST_THAT(dir.GetNumberOfEntries() == 1);
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = i.Next();
		TEST_THAT(en != 0);
		TEST_THAT(i.Next() == 0);
		if(en != 0)
		{
			TEST_THAT(en->GetName() == store1name);
			TEST_THAT(en->GetModificationTime() == 0x123456789abcdefLL);
			TEST_THAT(en->GetAttributesHash() == 0x7362383249872dfLL);
			TEST_THAT(en->GetObjectID() == store1objid);
			int expected_size = (spec.name() == "store") ? 6 : 1;
			TEST_EQUAL(expected_size, en->GetSizeInBlocks());
			TEST_THAT(en->GetFlags() == BackupStoreDirectory::Entry::Flags_File);
		}
	}

	int file1_blocks = get_disc_usage_in_blocks(false, // !IsDirectory
		store1objid, spec);
	TEST_THAT(check_num_files(fs, 1, 0, 0, 1));
	TEST_THAT(check_num_blocks(protocol, file1_blocks, 0, 0, root_dir_blocks,
		file1_blocks + root_dir_blocks));

	// Upload again, as a patch to the original file.
	int64_t patch1_id = BackupStoreFile::QueryStoreFileDiff(protocol,
		"testfiles/file1", // LocalFilename
		BACKUPSTORE_ROOT_DIRECTORY_ID, // DirectoryObjectID
		store1objid, // DiffFromFileID
		0x7362383249872dfLL, // AttributesHash
		store1name // StoreFilename
		);
	TEST_EQUAL_LINE(store1objid + 1, patch1_id, "wrong ObjectID for newly uploaded full file");
	// Update expected reference count of this new object
	set_refcount(patch1_id, 1);

	// How many extra blocks used by uploading patch1?
	// We need to check the old file's size, because it's been replaced
	// by a reverse diff, and patch1_id is a complete file, not a diff.
	int patch1_blocks = get_disc_usage_in_blocks(false, // !IsDirectory
		store1objid, spec);

	// It will take extra blocks, even though there are no changes, because
	// the server code is not smart enough to realise that the file
	// contents are identical, so it will create an empty patch.

	TEST_THAT(check_num_files(fs, 1, 1, 0, 1));
	TEST_THAT(check_num_blocks(protocol, file1_blocks, patch1_blocks, 0,
		root_dir_blocks, file1_blocks + patch1_blocks + root_dir_blocks));
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// Change the file and upload again, as a patch to the original file.
	{
		FileStream out("testfiles/file1", O_WRONLY | O_APPEND);
		std::string appendix = "appendix!";
		out.Write(appendix.c_str(), appendix.size());
		out.Close();
	}

	int64_t patch2_id = BackupStoreFile::QueryStoreFileDiff(protocol,
		"testfiles/file1", // LocalFilename
		BACKUPSTORE_ROOT_DIRECTORY_ID, // DirectoryObjectID
		patch1_id, // DiffFromFileID
		0x7362383249872dfLL, // AttributesHash
		store1name // StoreFilename
		);
	TEST_EQUAL_LINE(4, patch2_id, "wrong ObjectID for newly uploaded "
		"patch file");
	set_refcount(patch2_id, 1);

	// How many extra blocks used by uploading patch2?
	// We need to check the old file's size, because it's been replaced
	// by a reverse diff, and patch2_id is a complete file, not a diff.
	int patch2_blocks = get_disc_usage_in_blocks(false, // !IsDirectory
		patch1_id, spec);

	TEST_THAT(check_num_files(fs, 1, 2, 0, 1));
	TEST_THAT(check_num_blocks(protocol, file1_blocks, patch1_blocks + patch2_blocks, 0,
		root_dir_blocks, file1_blocks + patch1_blocks + patch2_blocks +
		root_dir_blocks));
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// Housekeeping should not change anything just yet
	protocol.QueryFinished();
	TEST_THAT(run_housekeeping_and_check_account(fs));
	protocol.Reopen();

	TEST_THAT(check_num_files(fs, 1, 2, 0, 1));
	TEST_THAT(check_num_blocks(protocol, file1_blocks, patch1_blocks + patch2_blocks, 0,
		root_dir_blocks, file1_blocks + patch1_blocks + patch2_blocks +
		root_dir_blocks));
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// Upload not as a patch, but as a completely different file. This
	// marks the previous file as old (because the filename is the same)
	// but used to not adjust the number of old/deleted files properly.
	int64_t replaced_id = BackupStoreFile::QueryStoreFileDiff(protocol,
		"testfiles/file1", // LocalFilename
		BACKUPSTORE_ROOT_DIRECTORY_ID, // DirectoryObjectID
		0, // DiffFromFileID
		0x7362383249872dfLL, // AttributesHash
		store1name // StoreFilename
		);
	TEST_EQUAL_LINE(5, replaced_id, "wrong ObjectID for newly uploaded "
		"full file");
	set_refcount(replaced_id, 1);

	// How many blocks used by the new file? This time we need to check the new file, because
	// it's not a patch, so the old file wasn't converted to a reverse diff.
	int replaced_blocks = get_disc_usage_in_blocks(false, // !IsDirectory
		replaced_id, spec);

	TEST_THAT(check_num_files(fs, 1, 3, 0, 1));
	TEST_THAT(check_num_blocks(protocol, replaced_blocks, // current
		file1_blocks + patch1_blocks + patch2_blocks, // old
		0, // deleted
		root_dir_blocks, // directories
		file1_blocks + patch1_blocks + patch2_blocks + replaced_blocks +
		root_dir_blocks)); // total
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// Housekeeping should not change anything just yet
	protocol.QueryFinished();
	TEST_THAT(run_housekeeping_and_check_account(fs));
	protocol.Reopen();

	TEST_THAT(check_num_files(fs, 1, 3, 0, 1));
	TEST_THAT(check_num_blocks(protocol, replaced_blocks, // current
		file1_blocks + patch1_blocks + patch2_blocks, // old
		0, // deleted
		root_dir_blocks, // directories
		file1_blocks + patch1_blocks + patch2_blocks + replaced_blocks +
		root_dir_blocks)); // total
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// But if we reduce the limits, then it will
	std::ostringstream new_limit;
	new_limit << (replaced_blocks + file1_blocks + root_dir_blocks) << "B";
	protocol.QueryFinished();
	TEST_THAT(change_account_limits(spec.control(), new_limit.str().c_str(), "2000B"));
	TEST_THAT(run_housekeeping_and_check_account(fs));
	protocol.Reopen();

	// We expect housekeeping to have removed the two oldest versions:
	set_refcount(store1objid, 0);
	set_refcount(patch1_id, 0);

	TEST_THAT(check_num_files(fs, 1, 1, 0, 1));
	TEST_THAT(check_num_blocks(protocol, replaced_blocks, // current
		file1_blocks, // old
		0, // deleted
		root_dir_blocks, // directories
		file1_blocks + replaced_blocks + root_dir_blocks)); // total
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// Check that deleting files is accounted for as well
	COMMAND(QueryDeleteFile(BACKUPSTORE_ROOT_DIRECTORY_ID, store1name), replaced_id);

	// The old version file is deleted as well!
	TEST_THAT(check_num_files(fs, 0, 1, 2, 1));
	TEST_THAT(check_num_blocks(protocol, 0, // current
		file1_blocks, // old
		replaced_blocks + file1_blocks, // deleted
		root_dir_blocks, // directories
		file1_blocks + replaced_blocks + root_dir_blocks));
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// Reduce limits again, check that removed files are subtracted from
	// block counts.
	protocol.QueryFinished();
	TEST_THAT(change_account_limits(spec.control(), "0B", "2000B"));
	TEST_THAT(run_housekeeping_and_check_account(fs));
	protocol.Reopen();
	set_refcount(store1objid, 0);

	// We expect housekeeping to have removed the two most recent versions
	// of the now-deleted file:
	set_refcount(patch2_id, 0);
	set_refcount(replaced_id, 0);

	TEST_THAT(check_num_files(fs, 0, 0, 0, 1));
	TEST_THAT(check_num_blocks(protocol, 0, 0, 0, root_dir_blocks, root_dir_blocks));
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	#undef COMMAND

	// Close the protocol, so we can housekeep the account
	protocol.QueryFinished();

	TEARDOWN_TEST_SPECIALISED(spec);
}

int64_t create_directory(BackupProtocolCallable& protocol, int64_t parent_dir_id)
{
	// Create a directory
	BackupStoreFilenameClear dirname("lovely_directory");
	// Attributes
	std::auto_ptr<IOStream> attr(new MemBlockStream(attr1, sizeof(attr1)));

	std::auto_ptr<BackupProtocolSuccess> dirCreate(
		protocol.QueryCreateDirectory2(
			parent_dir_id, FAKE_ATTR_MODIFICATION_TIME,
			FAKE_MODIFICATION_TIME, dirname, attr));

	int64_t subdirid = dirCreate->GetObjectID();
	set_refcount(subdirid, 1);
	return subdirid;
}

int64_t create_file(BackupProtocolCallable& protocol, int64_t subdirid,
	const std::string& remote_filename)
{
	// write_test_file(0) should be called by each test that uses create_file(), before calling it.
	// Not rewriting the test file over and over makes a surprisingly big difference on Windows.

	BackupStoreFilenameClear remote_filename_encoded;
	if (remote_filename.empty())
	{
		remote_filename_encoded = uploads[0].name;
	}
	else
	{
		remote_filename_encoded = remote_filename;
	}

	std::string filename("testfiles/test0");
	int64_t modtime;
	std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile(filename,
		subdirid, remote_filename_encoded, &modtime));

	std::auto_ptr<BackupProtocolSuccess> stored(protocol.QueryStoreFile(
		subdirid,
		modtime,
		modtime, /* use for attr hash too */
		0, /* diff from ID */
		remote_filename_encoded,
		upload));

	int64_t subdirfileid = stored->GetObjectID();
	set_refcount(subdirfileid, 1);
	return subdirfileid;
}

bool test_multiple_uploads(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, rwContext, protocol, false); // !ReadOnly
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, roContext, protocol_read_only, true); // ReadOnly

	// Read the root directory a few times (as it's cached, so make sure it doesn't hurt anything)
	for(int l = 0; l < 3; ++l)
	{
		// Command
		protocol.QueryListDirectory(
			BACKUPSTORE_ROOT_DIRECTORY_ID,
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */);
		// Stream
		BackupStoreDirectory dir(protocol.ReceiveStream(),
			protocol.GetTimeout());
		TEST_THAT(dir.GetNumberOfEntries() == 0);
	}

	// Read the dir from the readonly connection (make sure it gets in the cache)
	// Command
	protocol_read_only.QueryListDirectory(
		BACKUPSTORE_ROOT_DIRECTORY_ID,
		BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
		BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		false /* no attributes */);
	// Stream
	BackupStoreDirectory dir(protocol_read_only.ReceiveStream(),
		protocol_read_only.GetTimeout());
	TEST_THAT(dir.GetNumberOfEntries() == 0);

	// TODO FIXME dedent
	{
		TEST_THAT(check_num_files(fs, 0, 0, 0, 1));

		// sleep to ensure that the timestamp on the file will change
		::safe_sleep(1);

		// Create and upload some test files
		int64_t maxID = 0;
		for(int t = 0; t < UPLOAD_NUM; ++t)
		{
			write_test_file(t);

			std::string filename("testfiles/test");
			filename += uploads[t].fnextra;
			int64_t modtime = 0;

			std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile(filename.c_str(), BACKUPSTORE_ROOT_DIRECTORY_ID, uploads[t].name, &modtime));
			TEST_THAT(modtime != 0);

			std::auto_ptr<BackupProtocolSuccess> stored(protocol.QueryStoreFile(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				modtime,
				modtime, /* use it for attr hash too */
				0, /* diff from ID */
				uploads[t].name,
				upload));
			uploads[t].allocated_objid = stored->GetObjectID();
			uploads[t].mod_time = modtime;
			if(maxID < stored->GetObjectID()) maxID = stored->GetObjectID();
			set_refcount(stored->GetObjectID(), 1);
			BOX_TRACE("wrote file " << filename << " to server "
				"as object " <<
				BOX_FORMAT_OBJECTID(stored->GetObjectID()));

			// Some of the uploaded files replace old ones, increasing
			// the old file count instead of the current file count.
			int expected_num_old_files = 0;
			if (t >= 8) expected_num_old_files++;
			if (t >= 12) expected_num_old_files++;
			if (t >= 13) expected_num_old_files++;
			int expected_num_current_files = t + 1 - expected_num_old_files;

			TEST_THAT(check_num_files(fs, expected_num_current_files,
				expected_num_old_files, 0, 1));

			protocol.QueryFinished();
			protocol_read_only.QueryFinished();
			TEST_THAT(run_housekeeping_and_check_account(fs));
			protocol.Reopen();
			protocol_read_only.Reopen();

			TEST_THAT(check_num_files(fs, expected_num_current_files,
				expected_num_old_files, 0, 1));
		}

		// Add some attributes onto one of them
		{
			TEST_THAT(check_num_files(fs, UPLOAD_NUM - 3, 3, 0, 1));
			std::auto_ptr<IOStream> attrnew(
				new MemBlockStream(attr3, sizeof(attr3)));
			std::auto_ptr<BackupProtocolSuccess> set(protocol.QuerySetReplacementFileAttributes(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				32498749832475LL,
				uploads[UPLOAD_ATTRS_EN].name,
				attrnew));
			TEST_THAT(set->GetObjectID() == uploads[UPLOAD_ATTRS_EN].allocated_objid);
			TEST_THAT(check_num_files(fs, UPLOAD_NUM - 3, 3, 0, 1));
		}

		protocol.QueryFinished();
		protocol_read_only.QueryFinished();
		TEST_THAT(run_housekeeping_and_check_account(fs));
		protocol.Reopen();
		protocol_read_only.Reopen();

		// Delete one of them (will implicitly delete an old version)
		{
			std::auto_ptr<BackupProtocolSuccess> del(protocol.QueryDeleteFile(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				uploads[UPLOAD_DELETE_EN].name));
			TEST_THAT(del->GetObjectID() == uploads[UPLOAD_DELETE_EN].allocated_objid);
			TEST_THAT(check_num_files(fs, UPLOAD_NUM - 4, 3, 2, 1));
		}

		protocol.QueryFinished();
		protocol_read_only.QueryFinished();
		TEST_THAT(run_housekeeping_and_check_account(fs));
		protocol.Reopen();
		protocol_read_only.Reopen();

		// Check that the block index can be obtained by name even though it's been deleted
		{
			// Fetch the raw object
			{
				FileStream out("testfiles/downloaddelobj", O_WRONLY | O_CREAT);
				std::auto_ptr<BackupProtocolSuccess> getobj(protocol.QueryGetObject(uploads[UPLOAD_DELETE_EN].allocated_objid));
				std::auto_ptr<IOStream> objstream(protocol.ReceiveStream());
				objstream->CopyStreamTo(out, protocol.GetTimeout());
			}
			// query index and test
			std::auto_ptr<BackupProtocolSuccess> getblockindex(protocol.QueryGetBlockIndexByName(
				BACKUPSTORE_ROOT_DIRECTORY_ID, uploads[UPLOAD_DELETE_EN].name));
			TEST_THAT(getblockindex->GetObjectID() == uploads[UPLOAD_DELETE_EN].allocated_objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
			TEST_THAT(check_block_index("testfiles/downloaddelobj", *blockIndexStream));
		}

		// Download them all... (even deleted files)
		for(int t = 0; t < UPLOAD_NUM; ++t)
		{
			printf("%d\n", t);
			std::auto_ptr<BackupProtocolSuccess> getFile(protocol.QueryGetFile(BACKUPSTORE_ROOT_DIRECTORY_ID, uploads[t].allocated_objid));
			TEST_THAT(getFile->GetObjectID() == uploads[t].allocated_objid);
			std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
			test_test_file(t, *filestream);
		}

		{
			StreamableMemBlock attrtest(attr3, sizeof(attr3));

			// Use the read only connection to verify that the directory is as we expect
			printf("\n\n==== Reading directory using read-only connection\n");
			check_dir_after_uploads(protocol_read_only, attrtest);
			printf("done.\n\n");
			// And on the read/write one
			check_dir_after_uploads(protocol, attrtest);
		}

		// sleep to ensure that the timestamp on the file will change
		::safe_sleep(1);

		// Check diffing and rsync like stuff...
		// Build a modified file
		{
			// Basically just insert a bit in the middle
			TEST_THAT(TestGetFileSize(TEST_FILE_FOR_PATCHING) == TEST_FILE_FOR_PATCHING_SIZE);
			FileStream in(TEST_FILE_FOR_PATCHING);
			void *buf = ::malloc(TEST_FILE_FOR_PATCHING_SIZE);
			FileStream out(TEST_FILE_FOR_PATCHING ".mod", O_WRONLY | O_CREAT);
			TEST_THAT(in.Read(buf, TEST_FILE_FOR_PATCHING_PATCH_AT) == TEST_FILE_FOR_PATCHING_PATCH_AT);
			out.Write(buf, TEST_FILE_FOR_PATCHING_PATCH_AT);
			char insert[13] = "INSERTINSERT";
			out.Write(insert, sizeof(insert));
			TEST_THAT(in.Read(buf, TEST_FILE_FOR_PATCHING_SIZE - TEST_FILE_FOR_PATCHING_PATCH_AT) == TEST_FILE_FOR_PATCHING_SIZE - TEST_FILE_FOR_PATCHING_PATCH_AT);
			out.Write(buf, TEST_FILE_FOR_PATCHING_SIZE - TEST_FILE_FOR_PATCHING_PATCH_AT);
			::free(buf);
		}

		TEST_THAT(check_num_files(fs, UPLOAD_NUM - 4, 3, 2, 1));

		// Run housekeeping (for which we need to disconnect
		// ourselves) and check that it doesn't change the numbers
		// of files
		protocol.QueryFinished();
		protocol_read_only.QueryFinished();
		TEST_THAT(run_housekeeping_and_check_account(fs));
		protocol.Reopen();
		protocol_read_only.Reopen();

		TEST_THAT(check_num_files(fs, UPLOAD_NUM - 4, 3, 2, 1));

		{
			// Fetch the block index for this one
			std::auto_ptr<BackupProtocolSuccess> getblockindex(protocol.QueryGetBlockIndexByName(
				BACKUPSTORE_ROOT_DIRECTORY_ID, uploads[UPLOAD_PATCH_EN].name));
			TEST_THAT(getblockindex->GetObjectID() == uploads[UPLOAD_PATCH_EN].allocated_objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());

			// Do the patching
			bool isCompletelyDifferent = false;
			int64_t modtime;
			std::auto_ptr<IOStream> patchstream(
				BackupStoreFile::EncodeFileDiff(
					TEST_FILE_FOR_PATCHING ".mod",
					BACKUPSTORE_ROOT_DIRECTORY_ID,
					uploads[UPLOAD_PATCH_EN].name,
					uploads[UPLOAD_PATCH_EN].allocated_objid,
					*blockIndexStream,
					SHORT_TIMEOUT,
					NULL, // pointer to DiffTimer impl
					&modtime, &isCompletelyDifferent));
			TEST_THAT(isCompletelyDifferent == false);

			// Sent this to a file, so we can check the size, rather than uploading it directly
			{
				FileStream patch(TEST_FILE_FOR_PATCHING ".patch", O_WRONLY | O_CREAT);
				patchstream->CopyStreamTo(patch);
			}

			// Release blockIndexStream to close the RaidFile, so that we can rename over it
			blockIndexStream.reset();

			// Make sure the stream is a plausible size for a patch containing only one new block
			TEST_THAT(TestGetFileSize(TEST_FILE_FOR_PATCHING ".patch") < (8*1024));

			// Upload it
			int64_t patchedID = 0;
			{
				std::auto_ptr<IOStream> uploadpatch(new FileStream(TEST_FILE_FOR_PATCHING ".patch"));
				std::auto_ptr<BackupProtocolSuccess> stored(protocol.QueryStoreFile(
					BACKUPSTORE_ROOT_DIRECTORY_ID,
					modtime,
					modtime, /* use it for attr hash too */
					uploads[UPLOAD_PATCH_EN].allocated_objid,		/* diff from ID */
					uploads[UPLOAD_PATCH_EN].name,
					uploadpatch));
				TEST_THAT(stored->GetObjectID() > 0);
				if(maxID < stored->GetObjectID()) maxID = stored->GetObjectID();
				patchedID = stored->GetObjectID();
			}

			set_refcount(patchedID, 1);

			// Then download it to check it's OK
			std::auto_ptr<BackupProtocolSuccess> getFile(
				protocol.QueryGetFile(BACKUPSTORE_ROOT_DIRECTORY_ID, patchedID));
			TEST_THAT(getFile->GetObjectID() == patchedID);
			std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
			BackupStoreFile::DecodeFile(*filestream,
				TEST_FILE_FOR_PATCHING ".downloaded", SHORT_TIMEOUT);

			// Check it's the same
			TEST_THAT(check_files_same(TEST_FILE_FOR_PATCHING ".downloaded", TEST_FILE_FOR_PATCHING ".mod"));
			TEST_THAT(check_num_files(fs, UPLOAD_NUM - 4, 4, 2, 1));
		}
	}

	protocol.QueryFinished();
	protocol_read_only.QueryFinished();

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_server_commands(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// Write the test file for create_file to upload:
	write_test_file(0);

	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, rwContext, protocol, false); // !ReadOnly
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, roContext, protocol_read_only, true); // ReadOnly

	// Try retrieving an object that doesn't exist. That used to return
	// BackupProtocolSuccess(NoObject) for no apparent reason.
	{
		TEST_COMMAND_RETURNS_ERROR(protocol, QueryGetObject(2),
			Err_DoesNotExist);
	}

	{
		TEST_COMMAND_RETURNS_ERROR(protocol, QueryListDirectory(2, false, 0, false),
			Err_DoesNotExist);
	}

	// Try using GetFile on an object ID that doesn't exist in the directory.
	{
		TEST_COMMAND_RETURNS_ERROR(protocol,
			QueryGetFile(BACKUPSTORE_ROOT_DIRECTORY_ID,
				BACKUPSTORE_ROOT_DIRECTORY_ID),
			Err_DoesNotExistInDirectory);
	}

	// Try uploading a file that doesn't verify.
	{
		std::auto_ptr<IOStream> upload(new ZeroStream(1000));
		TEST_COMMAND_RETURNS_ERROR(protocol, QueryStoreFile(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				0,
				0, /* use for attr hash too */
				0, /* diff from ID */
				uploads[0].name,
				upload),
			Err_FileDoesNotVerify);
	}

	// TODO FIXME: in the case of S3 stores, we will have sent the request (but no data) before
	// the client realises that the stream is invalid, and aborts. The S3 server will receive a
	// PUT request for a zero-byte file, and have no idea that it's not a valid file, so it will
	// store it. We should send a checksum (if possible) and a content-length (at least) to
	// prevent this, and test that no file is stored instead of unlinking it here. Alternatively,
	// the server could notice that the client closed the connection and didn't read the 200 OK
	// response (but sent back an RST instead), and delete the file that it just created.
	if(spec.name() == "s3")
	{
		TEST_EQUAL(0, EMU_UNLINK("testfiles/store/subdir/0x2.file"));
	}

	// Try uploading a file referencing another file which doesn't exist.
	// This used to not consume the stream, leaving it unusable.
	{
		std::auto_ptr<IOStream> upload(new ZeroStream(1000));
		TEST_COMMAND_RETURNS_ERROR(protocol, QueryStoreFile(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				0,
				0, /* use for attr hash too */
				99999, /* diff from ID */
				uploads[0].name,
				upload),
			Err_DiffFromFileDoesNotExist);
	}

	// BLOCK
	// TODO FIXME dedent this block.
	{
		// Create a directory
		int64_t subdirid = create_directory(protocol);
		// Ensure that store info is flushed out to disk, so we can check it:
		rwContext.SaveStoreInfo(false); // !AllowDelay
		TEST_THAT(check_num_files(fs, 0, 0, 0, 2));

		// Try using GetFile on the directory
		{
			TEST_COMMAND_RETURNS_ERROR(protocol,
				QueryGetFile(BACKUPSTORE_ROOT_DIRECTORY_ID,
					subdirid),
				Err_FileDoesNotVerify);
		}

		// Stick a file in it
		int64_t subdirfileid = create_file(protocol, subdirid);
		TEST_THAT(check_num_files(fs, 1, 0, 0, 2));

		// Flush the cache so that the read-only connection works
		rwContext.FlushDirectoryCache();

		BOX_TRACE("Checking root directory using read-only connection");
		{
			// Command
			protocol_read_only.QueryListDirectory(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
				BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
				false /* no attributes! */); // Stream
			BackupStoreDirectory dir(protocol_read_only.ReceiveStream(),
				SHORT_TIMEOUT);

			// UPLOAD_NUM test files, patch uploaded and new dir
			TEST_EQUAL(1, dir.GetNumberOfEntries());

			// Check the last one...
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;
			BackupStoreDirectory::Entry *t = 0;
			BackupStoreFilenameClear dirname("lovely_directory");

			while((t = i.Next()) != 0)
			{
				if(en != 0)
				{
					// here for all but last object
					TEST_THAT(en->GetObjectID() != subdirid);
					TEST_THAT(en->GetName() != dirname);
				}
				en = t;
			}

			// Check that the last entry looks right
			TEST_THAT_OR(en != NULL, FAIL);
			TEST_EQUAL(subdirid, en->GetObjectID());
			TEST_THAT(en->GetName() == dirname);
			TEST_EQUAL(BackupProtocolListDirectory::Flags_Dir, en->GetFlags());
			int64_t actual_size = get_disc_usage_in_blocks(true, // IsDirectory
				subdirid, spec);
			TEST_EQUAL(actual_size, en->GetSizeInBlocks());
			TEST_EQUAL(FAKE_MODIFICATION_TIME, en->GetModificationTime());
		}

		BOX_TRACE("Checking subdirectory using read-only connection");
		{
			// Command
			TEST_EQUAL(subdirid,
				protocol_read_only.QueryListDirectory(
					subdirid,
					BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
					BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
					true /* get attributes */)->GetObjectID());
			BackupStoreDirectory dir(protocol_read_only.ReceiveStream(),
				SHORT_TIMEOUT);
			TEST_THAT(dir.GetNumberOfEntries() == 1);

			// Check the (only) one...
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);

			// Check that it looks right
			TEST_EQUAL(subdirfileid, en->GetObjectID());
			TEST_THAT(en->GetName() == uploads[0].name);
			TEST_EQUAL(BackupProtocolListDirectory::Flags_File, en->GetFlags());
			int64_t actual_size = get_disc_usage_in_blocks(false, // !IsDirectory
				subdirfileid, spec);
			TEST_EQUAL(actual_size, en->GetSizeInBlocks());
			TEST_THAT(en->GetModificationTime() != 0);

			// Attributes
			TEST_THAT(dir.HasAttributes());
			TEST_EQUAL(FAKE_ATTR_MODIFICATION_TIME,
				dir.GetAttributesModTime());
			StreamableMemBlock attr(attr1, sizeof(attr1));
			TEST_THAT(dir.GetAttributes() == attr);
		}

		BOX_TRACE("Checking that we don't get attributes if we don't ask for them");
		{
			// Command
			protocol_read_only.QueryListDirectory(
				subdirid,
				BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
				BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
				false /* no attributes! */);
			// Stream
			BackupStoreDirectory dir(protocol_read_only.ReceiveStream(),
				SHORT_TIMEOUT);
			TEST_THAT(!dir.HasAttributes());
		}

		// Sleep to ensure that the timestamp on the file will change,
		// invalidating the read-only connection's cache of the
		// directory, and forcing it to be reloaded.
		::safe_sleep(1);

		// Change attributes on the directory
		{
			std::auto_ptr<IOStream> attrnew(
				new MemBlockStream(attr2, sizeof(attr2)));
			std::auto_ptr<BackupProtocolSuccess> changereply(
				protocol.QueryChangeDirAttributes(
					subdirid,
					329483209443598LL,
					attrnew));
			TEST_THAT(changereply->GetObjectID() == subdirid);
		}

		// Check the new attributes using the read-write connection
		{
			// Command
			protocol.QueryListDirectory(
				subdirid,
				0,	// no flags
				BackupProtocolListDirectory::Flags_EXCLUDE_EVERYTHING,
				true /* get attributes */);
			// Stream
			BackupStoreDirectory dir(protocol.ReceiveStream(),
				SHORT_TIMEOUT);
			TEST_THAT(dir.GetNumberOfEntries() == 0);

			// Attributes
			TEST_THAT(dir.HasAttributes());
			TEST_EQUAL(329483209443598LL, dir.GetAttributesModTime());
			StreamableMemBlock attrtest(attr2, sizeof(attr2));
			TEST_THAT(dir.GetAttributes() == attrtest);
		}

		// Check the new attributes using the read-only connection
		{
			rwContext.FlushDirectoryCache();
			roContext.ClearDirectoryCache();

			// Command
			protocol_read_only.QueryListDirectory(
				subdirid,
				0,	// no flags
				BackupProtocolListDirectory::Flags_EXCLUDE_EVERYTHING,
				true /* get attributes */);
			// Stream
			BackupStoreDirectory dir(protocol_read_only.ReceiveStream(),
				SHORT_TIMEOUT);
			TEST_THAT(dir.GetNumberOfEntries() == 0);

			// Attributes
			TEST_THAT(dir.HasAttributes());
			TEST_EQUAL(329483209443598LL, dir.GetAttributesModTime());
			StreamableMemBlock attrtest(attr2, sizeof(attr2));
			TEST_THAT(dir.GetAttributes() == attrtest);
		}

		BackupStoreFilenameClear& oldName(uploads[0].name);
		int64_t root_file_id = create_file(protocol, BACKUPSTORE_ROOT_DIRECTORY_ID);
		TEST_THAT(check_num_files(fs, 2, 0, 0, 2));

		// Upload a new version of the file as well, to ensure that the
		// old version is moved along with the current version.
		root_file_id = BackupStoreFile::QueryStoreFileDiff(protocol,
			"testfiles/test0", BACKUPSTORE_ROOT_DIRECTORY_ID,
			0, // DiffFromFileID
			0, // AttributesHash
			oldName);
		set_refcount(root_file_id, 1);
		TEST_THAT(check_num_files(fs, 2, 1, 0, 2));

		// Check that it's in the root directory (it won't be for long)
		rwContext.FlushDirectoryCache();
		roContext.ClearDirectoryCache();
		protocol_read_only.QueryListDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID,
			0, 0, false);
		TEST_THAT(BackupStoreDirectory(protocol_read_only.ReceiveStream())
			.FindEntryByID(root_file_id) != NULL);

		BackupStoreFilenameClear newName("moved-files");

		// Sleep before modifying the root directory, to ensure that
		// the timestamp on the file it's stored in will change when
		// we modify it, invalidating the read-only connection's cache
		// and forcing it to reload the root directory, next time we
		// ask for its contents.
		::safe_sleep(1);

		// Test moving a file
		{
			std::auto_ptr<BackupProtocolSuccess> rep(protocol.QueryMoveObject(root_file_id,
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				subdirid, BackupProtocolMoveObject::Flags_MoveAllWithSameName, newName));
			TEST_EQUAL(root_file_id, rep->GetObjectID());
		}

		// Try some dodgy renames
		{
			// File doesn't exist at all
			TEST_COMMAND_RETURNS_ERROR(protocol,
				QueryMoveObject(-1,
					BACKUPSTORE_ROOT_DIRECTORY_ID, subdirid,
					BackupProtocolMoveObject::Flags_MoveAllWithSameName,
					newName),
				Err_DoesNotExistInDirectory);
			BackupStoreFilenameClear newName("moved-files");
			TEST_COMMAND_RETURNS_ERROR(protocol,
				QueryMoveObject(
					uploads[UPLOAD_FILE_TO_MOVE].allocated_objid,
					BackupProtocolListDirectory::RootDirectory,
					subdirid,
					BackupProtocolMoveObject::Flags_MoveAllWithSameName,
					newName),
				Err_DoesNotExistInDirectory);
			TEST_COMMAND_RETURNS_ERROR(protocol,
				QueryMoveObject(
					uploads[UPLOAD_FILE_TO_MOVE].allocated_objid,
					subdirid,
					subdirid,
					BackupProtocolMoveObject::Flags_MoveAllWithSameName,
					newName),
				Err_DoesNotExistInDirectory);
		}

		// File exists, but not in this directory (we just moved it)
		TEST_COMMAND_RETURNS_ERROR(protocol,
			QueryMoveObject(root_file_id,
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				subdirid,
				BackupProtocolMoveObject::Flags_MoveAllWithSameName,
				newName),
			Err_DoesNotExistInDirectory);

		// Moving file to same directory that it's already in,
		// with the same name
		TEST_COMMAND_RETURNS_ERROR(protocol,
			QueryMoveObject(root_file_id,
				subdirid,
				subdirid,
				BackupProtocolMoveObject::Flags_MoveAllWithSameName,
				newName),
			Err_TargetNameExists);

		// Rename within a directory (successfully)
		{
			BackupStoreFilenameClear newName2("moved-files-x");
			protocol.QueryMoveObject(root_file_id, subdirid,
				subdirid, BackupProtocolMoveObject::Flags_MoveAllWithSameName,
				newName2);
		}

		// Check it's all gone from the root directory...
		rwContext.FlushDirectoryCache();
		roContext.ClearDirectoryCache();
		protocol_read_only.QueryListDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID,
			0, 0, false);
		TEST_THAT(BackupStoreDirectory(protocol_read_only.ReceiveStream(),
			SHORT_TIMEOUT).FindEntryByID(root_file_id) == NULL);

		// Check the old and new versions are in the other directory
		{
			BackupStoreFilenameClear notThere("moved-files");
			BackupStoreFilenameClear lookFor("moved-files-x");

			// Command
			protocol_read_only.QueryListDirectory(
				subdirid,
				BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
				BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
				false /* no attributes */);

			// Stream
			BackupStoreDirectory dir(protocol_read_only.ReceiveStream(),
				SHORT_TIMEOUT);

			// Check entries
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;
			bool foundCurrent = false;
			bool foundOld = false;
			while((en = i.Next()) != 0)
			{
				// If we find the old name, then the rename
				// operation didn't work.
				TEST_THAT(en->GetName() != notThere);

				if(en->GetName() == lookFor)
				{
					if(en->GetFlags() == (BackupStoreDirectory::Entry::Flags_File)) foundCurrent = true;
					if(en->GetFlags() == (BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion)) foundOld = true;
				}
			}
			TEST_THAT(foundCurrent);
			TEST_THAT(foundOld);
		}

		// sleep to ensure that the timestamp on the file will change
		::safe_sleep(1);

		// make a little bit more of a thing to look at
		int64_t subsubdirid = 0;
		int64_t subsubfileid = 0;
		{
			// TODO FIXME use create_dir() and create_file() instead.
			BackupStoreFilenameClear nd("sub2");
			// Attributes
			std::auto_ptr<IOStream> attr(new MemBlockStream(attr1,
				sizeof(attr1)));
			subsubdirid = protocol.QueryCreateDirectory(subdirid,
				FAKE_ATTR_MODIFICATION_TIME, nd, attr)->GetObjectID();

			write_test_file(2);

			BackupStoreFilenameClear file2("file2");
			std::auto_ptr<IOStream> upload(
				BackupStoreFile::EncodeFile("testfiles/test2",
					BACKUPSTORE_ROOT_DIRECTORY_ID, file2));
			std::auto_ptr<BackupProtocolSuccess> stored(protocol.QueryStoreFile(
				subsubdirid,
				0x123456789abcdefLL,		/* modification time */
				0x7362383249872dfLL,		/* attr hash */
				0,							/* diff from ID */
				file2,
				upload));
			subsubfileid = stored->GetObjectID();
		}

		set_refcount(subsubdirid, 1);
		set_refcount(subsubfileid, 1);
		TEST_THAT(check_num_files(fs, 3, 1, 0, 3));

		protocol.QueryFinished();
		protocol_read_only.QueryFinished();
		TEST_THAT(run_housekeeping_and_check_account(fs));
		protocol.Reopen();
		protocol_read_only.Reopen();

		// Query names -- test that invalid stuff returns not found OK
		{
			std::auto_ptr<BackupProtocolObjectName> nameRep(protocol.QueryGetObjectName(3248972347823478927LL, subsubdirid));
			TEST_THAT(nameRep->GetNumNameElements() == 0);
		}
		{
			std::auto_ptr<BackupProtocolObjectName> nameRep(protocol.QueryGetObjectName(subsubfileid, 2342378424LL));
			TEST_THAT(nameRep->GetNumNameElements() == 0);
		}
		{
			std::auto_ptr<BackupProtocolObjectName> nameRep(protocol.QueryGetObjectName(38947234789LL, 2342378424LL));
			TEST_THAT(nameRep->GetNumNameElements() == 0);
		}
		{
			std::auto_ptr<BackupProtocolObjectName> nameRep(protocol.QueryGetObjectName(BackupProtocolGetObjectName::ObjectID_DirectoryOnly, 2234342378424LL));
			TEST_THAT(nameRep->GetNumNameElements() == 0);
		}

		// Query names... first, get info for the file
		{
			std::auto_ptr<BackupProtocolObjectName> nameRep(protocol.QueryGetObjectName(subsubfileid, subsubdirid));
			std::auto_ptr<IOStream> namestream(protocol.ReceiveStream());

			TEST_THAT(nameRep->GetNumNameElements() == 3);
			TEST_THAT(nameRep->GetFlags() == BackupProtocolListDirectory::Flags_File);
			TEST_THAT(nameRep->GetModificationTime() == 0x123456789abcdefLL);
			TEST_THAT(nameRep->GetAttributesHash() == 0x7362383249872dfLL);
			static const char *testnames[] = {"file2","sub2","lovely_directory"};
			for(int l = 0; l < nameRep->GetNumNameElements(); ++l)
			{
				BackupStoreFilenameClear fn;
				fn.ReadFromStream(*namestream, 10000);
				TEST_THAT(fn.GetClearFilename() == testnames[l]);
			}
		}

		// Query names... secondly, for the directory
		{
			std::auto_ptr<BackupProtocolObjectName> nameRep(protocol.QueryGetObjectName(BackupProtocolGetObjectName::ObjectID_DirectoryOnly, subsubdirid));
			std::auto_ptr<IOStream> namestream(protocol.ReceiveStream());

			TEST_THAT(nameRep->GetNumNameElements() == 2);
			TEST_THAT(nameRep->GetFlags() == BackupProtocolListDirectory::Flags_Dir);
			static const char *testnames[] = {"sub2","lovely_directory"};
			for(int l = 0; l < nameRep->GetNumNameElements(); ++l)
			{
				BackupStoreFilenameClear fn;
				fn.ReadFromStream(*namestream, 10000);
				TEST_THAT(fn.GetClearFilename() == testnames[l]);
			}
		}

		TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

		// Create some nice recursive directories
		write_test_file(1);
		int64_t dirtodelete;

		{
			BackupStoreRefCountDatabase& rRefCount(
				fs.GetPermanentRefCountDatabase(true)); // ReadOnly
			dirtodelete = create_test_data_subdirs(protocol,
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				"test_delete", 6 /* depth */, &rRefCount);
		}

		TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

		protocol.QueryFinished();
		protocol_read_only.QueryFinished();
		TEST_THAT(run_housekeeping_and_check_account(fs));
		TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly

		// Close the refcount database and reopen it, check that the counts are
		// still the same.
		fs.CloseRefCountDatabase(
			&fs.GetPermanentRefCountDatabase(true)); // ReadOnly

		{
			BackupStoreRefCountDatabase* pRefCount =
				fs.GetCurrentRefCountDatabase();
			TEST_EQUAL((BackupStoreRefCountDatabase *)NULL, pRefCount);
			pRefCount = &fs.GetPermanentRefCountDatabase(true); // ReadOnly
			TEST_EQUAL(pRefCount, fs.GetCurrentRefCountDatabase());
			TEST_THAT(check_reference_counts(*pRefCount));
		}

		// And delete them
		protocol.Reopen();
		protocol_read_only.Reopen();

		{
			std::auto_ptr<BackupProtocolSuccess> dirdel(protocol.QueryDeleteDirectory(
					dirtodelete));
			TEST_THAT(dirdel->GetObjectID() == dirtodelete);
		}

		protocol.QueryFinished();
		protocol_read_only.QueryFinished();
		TEST_THAT(run_housekeeping_and_check_account(fs));
		TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly
		protocol_read_only.Reopen();

		// Get the root dir, checking for deleted items
		{
			// Command
			protocol_read_only.QueryListDirectory(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				BackupProtocolListDirectory::Flags_Dir |
				BackupProtocolListDirectory::Flags_Deleted,
				BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
				false /* no attributes */);
			// Stream
			BackupStoreDirectory dir(protocol_read_only.ReceiveStream(),
				SHORT_TIMEOUT);

			// Check there's only that one entry
			TEST_THAT(dir.GetNumberOfEntries() == 1);

			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);
			if(en)
			{
				TEST_EQUAL(dirtodelete, en->GetObjectID());
				BackupStoreFilenameClear n("test_delete");
				TEST_THAT(en->GetName() == n);
			}

			// Then... check everything's deleted
			assert_everything_deleted(protocol_read_only, dirtodelete);
		}

		// Undelete and check that block counts are restored properly
		protocol.Reopen();
		TEST_EQUAL(dirtodelete,
			protocol.QueryUndeleteDirectory(dirtodelete)->GetObjectID());

		// Finish the connections
		protocol.QueryFinished();
		protocol_read_only.QueryFinished();

		TEST_THAT(run_housekeeping_and_check_account(fs));
		TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

int get_object_size(BackupProtocolCallable& protocol, int64_t ObjectID,
	int64_t ContainerID)
{
	// Get the root directory cached in the read-only connection
	protocol.QueryListDirectory(ContainerID, 0, // FlagsMustBeSet
		BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		false /* no attributes */);

	BackupStoreDirectory dir(protocol.ReceiveStream());
	BackupStoreDirectory::Entry *en = dir.FindEntryByID(ObjectID);
	TEST_THAT_OR(en != 0, return -1);
	TEST_EQUAL_OR(ObjectID, en->GetObjectID(), return -1);
	return en->GetSizeInBlocks();
}

bool test_directory_parent_entry_tracks_directory_size(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

#ifdef BOX_RELEASE_BUILD
	BOX_NOTICE("skipping test: takes too long in release mode");
#else
	// Write the test file for create_file to upload:
	write_test_file(0);

	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, rwContext, protocol, false); // !ReadOnly
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, roContext, protocol_read_only, true); // ReadOnly

	int64_t subdirid = create_directory(protocol);
	// The updated entry for subdirid in the root directory (needed by get_object_size below)
	// may not have been written out of the cache yet, so flush it:
	rwContext.FlushDirectoryCache();

	// Get the root directory cached in the read-only connection, and
	// test that the initial size is correct.
	int old_size = get_disc_usage_in_blocks(true, subdirid, spec);
	TEST_THAT(old_size > 0);
	TEST_EQUAL(old_size, get_object_size(protocol_read_only, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));

	// Sleep to ensure that the directory file timestamp changes, so that
	// the read-only connection will discard its cached copy.
	safe_sleep(1);

	// Start adding files until the size on disk increases. This is
	// guaranteed to happen eventually :)
	int new_size = old_size;
	int64_t last_added_file_id = 0;
	std::string last_added_filename;

	for (int i = 0; new_size == old_size; i++)
	{
		std::ostringstream name;
		name << "testfile_" << i;
		last_added_filename = name.str();
		// No need to catch exceptions here, because we do not expect to hit the account
		// hard limit, and if we do it should cause the test to fail.
		last_added_file_id = create_file(protocol, subdirid, name.str());
		// We need to flush the directory cache every time, because we want to catch the
		// exact moment when the directory size goes over one block.
		rwContext.FlushDirectoryCache();
		new_size = get_disc_usage_in_blocks(true, subdirid, spec);
	}

	// Check that the root directory entry has been updated
	roContext.ClearDirectoryCache();
	TEST_EQUAL(new_size, get_object_size(protocol_read_only, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));

	// Now delete an entry, and check that the size is reduced
	protocol.QueryDeleteFile(subdirid,
		BackupStoreFilenameClear(last_added_filename));

	// Reduce the limits, to remove it permanently from the store
	protocol.QueryFinished();
	protocol_read_only.QueryFinished();
	TEST_THAT(change_account_limits(spec.control(), "0B", "20000B"));
	TEST_THAT(run_housekeeping_and_check_account(fs));
	set_refcount(last_added_file_id, 0);
	protocol.Reopen();
	protocol_read_only.Reopen();

	TEST_EQUAL(old_size, get_disc_usage_in_blocks(true, subdirid, spec));
	// Check that the entry in the root directory was updated too
	TEST_EQUAL(old_size, get_object_size(protocol_read_only, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));

	// Push the limits back up
	protocol.QueryFinished();
	protocol_read_only.QueryFinished();
	TEST_THAT(change_account_limits(spec.control(), "1000B", "20000B"));
	TEST_THAT(run_housekeeping_and_check_account(fs));
	protocol.Reopen();
	protocol_read_only.Reopen();

	// Now modify the root directory to remove its entry for this one
	BackupStoreDirectory root(
		*get_object_stream(true, // IsDirectory
			BACKUPSTORE_ROOT_DIRECTORY_ID, // ObjectID
			spec),
		IOStream::TimeOutInfinite);
	BackupStoreDirectory::Entry *en = root.FindEntryByID(subdirid);
	TEST_THAT_OR(en, return false);
	BackupStoreDirectory::Entry enCopy(*en);
	root.DeleteEntry(subdirid);
	fs.PutDirectory(root);

	// Add a directory, this should try to push the object size back up, which will try to
	// modify the subdir's entry in its parent, which no longer exists, which should return
	// an error:
	{
		BackupStoreFilenameClear dirname("lovely_directory");
		std::auto_ptr<IOStream> attr(new MemBlockStream(attr1, sizeof(attr1)));
		TEST_COMMAND_RETURNS_ERROR(protocol,
			QueryCreateDirectory2(subdirid, FAKE_ATTR_MODIFICATION_TIME,
				FAKE_MODIFICATION_TIME, dirname, attr),
			Err_DoesNotExistInDirectory);
	}

	// Repair the error ourselves, as bbstoreaccounts can't.
	protocol.QueryFinished();

	// We also have to remove the entry for lovely_directory created above, because otherwise
	// we can't create it again. (Perhaps it should not have been committed because we failed
	// to update the parent, but currently it is.)
	BackupStoreDirectory subdir(
		*get_object_stream(true, // IsDirectory
			subdirid, // ObjectID
			spec),
		IOStream::TimeOutInfinite);

	{
		BackupStoreDirectory::Iterator i(subdir);
		en = i.FindMatchingClearName(
			BackupStoreFilenameClear("lovely_directory"));
	}
	TEST_THAT_OR(en, return false);
	set_refcount(en->GetObjectID(), 0);
	subdir.DeleteEntry(en->GetObjectID());
	fs.PutDirectory(subdir);

	enCopy.SetSizeInBlocks(get_disc_usage_in_blocks(true, subdirid, spec));
	root.AddEntry(enCopy);
	fs.PutDirectory(root);

	// Deleting the directory and removing its entry will have changed the block counts by 3,
	// so repair the store to fix them now.
	TEST_EQUAL(2, check_account_and_fix_errors(fs));
	TEST_THAT(check_reference_counts(fs.GetPermanentRefCountDatabase(true))); // ReadOnly
	protocol.Reopen();

	// This should have fixed the error, so we should be able to add the
	// entry now. This should push the object size back up.
	int64_t dir2id = create_directory(protocol, subdirid);
	TEST_EQUAL(new_size, get_disc_usage_in_blocks(true, subdirid, spec));
	TEST_EQUAL(new_size, get_object_size(protocol, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));

	// The root directory may have been rewritten in the same second, so the cache may not
	// realise that it needs to reload it, so clear the cache:
	roContext.ClearDirectoryCache();
	TEST_EQUAL(old_size, get_object_size(protocol_read_only, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));

	// Delete it again, which should reduce the object size again
	protocol.QueryDeleteDirectory(dir2id);
	set_refcount(dir2id, 0);

	// Reduce the limits, to remove it permanently from the store
	protocol.QueryFinished();
	protocol_read_only.QueryFinished();
	TEST_THAT(change_account_limits(spec.control(), "0B", "20000B"));
	TEST_THAT(run_housekeeping_and_check_account(fs));
	protocol.Reopen();
	protocol_read_only.Reopen();

	// Check that the entry in the root directory was updated
	TEST_EQUAL(old_size, get_disc_usage_in_blocks(true, subdirid, spec));
	TEST_EQUAL(old_size, get_object_size(protocol_read_only, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));

	// Check that bbstoreaccounts check fix will detect and repair when
	// a directory's parent entry has the wrong size for the directory.
	protocol.QueryFinished();

	root.ReadFromStream(
		*get_object_stream(true, // IsDirectory
			BACKUPSTORE_ROOT_DIRECTORY_ID, // ObjectID
			spec),
		IOStream::TimeOutInfinite);
	en = root.FindEntryByID(subdirid);
	TEST_THAT_OR(en != 0, return false);
	en->SetSizeInBlocks(1234);

	// Sleep to ensure that the directory file timestamp changes, so that
	// the read-only connection will discard its cached copy.
	safe_sleep(1);
	fs.PutDirectory(root);

	TEST_EQUAL(1234, get_object_size(protocol_read_only, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));

	// Sleep to ensure that the directory file timestamp changes, so that
	// the read-only connection will discard its cached copy.
	safe_sleep(1);

	protocol_read_only.QueryFinished();
	TEST_EQUAL(1, check_account_and_fix_errors(fs));

	protocol_read_only.Reopen();
	TEST_EQUAL(old_size, get_object_size(protocol_read_only, subdirid,
		BACKUPSTORE_ROOT_DIRECTORY_ID));
	protocol_read_only.QueryFinished();
#endif // BOX_RELEASE_BUILD

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_cannot_open_multiple_writable_connections(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// Temporarily increase logging level to trace, to show stack traces on exceptions, to help
	// debug random failures of this test on AppVeyor.
	Logger::LevelGuard log_all_trace(Logging::GetConsole(), Log::TRACE);

	// Teardown calls check_account_and_fix_errors with the default logging level, WARNING,
	// which silences log messages about opening files that we want to see, so override them:
	LogLevelOverrideByFileGuard log_filestream_trace("FileStream.cpp", "", Log::TRACE);
	log_filestream_trace.Install();

	// We need another filesystem to be able to create conflicting locks:
	std::auto_ptr<Configuration> ap_config_2;
	std::auto_ptr<BackupAccountControl> ap_control_2;

	if(spec.name() == "s3")
	{
		ap_config_2 = load_config_file(DEFAULT_BBACKUPD_CONFIG_FILE,
			BackupDaemonConfigVerify);
		// We need to use a different cache directory, otherwise the cache lock will cause
		// a different exception to be thrown when the S3BackupFileSystem tries to lock it:
		ap_config_2->GetSubConfigurationEditable("S3Store").SetKeyValue("CacheDirectory",
			"testfiles/bbackupd-cache-2");
		ap_control_2.reset(new S3BackupAccountControl(*ap_config_2));
	}
	else
	{
		ap_config_2 = load_config_file(DEFAULT_BBSTORED_CONFIG_FILE,
			BackupConfigFileVerify);
		ap_control_2.reset(new BackupStoreAccountControl(*ap_config_2, 0x01234567));
	}

	BackupFileSystem& fs_2(ap_control_2->GetFileSystem());

	// First try a local protocol (makes debugging easier, and also works for s3).
	// This will create a write lock on fs, which we can probe using fs_2:
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, bs_context, protocol_writable, false); // !ReadOnly

	// Set the client store marker
	protocol_writable.QuerySetClientStoreMarker(0x8732523ab23aLL);

	// This works on platforms that have non-reentrant file locks (all but F_SETLK)
#ifndef BOX_LOCK_TYPE_F_SETLK
	BOX_TRACE("Opening read-write local protocol (expected to fail to get a write lock)");
	{
		BackupStoreContext context_2(fs_2, NULL, "fake test connection");
		context_2.SetClientHasAccount();
		BackupProtocolLocal protocol_writable_2(context_2); // !ReadOnly
		protocol_writable_2.QueryVersion(BACKUP_STORE_SERVER_VERSION);
		TEST_COMMAND_RETURNS_ERROR(protocol_writable_2, QueryLogin(0x01234567, 0),
			Err_CannotLockStoreForWriting);
	}
#endif

	// But a read-only context should work:
	BOX_TRACE("Opening read-only local protocol (expected to succeed)");
	{
		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs_2, context_2, protocol_read_only,
			true); // ReadOnly
		TEST_EQUAL(0x8732523ab23aLL, protocol_read_only.GetClientStoreMarker());
	}

	// Try network connections too (but only for store tests):
	if(spec.name() == "store")
	{
		TEST_THAT(StartServer());

		BOX_TRACE("Opening read-write connection (expected to fail to get a write lock)");
		BackupProtocolClient protocol_writable_3(open_conn("localhost", tls_context));
		{
			std::auto_ptr<BackupProtocolVersion> serverVersion
				(protocol_writable_3.QueryVersion(BACKUP_STORE_SERVER_VERSION));
			TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);
			TEST_COMMAND_RETURNS_ERROR(protocol_writable_3, QueryLogin(0x01234567, 0),
				Err_CannotLockStoreForWriting);
			protocol_writable_3.QueryFinished();
		}

		BOX_TRACE("Opening read-only connection (expected to succeed)");
		BackupProtocolClient protocol_read_only_2(open_conn("localhost", tls_context));
		{
			std::auto_ptr<BackupProtocolVersion> serverVersion
				(protocol_read_only_2.QueryVersion(BACKUP_STORE_SERVER_VERSION));
			TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);
			std::auto_ptr<BackupProtocolLoginConfirmed> loginConf
				(protocol_read_only_2.QueryLogin(0x01234567,
					BackupProtocolLogin::Flags_ReadOnly));
			TEST_EQUAL(0x8732523ab23aLL, loginConf->GetClientStoreMarker());
		}

		TEST_THAT(StopServer());
	}

	protocol_writable.QueryFinished();
	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_file_encoding()
{
	// Now test encoded files
	// TODO: This test needs to check failure situations as well as everything working,
	// but this will be saved for the full implementation.

	SETUP_TEST_UNIFIED();

	int encfile[ENCFILE_SIZE];

	{
		for(int l = 0; l < ENCFILE_SIZE; ++l)
		{
			encfile[l] = l * 173;
		}

		// Encode and decode a small block (shouldn't be compressed)
		{
			#define SMALL_BLOCK_SIZE	251
			int encBlockSize = BackupStoreFile::MaxBlockSizeForChunkSize(SMALL_BLOCK_SIZE);
			TEST_THAT(encBlockSize > SMALL_BLOCK_SIZE);
			BackupStoreFile::EncodingBuffer encoded;
			encoded.Allocate(encBlockSize / 8);		// make sure reallocation happens

			// Encode!
			int encSize = BackupStoreFile::EncodeChunk(encfile, SMALL_BLOCK_SIZE, encoded);
			// Check the header says it's not been compressed
			TEST_THAT((encoded.mpBuffer[0] & 1) == 0);
			// Check the output size has been inflated (no compression)
			TEST_THAT(encSize > SMALL_BLOCK_SIZE);

			// Decode it
			int decBlockSize = BackupStoreFile::OutputBufferSizeForKnownOutputSize(SMALL_BLOCK_SIZE);
			TEST_THAT(decBlockSize > SMALL_BLOCK_SIZE);
			uint8_t *decoded = (uint8_t*)malloc(decBlockSize);
			int decSize = BackupStoreFile::DecodeChunk(encoded.mpBuffer, encSize, decoded, decBlockSize);
			TEST_THAT(decSize < decBlockSize);
			TEST_THAT(decSize == SMALL_BLOCK_SIZE);

			// Check it came out of the wash the same
			TEST_THAT(::memcmp(encfile, decoded, SMALL_BLOCK_SIZE) == 0);

			free(decoded);
		}

		// Encode and decode a big block (should be compressed)
		{
			int encBlockSize = BackupStoreFile::MaxBlockSizeForChunkSize(ENCFILE_SIZE);
			TEST_THAT(encBlockSize > ENCFILE_SIZE);
			BackupStoreFile::EncodingBuffer encoded;
			encoded.Allocate(encBlockSize / 8);		// make sure reallocation happens

			// Encode!
			int encSize = BackupStoreFile::EncodeChunk(encfile, ENCFILE_SIZE, encoded);
			// Check the header says it's compressed
			TEST_THAT((encoded.mpBuffer[0] & 1) == 1);
			// Check the output size make it likely that it's compressed (is very compressible data)
			TEST_THAT(encSize < ENCFILE_SIZE);

			// Decode it
			int decBlockSize = BackupStoreFile::OutputBufferSizeForKnownOutputSize(ENCFILE_SIZE);
			TEST_THAT(decBlockSize > ENCFILE_SIZE);
			uint8_t *decoded = (uint8_t*)malloc(decBlockSize);
			int decSize = BackupStoreFile::DecodeChunk(encoded.mpBuffer, encSize, decoded, decBlockSize);
			TEST_THAT(decSize < decBlockSize);
			TEST_THAT(decSize == ENCFILE_SIZE);

			// Check it came out of the wash the same
			TEST_THAT(::memcmp(encfile, decoded, ENCFILE_SIZE) == 0);

			free(decoded);
		}

		// Write the test block to a file
		{
			FileStream f("testfiles/testenc1", O_WRONLY | O_CREAT);
			f.Write(encfile, sizeof(encfile));
		}

		// Encode it
		{
			FileStream out("testfiles/testenc1_enc", O_WRONLY | O_CREAT);
			BackupStoreFilenameClear name("testfiles/testenc1");

			std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/testenc1", 32, name));
			encoded->CopyStreamTo(out);
		}

		// Verify it
		{
			FileStream enc("testfiles/testenc1_enc");
			TEST_THAT(BackupStoreFile::VerifyEncodedFileFormat(enc) == true);

			// And using the stream-based interface, writing different
			// block sizes at a time.
			CollectInBufferStream contents;
			enc.Seek(0, IOStream::SeekType_Absolute);
			enc.CopyStreamTo(contents);
			contents.SetForReading();

			enc.Seek(0, IOStream::SeekType_End);
			size_t file_size = enc.GetPosition();
			TEST_EQUAL(file_size, contents.GetSize());

			for(size_t buffer_size = 1; ; buffer_size <<= 1)
			{
				enc.Seek(0, IOStream::SeekType_Absolute);
				BackupStoreFile::VerifyStream verifier(enc);
				CollectInBufferStream temp_copy;
				verifier.CopyStreamTo(temp_copy,
					IOStream::TimeOutInfinite, buffer_size);

				// The block index is only validated on Close(), which
				// CopyStreamTo() doesn't do.
				verifier.Close(false); // !CloseReadFromStream

				temp_copy.SetForReading();
				TEST_EQUAL(file_size, temp_copy.GetSize());
				TEST_THAT(memcmp(contents.GetBuffer(),
					temp_copy.GetBuffer(), file_size) == 0);

				// Keep doubling buffer size until we've copied the
				// entire encoded file in a single pass, then stop.
				if(buffer_size > file_size)
				{
					break;
				}
			}
		}

		// Decode it
		{
			UNLINK_IF_EXISTS("testfiles/testenc1_orig");
			FileStream enc("testfiles/testenc1_enc");
			BackupStoreFile::DecodeFile(enc, "testfiles/testenc1_orig", IOStream::TimeOutInfinite);
		}

		// Read in rebuilt original, and compare contents
		{
			TEST_THAT(TestGetFileSize("testfiles/testenc1_orig") == sizeof(encfile));
			FileStream in("testfiles/testenc1_orig");
			int encfile_i[ENCFILE_SIZE];
			in.Read(encfile_i, sizeof(encfile_i));
			TEST_THAT(memcmp(encfile, encfile_i, sizeof(encfile)) == 0);
		}

		// Check how many blocks it had, and test the stream based interface
		{
			FileStream enc("testfiles/testenc1_enc");
			std::auto_ptr<BackupStoreFile::DecodedStream> decoded(BackupStoreFile::DecodeFileStream(enc, IOStream::TimeOutInfinite));
			CollectInBufferStream d;
			decoded->CopyStreamTo(d, IOStream::TimeOutInfinite, 971 /* buffer block size */);
			d.SetForReading();
			TEST_THAT(d.GetSize() == sizeof(encfile));
			TEST_THAT(memcmp(encfile, d.GetBuffer(), sizeof(encfile)) == 0);

			TEST_THAT(decoded->GetNumBlocks() == 3);
		}

		// Test that the last block in a file, if less than 256 bytes, gets put into the last block
		{
			#define FILE_SIZE_JUST_OVER	((4096*2)+58)
			FileStream f("testfiles/testenc2", O_WRONLY | O_CREAT);
			f.Write(encfile + 2, FILE_SIZE_JUST_OVER);
			BackupStoreFilenameClear name("testenc2");
			std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/testenc2", 32, name));
			CollectInBufferStream e;
			encoded->CopyStreamTo(e);
			e.SetForReading();
			std::auto_ptr<BackupStoreFile::DecodedStream> decoded(BackupStoreFile::DecodeFileStream(e, IOStream::TimeOutInfinite));
			CollectInBufferStream d;
			decoded->CopyStreamTo(d, IOStream::TimeOutInfinite, 879 /* buffer block size */);
			d.SetForReading();
			TEST_THAT(d.GetSize() == FILE_SIZE_JUST_OVER);
			TEST_THAT(memcmp(encfile + 2, d.GetBuffer(), FILE_SIZE_JUST_OVER) == 0);

			TEST_THAT(decoded->GetNumBlocks() == 2);
		}

		// Test that reordered streams work too
		{
			FileStream enc("testfiles/testenc1_enc");
			std::auto_ptr<IOStream> reordered(BackupStoreFile::ReorderFileToStreamOrder(&enc, false));
			std::auto_ptr<BackupStoreFile::DecodedStream> decoded(BackupStoreFile::DecodeFileStream(*reordered, IOStream::TimeOutInfinite));
			CollectInBufferStream d;
			decoded->CopyStreamTo(d, IOStream::TimeOutInfinite, 971 /* buffer block size */);
			d.SetForReading();
			TEST_THAT(d.GetSize() == sizeof(encfile));
			TEST_THAT(memcmp(encfile, d.GetBuffer(), sizeof(encfile)) == 0);

			TEST_THAT(decoded->GetNumBlocks() == 3);
		}
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_symlinks()
{
	SETUP_TEST_UNIFIED();

#ifndef WIN32 // no symlinks on Win32
	UNLINK_IF_EXISTS("testfiles/testsymlink");
	TEST_THAT(::symlink("does/not/exist", "testfiles/testsymlink") == 0);
	BackupStoreFilenameClear name("testsymlink");
	std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/testsymlink", 32, name));

	// Can't decode it from the stream, because it's in file order, and doesn't have the
	// required properties to be able to reorder it. So buffer it...
	CollectInBufferStream b;
	encoded->CopyStreamTo(b);
	b.SetForReading();

	// Decode it
	UNLINK_IF_EXISTS("testfiles/testsymlink_2");
	BackupStoreFile::DecodeFile(b, "testfiles/testsymlink_2", IOStream::TimeOutInfinite);
#endif

	TEARDOWN_TEST_UNIFIED();
}

bool test_store_info(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	{
		BackupStoreInfo info(
			spec.name() == "s3" ? S3_FAKE_ACCOUNT_ID : 0x01234567,
			3461231233455433LL, 2934852487LL);
		fs.PutBackupStoreInfo(info);
	}

	{
		std::auto_ptr<BackupStoreInfo> info = fs.GetBackupStoreInfoUncached();
		TEST_CHECK_THROWS(fs.PutBackupStoreInfo(*info), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->ChangeBlocksUsed(1), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->ChangeBlocksInOldFiles(1), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->ChangeBlocksInDeletedFiles(1), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->RemovedDeletedDirectory(2), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->AddDeletedDirectory(2), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->SetAccountName("hello"), BackupStoreException, StoreInfoIsReadOnly);
	}

	{
		BackupStoreInfo& info(fs.GetBackupStoreInfo(false, // !ReadOnly
			true)); // Refresh
		info.ChangeBlocksUsed(8);
		info.ChangeBlocksInOldFiles(9);
		info.ChangeBlocksInDeletedFiles(10);
		info.ChangeBlocksUsed(-1);
		info.ChangeBlocksInOldFiles(-4);
		info.ChangeBlocksInDeletedFiles(-9);
		TEST_CHECK_THROWS(info.ChangeBlocksUsed(-100),
			BackupStoreException, StoreInfoBlockDeltaMakesValueNegative);
		TEST_CHECK_THROWS(info.ChangeBlocksInOldFiles(-100),
			BackupStoreException, StoreInfoBlockDeltaMakesValueNegative);
		TEST_CHECK_THROWS(info.ChangeBlocksInDeletedFiles(-100),
			BackupStoreException, StoreInfoBlockDeltaMakesValueNegative);
		info.AddDeletedDirectory(2);
		info.AddDeletedDirectory(3);
		info.AddDeletedDirectory(4);
		info.RemovedDeletedDirectory(3);
		info.SetAccountName("whee");
		TEST_CHECK_THROWS(info.RemovedDeletedDirectory(9),
			BackupStoreException, StoreInfoDirNotInList);
		fs.PutBackupStoreInfo(info);
	}

	{
		std::auto_ptr<BackupStoreInfo> info = fs.GetBackupStoreInfoUncached();
		TEST_EQUAL(7, info->GetBlocksUsed());
		TEST_EQUAL(5, info->GetBlocksInOldFiles());
		TEST_EQUAL(1, info->GetBlocksInDeletedFiles());
		TEST_EQUAL(3461231233455433LL, info->GetBlocksSoftLimit());
		TEST_EQUAL(2934852487LL, info->GetBlocksHardLimit());
		TEST_EQUAL("whee", info->GetAccountName());
		const std::vector<int64_t> &delfiles(info->GetDeletedDirectories());
		TEST_EQUAL(2, delfiles.size());
		TEST_EQUAL(2, delfiles[0]);
		TEST_EQUAL(4, delfiles[1]);
	}

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_login_without_account()
{
	// It doesn't make sense to specialise this test, because S3 filesystems don't have an
	// accounts database at all. And it's difficult because SetClientHasAccount is called by
	// BackupStoreDaemon (which has access to the accounts database) and specialised tests
	// normally use/ a local protocol, which bypasses BackupStoreDaemon.
	SETUP_TEST_UNIFIED();

	// Delete the account created by the test harness (0x01234567):
	delete_account();

	// Create another account, which doesn't match the SSL certificate:
	create_account(1000, 2000, 0x01234568);

	TEST_THAT_OR(StartServer(), FAIL);

	// BLOCK
	{
		// Open a connection to the server
		BackupProtocolClient protocol(open_conn("localhost", tls_context));

		// Check the version
		std::auto_ptr<BackupProtocolVersion> serverVersion(protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION));
		TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);

		// Try logging in with a nonexistent (deleted) account, which should fail:
		TEST_COMMAND_RETURNS_ERROR(protocol, QueryLogin(0x01234567, 0),
			Err_BadLogin);

		// Try to login as an account that doesn't match the SSL certificate CommonName
		TEST_COMMAND_RETURNS_ERROR(protocol, QueryLogin(0x01234568, 0),
			Err_BadLogin);

		// Finish the connection
		protocol.QueryFinished();
	}

	TEARDOWN_TEST_UNIFIED();
}

bool test_bbstoreaccounts_create(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// Delete the account, and create it again using bbstoreaccounts
	spec.control().DeleteAccount(false); // !AskForConfirmation

	// Release the lock to discard the filesystem's cached BackupStoreInfo:
	fs.ReleaseLock();

	TEST_THAT(run_bbstoreaccounts_specialised(spec.name(), "create", "",
		"test 10000B 20000B", "0 10000B 20000B"));

	// This code is almost exactly the same as tests3store.cpp:check_new_account_info()
	std::auto_ptr<BackupStoreInfo> info = fs.GetBackupStoreInfoUncached();
	if(spec.name() == "s3")
	{
		TEST_EQUAL(S3_FAKE_ACCOUNT_ID, info->GetAccountID());
		TEST_EQUAL(1, info->GetBlocksUsed());
		TEST_EQUAL(1, info->GetBlocksInDirectories());
		TEST_EQUAL("test", info->GetAccountName());
	}
	else
	{
		TEST_EQUAL(0x01234567, info->GetAccountID());
		TEST_EQUAL(2, info->GetBlocksUsed());
		TEST_EQUAL(2, info->GetBlocksInDirectories());
		TEST_EQUAL("", info->GetAccountName());
	}

	TEST_EQUAL(1, info->GetLastObjectIDUsed());
	TEST_EQUAL(0, info->GetBlocksInCurrentFiles());
	TEST_EQUAL(0, info->GetBlocksInOldFiles());
	TEST_EQUAL(0, info->GetBlocksInDeletedFiles());
	TEST_EQUAL(0, info->GetDeletedDirectories().size());
	TEST_EQUAL(10000, info->GetBlocksSoftLimit());
	TEST_EQUAL(20000, info->GetBlocksHardLimit());
	TEST_EQUAL(0, info->GetNumCurrentFiles());
	TEST_EQUAL(0, info->GetNumOldFiles());
	TEST_EQUAL(0, info->GetNumDeletedFiles());
	TEST_EQUAL(1, info->GetNumDirectories());
	TEST_EQUAL(true, info->IsAccountEnabled());
	TEST_EQUAL(true, info->IsReadOnly());
	TEST_EQUAL(0, info->GetClientStoreMarker());

	BackupStoreDirectory root_dir;
	fs.GetDirectory(BACKUPSTORE_ROOT_DIRECTORY_ID, root_dir);
	TEST_EQUAL(0, root_dir.GetNumberOfEntries());

	BackupStoreRefCountDatabase& refcount_db(fs.GetPermanentRefCountDatabase(true)); // ReadOnly
	TEST_EQUAL(1, refcount_db.GetRefCount(BACKUPSTORE_ROOT_DIRECTORY_ID));

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_bbstoreaccounts_delete(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// Release the lock so that we can run bbstoreaccounts:
	fs.ReleaseLock();

	TEST_THAT(run_bbstoreaccounts_specialised(spec.name(), "delete", "yes"));

	// Recreate the account so that teardown_test_backupstore() doesn't freak out
	TEST_THAT(create_test_account_specialised(spec.name(), spec.control()));

	TEARDOWN_TEST_SPECIALISED(spec);
}

// Test that login fails on a disabled account
bool test_login_with_disabled_account(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// Release the lock so that we can run bbstoreaccounts:
	fs.ReleaseLock();

	// The account is already enabled, but doing it again shouldn't hurt:
	TEST_THAT(run_bbstoreaccounts_specialised(spec.name(), "enabled", "yes"));

	// Check that we can log in
	{
		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly
	}

	TEST_THAT(run_bbstoreaccounts_specialised(spec.name(), "enabled", "no"));

	// BLOCK
	{
		// Open a local (virtual) connection to the server
		BackupStoreContext context(fs, NULL, "fake test connection");
		BackupProtocolLocal2 protocol(context, 0x01234567, false, // !read_only
			false); // !login

		// Login
		TEST_COMMAND_RETURNS_ERROR(protocol, QueryLogin(0x01234567, 0),
			Err_DisabledAccount);

		// Finish the connection
		protocol.QueryFinished();
	}

	// Re-enable the account, check that we can login again
	TEST_THAT(run_bbstoreaccounts_specialised(spec.name(), "enabled", "yes"));

	{
		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_login_with_no_refcount_db(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// Need to unlock the filesystem because it has an open file handle to the refcount DB, so
	// it won't notice if we delete the file unless we force it to close and attempt to reopen
	// that handle.
	fs.ReleaseLock();

	// Delete the refcount DB:
	if(spec.name() == "s3")
	{
		TEST_EQUAL(0, EMU_UNLINK("testfiles/store/subdir/" S3_REFCOUNT_FILE_NAME));
	}
	else
	{
		TEST_EQUAL(0, EMU_UNLINK("testfiles/0_0/backup/01234567/refcount.rdb.rfw"));
	}

	// Try to log in again. Check that we're locked out of the account until housekeeping (or
	// bbstoreaccounts check, in the case of s3 filesystems) has recreated the refcount db.
	TEST_CHECK_THROWS(
		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false), // !ReadOnly
		BackupStoreException, CorruptReferenceCountDatabase);

	// Run housekeeping or fix the store, and check that it fixes the refcount db:
	if(spec.name() == "s3")
	{
		TEST_EQUAL_LINE(1, check_account_and_fix_errors(fs),
			"Check should report 1 error if the refcount db is missing");
	}
	else
	{
		TEST_EQUAL_LINE(1, run_housekeeping(fs),
			"Housekeeping should report 1 error if the refcount db is missing");
	}

	// Check that housekeeping/check fix fixed the refcounts:
	TEST_THAT(check_reference_counts(
		fs.GetPermanentRefCountDatabase(true))); // ReadOnly

	// And that we can now log in:
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly

	// Test with a bbstored server, which doesn't make sense for S3 tests
	if(spec.name() == "store")
	{
		TEST_THAT(FileExists("testfiles/0_0/backup/01234567/refcount.rdb.rfw"));

		// Start a server and try again, remotely. This is difficult to debug
		// because housekeeping may fix the refcount database while we're
		// stepping through.
		TEST_THAT_THROWONFAIL(StartServer());

		// We need to close it first on Windows, otherwise we won't be able to delete it
		// because it's locked exclusively:
		fs.CloseRefCountDatabase(&fs.GetPermanentRefCountDatabase(true)); // ReadOnly

		TEST_EQUAL(0, EMU_UNLINK("testfiles/0_0/backup/01234567/refcount.rdb.rfw"));
		TEST_CHECK_THROWS(connect_and_login(tls_context),
			ConnectionException, Protocol_UnexpectedReply);

		TEST_THAT(ServerIsAlive(bbstored_pid));

		// Run housekeeping, check that it fixes the refcount db. We need to have closed
		// the permanent DB above, because housekeeping wants to open a new (potential) DB
		// and the filesystem won't let us have a read/write permanent and a potential DB
		// open at the same time.
		TEST_EQUAL_LINE(1, run_housekeeping(fs),
			"Housekeeping should report 1 error if the refcount db is missing");
		TEST_THAT(FileExists("testfiles/0_0/backup/01234567/refcount.rdb.rfw"));
		TEST_THAT(check_reference_counts());

		// And that we can log in afterwards
		connect_and_login(tls_context)->QueryFinished();
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

// Test the deletion of objects by the housekeeping system
bool test_housekeeping_deletes_files(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly

	// Create some nice recursive directories
	write_test_file(1);
	int64_t dirtodelete = create_test_data_subdirs(protocol,
		BACKUPSTORE_ROOT_DIRECTORY_ID, "test_delete", 6 /* depth */,
		NULL /* pRefCount */);

	TEST_EQUAL(dirtodelete,
		protocol.QueryDeleteDirectory(dirtodelete)->GetObjectID());
	assert_everything_deleted(protocol, dirtodelete);
	protocol.QueryFinished();

	// First, things as they are now.
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context_read_only, protocol_read_only,
		false); // !ReadOnly
	recursive_count_objects_results before = {0,0,0};
	recursive_count_objects(protocol_read_only, BACKUPSTORE_ROOT_DIRECTORY_ID, before);

	TEST_EQUAL(0, before.objectsNotDel);
	TEST_THAT(before.deleted != 0);
	TEST_THAT(before.old != 0);

	// Reduce the store limits, so housekeeping will remove all old files.
	// Leave the hard limit high, so we know that housekeeping's target
	// for freeing space is the soft limit.
	TEST_THAT(change_account_limits(spec.control(), "0B", "20000B"));
	TEST_THAT(run_housekeeping_and_check_account(fs));

	// Count the objects again
	recursive_count_objects_results after = {0,0,0};
	recursive_count_objects(protocol_read_only, BACKUPSTORE_ROOT_DIRECTORY_ID, after);
	TEST_EQUAL(before.objectsNotDel, after.objectsNotDel);
	TEST_EQUAL(0, after.deleted);
	TEST_EQUAL(0, after.old);

	// Adjust reference counts on deleted files, so that the final checks in
	// teardown_test_backupstore() don't fail.
	ExpectedRefCounts.resize(2);

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_account_limits_respected(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	BackupStoreContext rwContext(fs, NULL, // mpHousekeeping
		"fake test connection"); // rConnectionDetails
	BackupStoreContext roContext(fs, NULL, // mpHousekeeping
		"fake test connection"); // rConnectionDetails

	// Set a really small hard limit
	if(spec.name() == "s3")
	{
		spec.control().SetLimit("1B", "1B");
	}
	else
	{
		spec.control().SetLimit("2B", "2B");
	}

	// Try to upload a file and create a directory, both of which would exceed the
	// current account limits, and check that each command returns an error.
	{
		write_test_file(3);

		// Open a connection to the server
		std::auto_ptr<BackupProtocolLocal2> apProtocol(
			new BackupProtocolLocal2(rwContext, 0x01234567, false)); // !ReadOnly
		BackupStoreFilenameClear fnx("exceed-limit");
		int64_t modtime = 0;
		std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile("testfiles/test3", BACKUPSTORE_ROOT_DIRECTORY_ID, fnx, &modtime));
		TEST_THAT(modtime != 0);

		TEST_COMMAND_RETURNS_ERROR(*apProtocol,
			QueryStoreFile(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				modtime,
				modtime, /* use it for attr hash too */
				0, /* diff from ID */
				fnx,
				upload),
			Err_StorageLimitExceeded);

		// This currently causes a fatal error on the server, which
		// kills the connection. TODO FIXME return an error instead.
		std::auto_ptr<IOStream> attr(new MemBlockStream(&modtime, sizeof(modtime)));
		BackupStoreFilenameClear fnxd("exceed-limit-dir");
		TEST_COMMAND_RETURNS_ERROR(*apProtocol, 
			QueryCreateDirectory(
				BACKUPSTORE_ROOT_DIRECTORY_ID,
				FAKE_ATTR_MODIFICATION_TIME, fnxd, attr),
			Err_StorageLimitExceeded);

		// Finish the connection.
		apProtocol->QueryFinished();
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

bool test_open_files_with_limited_win32_permissions()
{
#ifdef WIN32
	// this had better work, or bbstored will die when combining diffs
	const char* file = "foo";

	DWORD accessRights = FILE_READ_ATTRIBUTES |
		FILE_LIST_DIRECTORY | FILE_READ_EA | FILE_WRITE_ATTRIBUTES |
		FILE_WRITE_DATA | FILE_WRITE_EA /*| FILE_ALL_ACCESS*/;
	DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

	HANDLE h1 = CreateFileA(file, accessRights, shareMode,
		NULL, OPEN_ALWAYS, // create file if it doesn't exist
		FILE_FLAG_BACKUP_SEMANTICS, NULL);
	TEST_THAT(h1 != INVALID_HANDLE_VALUE);

	accessRights = FILE_READ_ATTRIBUTES |
		FILE_LIST_DIRECTORY | FILE_READ_EA;

	HANDLE h2 = CreateFileA(file, accessRights, shareMode,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	TEST_THAT(h2 != INVALID_HANDLE_VALUE);

	CloseHandle(h2);
	CloseHandle(h1);

	h1 = openfile(file, O_CREAT | O_RDWR, 0);
	TEST_THAT(h1 != INVALID_HANDLE_VALUE);
	h2 = openfile(file, O_RDWR, 0);
	TEST_THAT(h2 != INVALID_HANDLE_VALUE);
	CloseHandle(h2);
	CloseHandle(h1);
#endif

	return true;
}

bool compare_backupstoreinfo_values_to_expected
(
	const std::string& test_phase,
	const info_StreamFormat_1& expected,
	const BackupStoreInfo& actual,
	const std::string& expected_account_name,
	bool expected_account_enabled,
	const MemBlockStream& extra_data = MemBlockStream(/* empty */)
)
{
	int num_failures_initial = num_failures;

	TEST_EQUAL_LINE(ntohl(expected.mAccountID), actual.GetAccountID(),
		test_phase << " AccountID");
	#define TEST_INFO_EQUAL(property) \
		TEST_EQUAL_LINE(box_ntoh64(expected.m ## property), \
			actual.Get ## property (), test_phase << " " #property)
	TEST_INFO_EQUAL(ClientStoreMarker);
	TEST_INFO_EQUAL(LastObjectIDUsed);
	TEST_INFO_EQUAL(BlocksUsed);
	TEST_INFO_EQUAL(BlocksInOldFiles);
	TEST_INFO_EQUAL(BlocksInDeletedFiles);
	TEST_INFO_EQUAL(BlocksInDirectories);
	TEST_INFO_EQUAL(BlocksSoftLimit);
	TEST_INFO_EQUAL(BlocksHardLimit);
	#undef TEST_INFO_EQUAL

	// These attributes of the v2 structure are not supported by v1,
	// so they should all be initialised to 0
	TEST_EQUAL_LINE(0, actual.GetBlocksInCurrentFiles(),
		test_phase << " BlocksInCurrentFiles");
	TEST_EQUAL_LINE(0, actual.GetNumOldFiles(),
		test_phase << " NumOldFiles");
	TEST_EQUAL_LINE(0, actual.GetNumDeletedFiles(),
		test_phase << " NumDeletedFiles");
	TEST_EQUAL_LINE(0, actual.GetNumDirectories(),
		test_phase << " NumDirectories");

	// These attributes of the old v1 structure are not actually loaded
	// or used:
	// TEST_INFO_EQUAL(CurrentMarkNumber);
	// TEST_INFO_EQUAL(OptionsPresent);

	TEST_EQUAL_LINE(box_ntoh64(expected.mNumberDeletedDirectories),
		actual.GetDeletedDirectories().size(),
		test_phase << " number of deleted directories");

	for (size_t i = 0; i < box_ntoh64(expected.mNumberDeletedDirectories) &&
		i < actual.GetDeletedDirectories().size(); i++)
	{
		TEST_EQUAL_LINE(13 + i, actual.GetDeletedDirectories()[i],
			test_phase << " deleted directory " << (i+1));
	}

	TEST_EQUAL_LINE(expected_account_name, actual.GetAccountName(),
		test_phase << " AccountName");
	TEST_EQUAL_LINE(expected_account_enabled, actual.IsAccountEnabled(),
		test_phase << " AccountEnabled");

	TEST_EQUAL_LINE(extra_data.GetSize(), actual.GetExtraData().GetSize(),
		test_phase << " extra data has wrong size");
	TEST_EQUAL_LINE(0, memcmp(extra_data.GetBuffer(),
		actual.GetExtraData().GetBuffer(), extra_data.GetSize()),
		test_phase << " extra data has wrong contents");

	return (num_failures == num_failures_initial);
}

bool test_read_old_backupstoreinfo_files()
{
	SETUP_TEST_UNIFIED();

	RaidBackupFileSystem fs(0x01234567, "backup/01234567/", 0);

	// Create an account for the test client
	std::auto_ptr<BackupStoreInfo> apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_EQUAL_LINE(true, apInfoReadOnly->IsAccountEnabled(),
		"'bbstoreaccounts create' should have set AccountEnabled flag");

	info_StreamFormat_1 info_v1;
	info_v1.mMagicValue = htonl(INFO_MAGIC_VALUE_1);
	info_v1.mAccountID = htonl(0x01234567);
	info_v1.mClientStoreMarker = box_hton64(3);
	info_v1.mLastObjectIDUsed = box_hton64(4);
	info_v1.mBlocksUsed = box_hton64(5);
	info_v1.mBlocksInOldFiles = box_hton64(6);
	info_v1.mBlocksInDeletedFiles = box_hton64(7);
	info_v1.mBlocksInDirectories = box_hton64(8);
	info_v1.mBlocksSoftLimit = box_hton64(9);
	info_v1.mBlocksHardLimit = box_hton64(10);
	info_v1.mCurrentMarkNumber = htonl(11);
	info_v1.mOptionsPresent = htonl(12);
	info_v1.mNumberDeletedDirectories = box_hton64(2);
	// Then mNumberDeletedDirectories * int64_t IDs for the deleted directories

	// Generate the filename
	std::string info_filename("backup/01234567/" INFO_FILENAME);
	std::auto_ptr<RaidFileWrite> rfw(new RaidFileWrite(0, info_filename));
	rfw->Open(/* AllowOverwrite = */ true);
	rfw->Write(&info_v1, sizeof(info_v1));
	// Write mNumberDeletedDirectories * int64_t IDs for the deleted directories
	std::auto_ptr<Archive> apArchive(new Archive(*rfw, IOStream::TimeOutInfinite));
	apArchive->Write((int64_t) 13);
	apArchive->Write((int64_t) 14);
	rfw->Commit(/* ConvertToRaidNow */ true);
	rfw.reset();

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("loaded from v1", info_v1,
			*apInfoReadOnly, "" /* no name by default */,
			true /* enabled by default */));

	BackupStoreInfo* pInfoReadWrite =
		&(fs.GetBackupStoreInfo(false, true)); // !ReadOnly, Refresh
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("loaded from v1", info_v1,
			*pInfoReadWrite, "" /* no name by default */,
			true /* enabled by default */));

	// Save the info again, with a new account name
	pInfoReadWrite->SetAccountName("bonk");
	fs.PutBackupStoreInfo(*pInfoReadWrite);

	// Check that it was saved in the new Archive format
	std::auto_ptr<RaidFileRead> rfr(RaidFileRead::Open(0, info_filename, 0));
	int32_t magic;
	if(!rfr->ReadFullBuffer(&magic, sizeof(magic), 0))
	{
		THROW_FILE_ERROR("Failed to read store info file: "
			"short read of magic number", info_filename,
			BackupStoreException, CouldNotLoadStoreInfo);
	}
	TEST_EQUAL_LINE(INFO_MAGIC_VALUE_2, ntohl(magic),
		"format version in newly saved BackupStoreInfo");
	rfr.reset();

	// load it, and check that all values are loaded properly
	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected(
			"loaded in v1, resaved in v2",
			info_v1, *apInfoReadOnly, "bonk", true));

	// Check that the new AccountEnabled flag is saved properly
	pInfoReadWrite->SetAccountEnabled(false);
	fs.PutBackupStoreInfo(*pInfoReadWrite);

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("saved in v2, loaded in v2",
			info_v1, *apInfoReadOnly, "bonk", false /* as modified above */));

	pInfoReadWrite->SetAccountEnabled(true);
	fs.PutBackupStoreInfo(*pInfoReadWrite);

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("resaved in v2 with "
			"account enabled", info_v1, *apInfoReadOnly, "bonk",
			true /* as modified above */));

	// Now save the info in v2 format without the AccountEnabled flag
	// (boxbackup 0.11 format) and check that the flag is set to true
	// for backwards compatibility

	rfw.reset(new RaidFileWrite(0, info_filename));
	rfw->Open(/* allowOverwrite */ true);
	magic = htonl(INFO_MAGIC_VALUE_2);
	apArchive.reset(new Archive(*rfw, IOStream::TimeOutInfinite));
	rfw->Write(&magic, sizeof(magic));
	apArchive->Write(apInfoReadOnly->GetAccountID());
	apArchive->Write(std::string("test"));
	apArchive->Write(apInfoReadOnly->GetClientStoreMarker());
	apArchive->Write(apInfoReadOnly->GetLastObjectIDUsed());
	apArchive->Write(apInfoReadOnly->GetBlocksUsed());
	apArchive->Write(apInfoReadOnly->GetBlocksInCurrentFiles());
	apArchive->Write(apInfoReadOnly->GetBlocksInOldFiles());
	apArchive->Write(apInfoReadOnly->GetBlocksInDeletedFiles());
	apArchive->Write(apInfoReadOnly->GetBlocksInDirectories());
	apArchive->Write(apInfoReadOnly->GetBlocksSoftLimit());
	apArchive->Write(apInfoReadOnly->GetBlocksHardLimit());
	apArchive->Write(apInfoReadOnly->GetNumCurrentFiles());
	apArchive->Write(apInfoReadOnly->GetNumOldFiles());
	apArchive->Write(apInfoReadOnly->GetNumDeletedFiles());
	apArchive->Write(apInfoReadOnly->GetNumDirectories());
	apArchive->Write((int64_t) apInfoReadOnly->GetDeletedDirectories().size());
	apArchive->Write(apInfoReadOnly->GetDeletedDirectories()[0]);
	apArchive->Write(apInfoReadOnly->GetDeletedDirectories()[1]);
	rfw->Commit(/* ConvertToRaidNow */ true);
	rfw.reset();

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("saved in v2 without "
			"AccountEnabled", info_v1, *apInfoReadOnly, "test", true));
	// Default for missing AccountEnabled should be true

	pInfoReadWrite = &(fs.GetBackupStoreInfo(false, true)); // !ReadOnly, Refresh
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("saved in v2 without "
			"AccountEnabled", info_v1, *pInfoReadWrite, "test", true));

	// Rewrite using full length, so that the first 4 bytes of extra data
	// doesn't get swallowed by "extra data".
	fs.PutBackupStoreInfo(*pInfoReadWrite);

	// Append some extra data after the known account values, to simulate a
	// new addition to the store format. Check that this extra data is loaded
	// and resaved with the info file. We made the mistake of deleting it in
	// 0.11, let's not make the same mistake again.
	CollectInBufferStream info_data;
	rfr = RaidFileRead::Open(0, info_filename, 0);
	rfr->CopyStreamTo(info_data);
	rfr.reset();
	info_data.SetForReading();
	rfw.reset(new RaidFileWrite(0, info_filename));
	rfw->Open(/* allowOverwrite */ true);
	info_data.CopyStreamTo(*rfw);
	char extra_string[] = "hello!";
	MemBlockStream extra_data(extra_string, strlen(extra_string));
	extra_data.CopyStreamTo(*rfw);
	rfw->Commit(/* ConvertToRaidNow */ true);
	rfw.reset();

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_EQUAL_LINE(extra_data.GetSize(), apInfoReadOnly->GetExtraData().GetSize(),
		"wrong amount of extra data loaded from info file");
	TEST_EQUAL_LINE(0, memcmp(extra_data.GetBuffer(),
		apInfoReadOnly->GetExtraData().GetBuffer(), extra_data.GetSize()),
		"extra data loaded from info file has wrong contents");

	// Save the file and load again, check that the extra data is still there
	pInfoReadWrite = &(fs.GetBackupStoreInfo(false, true)); // !ReadOnly, Refresh
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("saved in future format "
			"with extra_data", info_v1, *pInfoReadWrite, "test", true,
			extra_data));
	fs.PutBackupStoreInfo(*pInfoReadWrite);

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("saved in future format "
			"with extra_data", info_v1, *apInfoReadOnly, "test", true,
			extra_data));

	// Check that the new bbstoreaccounts command sets the flag properly
	TEST_THAT_OR(::system(BBSTOREACCOUNTS
		" -c testfiles/bbstored.conf enabled 01234567 no") == 0, FAIL);
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_EQUAL_LINE(false, apInfoReadOnly->IsAccountEnabled(),
		"'bbstoreaccounts disabled no' should have reset AccountEnabled flag");
	TEST_THAT_OR(::system(BBSTOREACCOUNTS
		" -c testfiles/bbstored.conf enabled 01234567 yes") == 0, FAIL);
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_EQUAL_LINE(true, apInfoReadOnly->IsAccountEnabled(),
		"'bbstoreaccounts disabled yes' should have set AccountEnabled flag");

	// Check that BackupStoreInfo::CreateForRegeneration saves all the
	// expected properties, including any extra data for forward
	// compatibility
	extra_data.Seek(0, IOStream::SeekType_Absolute);
	apInfoReadOnly = BackupStoreInfo::CreateForRegeneration(
		apInfoReadOnly->GetAccountID(), "spurtle" /* rAccountName */,
		apInfoReadOnly->GetLastObjectIDUsed(),
		apInfoReadOnly->GetBlocksUsed(),
		apInfoReadOnly->GetBlocksInCurrentFiles(),
		apInfoReadOnly->GetBlocksInOldFiles(),
		apInfoReadOnly->GetBlocksInDeletedFiles(),
		apInfoReadOnly->GetBlocksInDirectories(),
		apInfoReadOnly->GetBlocksSoftLimit(),
		apInfoReadOnly->GetBlocksHardLimit(),
		false /* AccountEnabled */,
		extra_data);
	// CreateForRegeneration always sets the ClientStoreMarker to 0
	info_v1.mClientStoreMarker = 0;
	// CreateForRegeneration does not store any deleted directories
	info_v1.mNumberDeletedDirectories = 0;

	// check that the store info has the correct values in memory
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("stored by "
			"BackupStoreInfo::CreateForRegeneration", info_v1,
			*apInfoReadOnly, "spurtle", false /* AccountEnabled */,
			extra_data));
	// Save the file and load again, check that the extra data is still there
	fs.PutBackupStoreInfo(*apInfoReadOnly);

	apInfoReadOnly = fs.GetBackupStoreInfoUncached();
	TEST_THAT(
		compare_backupstoreinfo_values_to_expected("saved by "
			"BackupStoreInfo::CreateForRegeneration and reloaded", info_v1,
			*apInfoReadOnly, "spurtle", false /* AccountEnabled */,
			extra_data));

	// Delete the account to stop teardown_test_backupstore checking it for errors.
	apInfoReadOnly.reset();
	TEST_THAT(delete_account());

	TEARDOWN_TEST_UNIFIED();
}

// Test that attributes can be correctly read from and written to the standard
// format, for compatibility with other servers and clients. See
// http://mailman.uk.freebsd.org/pipermail../public/boxbackup/2010-November/005818.html and
// http://lists.boxbackup.org/pipermail/boxbackup/2011-February/005978.html for
// details of the problems with packed structs.
bool test_read_write_attr_streamformat()
{
	SETUP_TEST_UNIFIED();

	// Construct a minimal valid directory with 1 entry in memory using Archive, and
	// try to read it back.
	CollectInBufferStream buf;
	Archive darc(buf, IOStream::TimeOutInfinite);

	// Write a dir_StreamFormat structure
	darc.Write((int32_t)OBJECTMAGIC_DIR_MAGIC_VALUE); // mMagicValue
	darc.Write((int32_t)1); // mNumEntries
	darc.Write((int64_t)0x0123456789abcdef); // mObjectID
	darc.Write((int64_t)0x0000000000000001); // mContainerID
	darc.Write((uint64_t)0x23456789abcdef01); // mAttributesModTime
	darc.Write((int32_t)BackupStoreDirectory::Option_DependencyInfoPresent);
	// mOptionsPresent
	// 36 bytes to here

	// Write fake attributes to make it valid.
	StreamableMemBlock attr;
	attr.WriteToStream(buf);
	// 40 bytes to here

	// Write a single entry in an en_StreamFormat structure
	darc.Write((uint64_t)0x3456789012345678); // mModificationTime
	darc.Write((int64_t)0x0000000000000002); // mObjectID
	darc.Write((int64_t)0x0000000000000003); // mSizeInBlocks
	darc.Write((uint64_t)0x0000000000000004); // mAttributesHash
	darc.WriteInt16((int16_t)0x3141); // mFlags
	// 74 bytes to here

	// Then a BackupStoreFilename
	BackupStoreFilename fn;
	fn.SetAsClearFilename("hello");
	fn.WriteToStream(buf);
	// 81 bytes to here

	// Then a StreamableMemBlock for attributes
	attr.WriteToStream(buf);
	// 85 bytes to here

	// Then an en_StreamFormatDepends for dependency info.
	darc.Write((uint64_t)0x0000000000000005); // mDependsOnObject
	darc.Write((uint64_t)0x0000000000000006); // mRequiredByObject
	// 101 bytes to here

	// Make sure none of the fields was expanded in transit by Archive.
	TEST_EQUAL(101, buf.GetSize());

	buf.SetForReading();
	BackupStoreDirectory dir(buf);

	TEST_EQUAL(1, dir.GetNumberOfEntries());
	TEST_EQUAL(0x0123456789abcdef, dir.GetObjectID());
	TEST_EQUAL(0x0000000000000001, dir.GetContainerID());
	TEST_EQUAL(0x23456789abcdef01, dir.GetAttributesModTime());

	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry* pen = i.Next();
	TEST_THAT_OR(pen != NULL, FAIL);
	TEST_EQUAL(0x3456789012345678, pen->GetModificationTime());
	TEST_EQUAL(0x0000000000000002, pen->GetObjectID());
	TEST_EQUAL(0x0000000000000003, pen->GetSizeInBlocks());
	TEST_EQUAL(0x0000000000000004, pen->GetAttributesHash());
	TEST_EQUAL(0x0000000000000005, pen->GetDependsOnObject());
	TEST_EQUAL(0x0000000000000006, pen->GetRequiredByObject());
	TEST_EQUAL(0x3141, pen->GetFlags());

	CollectInBufferStream buf2;
	dir.WriteToStream(buf2);
	buf2.SetForReading();
	buf.Seek(0, IOStream::SeekType_Absolute);
	TEST_EQUAL(101, buf2.GetSize());
	TEST_EQUAL(buf.GetSize(), buf2.GetSize());

	// Test that the stream written out for the Directory is exactly the same as the
	// one we hand-crafted earlier.
	TEST_EQUAL(0, memcmp(buf.GetBuffer(), buf2.GetBuffer(), buf.GetSize()));

	TEARDOWN_TEST_UNIFIED();
}

bool test_s3backupfilesystem(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_UNIFIED();

	// Test that S3BackupFileSystem returns a RevisionID based on the ETag (MD5
	// checksum) of the file.
	// rand() is platform-specific, so we can't rely on it to generate files with a
	// particular ETag, so we write the file ourselves instead.
	{
		FileStream fs("testfiles/store/subdir/0.file", O_CREAT | O_WRONLY | O_BINARY);
		for(int i = 0; i < 455; i++)
		{
			char c = (char)i;
			fs.Write(&c, 1);
		}
	}

	const Configuration s3config = spec.config().GetSubConfiguration("S3Store");
	S3Client client(s3config);
	HTTPResponse response = client.HeadObject("/subdir/0.file");
	client.CheckResponse(response, "Failed to get file /subdir/0.file");

	std::string etag = response.GetHeaderValue("etag");
	TEST_EQUAL("\"447baac70b0149224b4f48daedf5266f\"", etag);

	S3BackupFileSystem fs(s3config, client);
	int64_t revision_id = 0, expected_id = 0x447baac70b014922;
	TEST_THAT(fs.ObjectExists(0, &revision_id));
	TEST_EQUAL(expected_id, revision_id);

	TEARDOWN_TEST_UNIFIED();
}

// Test that the S3 backend correctly locks and unlocks the store using SimpleDB.
bool test_simpledb_locking(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_UNIFIED();

	const Configuration s3config = spec.config().GetSubConfiguration("S3Store");
	S3Client s3client(s3config);
	SimpleDBClient client(s3config);

	// There should be no locks at the beginning. In fact the domain should not even
	// exist: the client should create it itself.
	std::vector<std::string> expected_domains;
	TEST_THAT(test_equal_lists(expected_domains, client.ListDomains()));

	SimpleDBClient::str_map_t expected;

	// Create a client in a scope, so it will be destroyed when the scope ends.
	{
		S3BackupFileSystem fs(s3config, s3client);

		// Check that it hasn't acquired a lock yet.
		TEST_CHECK_THROWS(
			client.GetAttributes("boxbackup_locks", "localhost/subdir/"),
			HTTPException, SimpleDBItemNotFound);

		box_time_t before = GetCurrentBoxTime();
		// If this fails, it will throw an exception:
		fs.GetLock();
		box_time_t after = GetCurrentBoxTime();

		// Check that it has now acquired a lock.
		SimpleDBClient::str_map_t attributes =
			client.GetAttributes("boxbackup_locks", "localhost/subdir/");
		expected["locked"] = "true";

		std::ostringstream locker_buf;
		locker_buf << fs.GetCurrentUserName() << "@" << fs.GetCurrentHostName() <<
			"(" << getpid() << ")";
		TEST_EQUAL(locker_buf.str(), fs.GetSimpleDBLockValue());
		expected["locker"] = locker_buf.str();

		std::ostringstream pid_buf;
		pid_buf << getpid();
		expected["pid"] = pid_buf.str();

		char hostname_buf[1024];
		TEST_EQUAL(0, gethostname(hostname_buf, sizeof(hostname_buf)));
		TEST_EQUAL(hostname_buf, fs.GetCurrentHostName());
		expected["hostname"] = hostname_buf;

		TEST_LINE(fs.GetSinceTime() >= before, "Lock was apparently taken at " <<
			FormatTime(fs.GetSinceTime(), false, true) << " which should be after " <<
			FormatTime(before, false, true) << " but was not");
		TEST_LINE(fs.GetSinceTime() <= after, "Lock was apparently taken at " <<
			FormatTime(fs.GetSinceTime(), false, true) << " which should be before " <<
			FormatTime(after, false, true) << " but was not");

		std::ostringstream since_buf;
		since_buf << fs.GetSinceTime();
		expected["since"] = since_buf.str();

		TEST_THAT(test_equal_maps(expected, attributes));

		// Try to acquire another one, check that it fails.
		S3BackupFileSystem fs2(s3config, s3client);
		TEST_CHECK_THROWS(
			fs2.GetLock(),
			BackupStoreException, CouldNotLockStoreAccount);

		// And that the lock was not disturbed
		TEST_THAT(test_equal_maps(expected, attributes));
	}

	// Check that when the S3BackupFileSystem went out of scope, it released the lock
	expected["locked"] = "";
	{
		SimpleDBClient::str_map_t attributes =
			client.GetAttributes("boxbackup_locks", "localhost/subdir/");
		TEST_THAT(test_equal_maps(expected, attributes));
	}

	// And that we can acquire it again:
	{
		S3BackupFileSystem fs(s3config, s3client);
		fs.GetLock();

		expected["locked"] = "true";
		std::ostringstream since_buf;
		since_buf << fs.GetSinceTime();
		expected["since"] = since_buf.str();

		SimpleDBClient::str_map_t attributes =
			client.GetAttributes("boxbackup_locks", "localhost/subdir/");
		TEST_THAT(test_equal_maps(expected, attributes));
	}

	// And release it again:
	expected["locked"] = "";
	{
		SimpleDBClient::str_map_t attributes =
			client.GetAttributes("boxbackup_locks", "localhost/subdir/");
		TEST_THAT(test_equal_maps(expected, attributes));
	}

	TEARDOWN_TEST_UNIFIED();
}

int test(int argc, const char *argv[])
{
	TEST_THAT(test_open_files_with_limited_win32_permissions());

	// Initialise the raid file controller
	RaidFileController &rcontroller = RaidFileController::GetController();
	rcontroller.Initialise("testfiles/raidfile.conf");

	TEST_THAT(test_read_old_backupstoreinfo_files());

	// SSL library
	SSLLib::Initialise();

	// Use the setup crypto command to set up all these keys, so that the bbackupquery command can be used
	// for seeing what's going on.
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	// encode in some filenames -- can't do static initialisation
	// because the key won't be set up when these are initialised
	{
		MEMLEAKFINDER_NO_LEAKS

		for(unsigned int l = 0; l < sizeof(ens_filenames) / sizeof(ens_filenames[0]); ++l)
		{
			ens[l].fn = BackupStoreFilenameClear(ens_filenames[l]);
		}
		for(unsigned int l = 0; l < sizeof(uploads_filenames) / sizeof(uploads_filenames[0]); ++l)
		{
			uploads[l].name = BackupStoreFilenameClear(uploads_filenames[l]);
		}
	}

	// Trace errors out
	SET_DEBUG_SSLLIB_TRACE_ERRORS

	{
		R250 r(3465657);
		for(int l = 0; l < ATTR1_SIZE; ++l) {attr1[l] = r.next();}
		for(int l = 0; l < ATTR2_SIZE; ++l) {attr2[l] = r.next();}
		for(int l = 0; l < ATTR3_SIZE; ++l) {attr3[l] = r.next();}
	}

	tls_context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	std::auto_ptr<RaidAndS3TestSpecs> specs(
		new RaidAndS3TestSpecs(DEFAULT_BBACKUPD_CONFIG_FILE));

	TEST_THAT(kill_running_daemons());
	TEST_THAT(StartSimulator());

	TEST_THAT(test_s3backupfilesystem(specs->s3()));
	TEST_THAT(test_simpledb_locking(specs->s3()));

	TEST_THAT(test_filename_encoding());
	TEST_THAT(test_refcount_db_versions());
	TEST_THAT(test_temporary_refcount_db_is_independent());
	TEST_THAT(test_backupstore_directory());

	// Run all tests that take a RaidAndS3TestSpecs::Specialisation argument twice, once with
	// each specialisation that we have (S3 and BackupStore).

	for(auto i = specs->specs().begin(); i != specs->specs().end(); i++)
	{
		TEST_THAT(test_bbstoreaccounts_create(*i));
		TEST_THAT(test_bbstoreaccounts_delete(*i));
		TEST_THAT(test_directory_parent_entry_tracks_directory_size(*i));
		TEST_THAT(test_cannot_open_multiple_writable_connections(*i));
	}

	TEST_THAT(test_file_encoding());
	TEST_THAT(test_symlinks());
	TEST_THAT(test_login_without_account());

	for(auto i = specs->specs().begin(); i != specs->specs().end(); i++)
	{
		TEST_THAT(test_store_info(*i));
		TEST_THAT(test_server_housekeeping(*i));
		TEST_THAT(test_multiple_uploads(*i));
		TEST_THAT(test_server_commands(*i));
		TEST_THAT(test_account_limits_respected(*i));
		TEST_THAT(test_login_with_disabled_account(*i));
		TEST_THAT(test_login_with_no_refcount_db(*i));
		TEST_THAT(test_housekeeping_deletes_files(*i));
	}

	TEST_THAT(test_read_write_attr_streamformat());

	// Release lock before shutting down the simulator:
	specs.reset();
	TEST_THAT(StopSimulator());

	return finish_test_suite();
}
