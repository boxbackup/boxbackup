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

#include "Test.h"
#include "autogen_BackupProtocolClient.h"
#include "SSLLib.h"
#include "TLSContext.h"
#include "SocketStreamTLS.h"
#include "BoxPortsAndFiles.h"
#include "BackupStoreConstants.h"
#include "Socket.h"
#include "BackupStoreFilenameClear.h"
#include "CollectInBufferStream.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "FileStream.h"
#include "RaidFileController.h"
#include "RaidFileWrite.h"
#include "BackupStoreInfo.h"
#include "BackupStoreException.h"
#include "RaidFileException.h"
#include "MemBlockStream.h"
#include "BackupClientFileAttributes.h"
#include "BackupClientCryptoKeys.h"
#include "ServerControl.h"

#include "MemLeakFindOn.h"


#define ENCFILE_SIZE	2765

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
	char *fnextra;
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

int test1(int argc, const char *argv[])
{
	// Initialise the raid file controller
	RaidFileController &rcontroller = RaidFileController::GetController();
	rcontroller.Initialise("testfiles/raidfile.conf");

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
		TEST_THAT(fn2.find("name") == fn2.npos);
			
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
	return 0;
}

int test2(int argc, const char *argv[])
{
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
			BackupStoreDirectory dir2;
			dir2.ReadFromStream(stream, IOStream::TimeOutInfinite);
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
			BackupStoreDirectory dir2;
			dir2.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(dir2.GetNumberOfEntries() == DIR_FILES);
			CheckEntries(dir2, BackupStoreDirectory::Entry::Flags_File, BackupStoreDirectory::Entry::Flags_EXCLUDE_NOTHING);
		}
		{
			CollectInBufferStream stream;
			dir1.WriteToStream(stream, BackupStoreDirectory::Entry::Flags_INCLUDE_EVERYTHING, BackupStoreDirectory::Entry::Flags_File);
			stream.SetForReading();
			BackupStoreDirectory dir2;
			dir2.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(dir2.GetNumberOfEntries() == DIR_DIRS);
			CheckEntries(dir2, BackupStoreDirectory::Entry::Flags_Dir, BackupStoreDirectory::Entry::Flags_EXCLUDE_NOTHING);
		}
		{
			CollectInBufferStream stream;
			dir1.WriteToStream(stream, BackupStoreDirectory::Entry::Flags_File, BackupStoreDirectory::Entry::Flags_OldVersion);
			stream.SetForReading();
			BackupStoreDirectory dir2;
			dir2.ReadFromStream(stream, IOStream::TimeOutInfinite);
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
			BackupStoreDirectory dir2;
			dir2.ReadFromStream(stream, IOStream::TimeOutInfinite);
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
			BackupStoreDirectory d2;
			d2.ReadFromStream(stream, IOStream::TimeOutInfinite);
			TEST_THAT(d2.GetAttributes() == attr);
			TEST_THAT(d2.GetAttributesModTime() == 56234987324232LL);
		}
	}
	return 0;
}

void write_test_file(int t)
{
	std::string filename("testfiles/test");
	filename += uploads[t].fnextra;
	printf("%s\n", filename.c_str());
	
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
	BackupStoreFile::DecodeFile(rStream, "testfiles/test_download", IOStream::TimeOutInfinite);
	
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
	TEST_THAT(unlink("testfiles/test_download") == 0);
}

void test_everything_deleted(BackupProtocolClient &protocol, int64_t DirID)
{
	printf("Test for del: %llx\n", DirID);
	
	// Command
	std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocol.QueryListDirectory(
			DirID,
			BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
	// Stream
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
	dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
	
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	int files = 0;
	int dirs = 0;
	while((en = i.Next()) != 0)
	{
		if(en->GetFlags() & BackupProtocolClientListDirectory::Flags_Dir)
		{
			dirs++;
			// Recurse
			test_everything_deleted(protocol, en->GetObjectID());
		}
		else
		{
			files++;
		}
		// Check it's deleted
		TEST_THAT(en->GetFlags() & BackupProtocolClientListDirectory::Flags_Deleted);
	}
	
	// Check there were the right number of files and directories
	TEST_THAT(files == 3);
	TEST_THAT(dirs == 0 || dirs == 2);
}

int64_t create_test_data_subdirs(BackupProtocolClient &protocol, int64_t indir, const char *name, int depth)
{
	// Create a directory
	int64_t subdirid = 0;
	BackupStoreFilenameClear dirname(name);
	{
		// Create with dummy attributes
		int attrS = 0;
		MemBlockStream attr(&attrS, sizeof(attrS));
		std::auto_ptr<BackupProtocolClientSuccess> dirCreate(protocol.QueryCreateDirectory(
			indir,
			9837429842987984LL, dirname, attr));
		subdirid = dirCreate->GetObjectID(); 
	}
	
	printf("Create subdirs, depth = %d, dirid = %llx\n", depth, subdirid);
	
	// Put more directories in it, if we haven't gone down too far
	if(depth > 0)
	{
		create_test_data_subdirs(protocol, subdirid, "dir_One", depth - 1);
		create_test_data_subdirs(protocol, subdirid, "dir_Two", depth - 1);
	}
	
	// Stick some files in it
	{
		BackupStoreFilenameClear name("file_One");
		std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile("testfiles/file1", subdirid, name));
		std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
			subdirid,
			0x123456789abcdefLL,		/* modification time */
			0x7362383249872dfLL,		/* attr hash */
			0,							/* diff from ID */
			name,
			*upload));
	}
	{
		BackupStoreFilenameClear name("file_Two");
		std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile("testfiles/file1", subdirid, name));
		std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
			subdirid,
			0x123456789abcdefLL,		/* modification time */
			0x7362383249872dfLL,		/* attr hash */
			0,							/* diff from ID */
			name,
			*upload));
	}
	{
		BackupStoreFilenameClear name("file_Three");
		std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile("testfiles/file1", subdirid, name));
		std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
			subdirid,
			0x123456789abcdefLL,		/* modification time */
			0x7362383249872dfLL,		/* attr hash */
			0,							/* diff from ID */
			name,
			*upload));
	}

	return subdirid;
}


void check_dir_after_uploads(BackupProtocolClient &protocol, const StreamableMemBlock &Attributes)
{
	// Command
	std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocol.QueryListDirectory(
			BackupProtocolClientListDirectory::RootDirectory,
			BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
	TEST_THAT(dirreply->GetObjectID() == BackupProtocolClientListDirectory::RootDirectory);
	// Stream
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
	dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
	TEST_THAT(dir.GetNumberOfEntries() == UPLOAD_NUM + 1 /* for the first test file */);
	TEST_THAT(!dir.HasAttributes());

	// Check them!
	BackupStoreDirectory::Iterator i(dir);
	// Discard first
	BackupStoreDirectory::Entry *en = i.Next();
	TEST_THAT(en != 0);

	for(int t = 0; t < UPLOAD_NUM; ++t)
	{
		en = i.Next();
		TEST_THAT(en != 0);
		TEST_THAT(en->GetName() == uploads[t].name);
		TEST_THAT(en->GetObjectID() == uploads[t].allocated_objid);
		TEST_THAT(en->GetModificationTime() == uploads[t].mod_time);
		int correct_flags = BackupProtocolClientListDirectory::Flags_File;
		if(uploads[t].should_be_old_version) correct_flags |= BackupProtocolClientListDirectory::Flags_OldVersion;
		if(uploads[t].delete_file) correct_flags |= BackupProtocolClientListDirectory::Flags_Deleted;
		TEST_THAT(en->GetFlags() == correct_flags);
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

void recursive_count_objects_r(BackupProtocolClient &protocol, int64_t id, recursive_count_objects_results &results)
{
	// Command
	std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocol.QueryListDirectory(
			id,
			BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
			BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
	// Stream
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
	dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);

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

void recursive_count_objects(const char *hostname, int64_t id, recursive_count_objects_results &results)
{
	// Context
	TLSContext context;
	context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	// Get a connection
	SocketStreamTLS connReadOnly;
	connReadOnly.Open(context, Socket::TypeINET, hostname, BOX_PORT_BBSTORED);
	BackupProtocolClient protocolReadOnly(connReadOnly);

	{
		std::auto_ptr<BackupProtocolClientVersion> serverVersion(protocolReadOnly.QueryVersion(BACKUP_STORE_SERVER_VERSION));
		TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);
		std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocolReadOnly.QueryLogin(0x01234567, BackupProtocolClientLogin::Flags_ReadOnly));
	}
	
	// Count objects
	recursive_count_objects_r(protocolReadOnly, id, results);

	// Close it
	protocolReadOnly.QueryFinished();
}

bool check_block_index(const char *encoded_file, IOStream &rBlockIndex)
{
	// Open file, and move to the right position
	FileStream enc(encoded_file);
	BackupStoreFile::MoveStreamPositionToBlockIndex(enc);

	bool same = true;

	// Now compare the two...
	while(enc.StreamDataLeft())
	{
		char buffer1[2048];
		char buffer2[2048];
		int s = enc.Read(buffer1, sizeof(buffer1));
		if(rBlockIndex.Read(buffer2, s) != s)
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
	
	if(rBlockIndex.StreamDataLeft())
	{
		same = false;
		
		// Absorb all this excess data so procotol is in the first state
		char buffer[2048];
		while(rBlockIndex.StreamDataLeft())
		{
			rBlockIndex.Read(buffer, sizeof(buffer));
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


void test_server_1(BackupProtocolClient &protocol, BackupProtocolClient &protocolReadOnly)
{
	int encfile[ENCFILE_SIZE];
	{
		for(int l = 0; l < ENCFILE_SIZE; ++l)
		{
			encfile[l] = l * 173;
		}

		// Write this to a file
		{
			FileStream f("testfiles/file1", O_WRONLY | O_CREAT | O_EXCL);
			f.Write(encfile, sizeof(encfile));
		}
		
	}

	// Read the root directory a few times (as it's cached, so make sure it doesn't hurt anything)
	for(int l = 0; l < 3; ++l)
	{
		// Command
		std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocol.QueryListDirectory(
				BackupProtocolClientListDirectory::RootDirectory,
				BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
				BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
		// Stream
		BackupStoreDirectory dir;
		std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
		dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
		TEST_THAT(dir.GetNumberOfEntries() == 0);
	}

	// Read the dir from the readonly connection (make sure it gets in the cache)
	{
		// Command
		std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
				BackupProtocolClientListDirectory::RootDirectory,
				BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
				BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
		// Stream
		BackupStoreDirectory dir;
		std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
		dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
		TEST_THAT(dir.GetNumberOfEntries() == 0);			
	}

	// Store a file -- first make the encoded file
	BackupStoreFilenameClear store1name("testfiles/file1");
	{
		FileStream out("testfiles/file1_upload1", O_WRONLY | O_CREAT | O_EXCL);
		std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/file1", BackupProtocolClientListDirectory::RootDirectory, store1name));
		encoded->CopyStreamTo(out);
	}

//	printf("SKIPPING\n");
//	goto skip; {
	// Then send it
	int64_t store1objid = 0;
	{
		FileStream upload("testfiles/file1_upload1");
		std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
			BackupProtocolClientListDirectory::RootDirectory,
			0x123456789abcdefLL,		/* modification time */
			0x7362383249872dfLL,		/* attr hash */
			0,							/* diff from ID */
			store1name,
			upload));
		store1objid = stored->GetObjectID();
		TEST_THAT(store1objid == 2);
	}
	// And retrieve it
	{
		// Retrieve as object
		std::auto_ptr<BackupProtocolClientSuccess> getfile(protocol.QueryGetObject(store1objid));
		TEST_THAT(getfile->GetObjectID() == store1objid);
		// BLOCK
		{
			// Get stream
			std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
			// Need to put it in another stream, because it's not in stream order
			CollectInBufferStream f;
			filestream->CopyStreamTo(f);
			f.SetForReading();
			// Get and decode
			BackupStoreFile::DecodeFile(f, "testfiles/file1_upload_retrieved", IOStream::TimeOutInfinite);
		}

		// Retrieve as file
		std::auto_ptr<BackupProtocolClientSuccess> getobj(protocol.QueryGetFile(BackupProtocolClientListDirectory::RootDirectory, store1objid));
		TEST_THAT(getobj->GetObjectID() == store1objid);
		// BLOCK
		{
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
			std::auto_ptr<BackupProtocolClientSuccess> getblockindex(protocol.QueryGetBlockIndexByID(store1objid));
			TEST_THAT(getblockindex->GetObjectID() == store1objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
			// Check against uploaded file
			TEST_THAT(check_block_index("testfiles/file1_upload1", *blockIndexStream));
		}
		// and again, by name
		{
			std::auto_ptr<BackupProtocolClientSuccess> getblockindex(protocol.QueryGetBlockIndexByName(BackupProtocolClientListDirectory::RootDirectory, store1name));
			TEST_THAT(getblockindex->GetObjectID() == store1objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
			// Check against uploaded file
			TEST_THAT(check_block_index("testfiles/file1_upload1", *blockIndexStream));
		}
	}
	// Get the directory again, and see if the entry is in it
	{
		// Command
		std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocol.QueryListDirectory(
				BackupProtocolClientListDirectory::RootDirectory,
				BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
				BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
		// Stream
		BackupStoreDirectory dir;
		std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
		dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
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
			TEST_THAT(en->GetSizeInBlocks() < ((ENCFILE_SIZE * 4 * 3) / 2 / 2048)+2);
			TEST_THAT(en->GetFlags() == BackupStoreDirectory::Entry::Flags_File);
		}
	}

	// Try using GetFile on a directory
	{
		TEST_CHECK_THROWS(std::auto_ptr<BackupProtocolClientSuccess> getFile(protocol.QueryGetFile(BackupProtocolClientListDirectory::RootDirectory, BackupProtocolClientListDirectory::RootDirectory)),
			ConnectionException, Conn_Protocol_UnexpectedReply);
	}
}


int test_server(const char *hostname)
{
	// Context
	TLSContext context;
	context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	// Make some test attributes
	#define ATTR1_SIZE 	245
	#define ATTR2_SIZE 	23
	#define ATTR3_SIZE 	122
	int attr1[ATTR1_SIZE];
	int attr2[ATTR2_SIZE];
	int attr3[ATTR3_SIZE];
	{
		R250 r(3465657);
		for(int l = 0; l < ATTR1_SIZE; ++l) {attr1[l] = r.next();}
		for(int l = 0; l < ATTR2_SIZE; ++l) {attr2[l] = r.next();}
		for(int l = 0; l < ATTR3_SIZE; ++l) {attr3[l] = r.next();}
	}

	// BLOCK
	{
		// Open a connection to the server
		SocketStreamTLS conn;
		conn.Open(context, Socket::TypeINET, hostname, BOX_PORT_BBSTORED);

		// Make a protocol
		BackupProtocolClient protocol(conn);
		
		// Get it logging
		FILE *protocolLog = ::fopen("testfiles/protocol.log", "w");
		TEST_THAT(protocolLog != 0);
		protocol.SetLogToFile(protocolLog);

		// Check the version
		std::auto_ptr<BackupProtocolClientVersion> serverVersion(protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION));
		TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);

		// Login
		std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, 0));
		
		// Check marker is 0
		TEST_THAT(loginConf->GetClientStoreMarker() == 0);

#ifndef WIN32
		// Check that we can't open a new connection which requests write permissions
		{
			SocketStreamTLS conn;
			conn.Open(context, Socket::TypeINET, hostname, BOX_PORT_BBSTORED);
			BackupProtocolClient protocol(conn);
			std::auto_ptr<BackupProtocolClientVersion> serverVersion(protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION));
			TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);
			TEST_CHECK_THROWS(std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, 0)),
				ConnectionException, Conn_Protocol_UnexpectedReply);
			protocol.QueryFinished();
		}
#endif
		
		// Set the client store marker
		protocol.QuerySetClientStoreMarker(0x8732523ab23aLL);

#ifndef WIN32
		// Open a new connection which is read only
		SocketStreamTLS connReadOnly;
		connReadOnly.Open(context, Socket::TypeINET, hostname, BOX_PORT_BBSTORED);
		BackupProtocolClient protocolReadOnly(connReadOnly);

		// Get it logging
		FILE *protocolReadOnlyLog = ::fopen("testfiles/protocolReadOnly.log", "w");
		TEST_THAT(protocolReadOnlyLog != 0);
		protocolReadOnly.SetLogToFile(protocolReadOnlyLog);

		{
			std::auto_ptr<BackupProtocolClientVersion> serverVersion(protocolReadOnly.QueryVersion(BACKUP_STORE_SERVER_VERSION));
			TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);
			std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocolReadOnly.QueryLogin(0x01234567, BackupProtocolClientLogin::Flags_ReadOnly));
			
			// Check client store marker
			TEST_THAT(loginConf->GetClientStoreMarker() == 0x8732523ab23aLL);
		}
#else // WIN32
		BackupProtocolClient& protocolReadOnly(protocol);
#endif

		test_server_1(protocol, protocolReadOnly);
		// Create and upload some test files
		int64_t maxID = 0;
		for(int t = 0; t < UPLOAD_NUM; ++t)
		{
			write_test_file(t);

			std::string filename("testfiles/test");
			filename += uploads[t].fnextra;
			int64_t modtime = 0;
			
			std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile(filename.c_str(), BackupProtocolClientListDirectory::RootDirectory, uploads[t].name, &modtime));
			TEST_THAT(modtime != 0);
			
			std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
				BackupProtocolClientListDirectory::RootDirectory,
				modtime,
				modtime, /* use it for attr hash too */
				0,							/* diff from ID */
				uploads[t].name,
				*upload));
			uploads[t].allocated_objid = stored->GetObjectID();
			uploads[t].mod_time = modtime;
			if(maxID < stored->GetObjectID()) maxID = stored->GetObjectID();
		}

		// Add some attributes onto one of them
		{
			MemBlockStream attrnew(attr3, sizeof(attr3));
			std::auto_ptr<BackupProtocolClientSuccess> set(protocol.QuerySetReplacementFileAttributes(
				BackupProtocolClientListDirectory::RootDirectory,
				32498749832475LL,
				uploads[UPLOAD_ATTRS_EN].name,
				attrnew));
			TEST_THAT(set->GetObjectID() == uploads[UPLOAD_ATTRS_EN].allocated_objid);
		}
		
		// Delete one of them (will implicitly delete an old version)
		{
			std::auto_ptr<BackupProtocolClientSuccess> del(protocol.QueryDeleteFile(
				BackupProtocolClientListDirectory::RootDirectory,
				uploads[UPLOAD_DELETE_EN].name));
			TEST_THAT(del->GetObjectID() == uploads[UPLOAD_DELETE_EN].allocated_objid);
		}
		// Check that the block index can be obtained by name even though it's been deleted
		{
			// Fetch the raw object
			{
				FileStream out("testfiles/downloaddelobj", O_WRONLY | O_CREAT);
				std::auto_ptr<BackupProtocolClientSuccess> getobj(protocol.QueryGetObject(uploads[UPLOAD_DELETE_EN].allocated_objid));
				std::auto_ptr<IOStream> objstream(protocol.ReceiveStream());
				objstream->CopyStreamTo(out);
			}
			// query index and test
			std::auto_ptr<BackupProtocolClientSuccess> getblockindex(protocol.QueryGetBlockIndexByName(
				BackupProtocolClientListDirectory::RootDirectory, uploads[UPLOAD_DELETE_EN].name));
			TEST_THAT(getblockindex->GetObjectID() == uploads[UPLOAD_DELETE_EN].allocated_objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
			TEST_THAT(check_block_index("testfiles/downloaddelobj", *blockIndexStream));
		}

		// Download them all... (even deleted files)
		for(int t = 0; t < UPLOAD_NUM; ++t)
		{
			printf("%d\n", t);
			std::auto_ptr<BackupProtocolClientSuccess> getFile(protocol.QueryGetFile(BackupProtocolClientListDirectory::RootDirectory, uploads[t].allocated_objid));
			TEST_THAT(getFile->GetObjectID() == uploads[t].allocated_objid);
			std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
			test_test_file(t, *filestream);
		}

		{
			StreamableMemBlock attrtest(attr3, sizeof(attr3));

			// Use the read only connection to verify that the directory is as we expect
			check_dir_after_uploads(protocolReadOnly, attrtest);
			// And on the read/write one
			check_dir_after_uploads(protocol, attrtest);
		}
		
		// Check diffing and rsync like stuff...
		// Build a modified file
		{
			// Basically just insert a bit in the middle
			TEST_THAT(TestGetFileSize(TEST_FILE_FOR_PATCHING) == TEST_FILE_FOR_PATCHING_SIZE);
			FileStream in(TEST_FILE_FOR_PATCHING);
			void *buf = ::malloc(TEST_FILE_FOR_PATCHING_SIZE);
			FileStream out(TEST_FILE_FOR_PATCHING ".mod", O_WRONLY | O_CREAT | O_EXCL);
			TEST_THAT(in.Read(buf, TEST_FILE_FOR_PATCHING_PATCH_AT) == TEST_FILE_FOR_PATCHING_PATCH_AT);
			out.Write(buf, TEST_FILE_FOR_PATCHING_PATCH_AT);
			char insert[13] = "INSERTINSERT";
			out.Write(insert, sizeof(insert));
			TEST_THAT(in.Read(buf, TEST_FILE_FOR_PATCHING_SIZE - TEST_FILE_FOR_PATCHING_PATCH_AT) == TEST_FILE_FOR_PATCHING_SIZE - TEST_FILE_FOR_PATCHING_PATCH_AT);
			out.Write(buf, TEST_FILE_FOR_PATCHING_SIZE - TEST_FILE_FOR_PATCHING_PATCH_AT);
			::free(buf);
		}
		{
			// Fetch the block index for this one
			std::auto_ptr<BackupProtocolClientSuccess> getblockindex(protocol.QueryGetBlockIndexByName(
				BackupProtocolClientListDirectory::RootDirectory, uploads[UPLOAD_PATCH_EN].name));
			TEST_THAT(getblockindex->GetObjectID() == uploads[UPLOAD_PATCH_EN].allocated_objid);
			std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
			
			// Do the patching
			bool isCompletelyDifferent = false;
			int64_t modtime;
			std::auto_ptr<IOStream> patchstream(
				BackupStoreFile::EncodeFileDiff(
					TEST_FILE_FOR_PATCHING ".mod", 
					BackupProtocolClientListDirectory::RootDirectory,
					uploads[UPLOAD_PATCH_EN].name, 
					uploads[UPLOAD_PATCH_EN].allocated_objid, 
					*blockIndexStream,
					IOStream::TimeOutInfinite, 
					NULL, // pointer to DiffTimer impl
					&modtime, &isCompletelyDifferent));
			TEST_THAT(isCompletelyDifferent == false);
			// Sent this to a file, so we can check the size, rather than uploading it directly
			{
				FileStream patch(TEST_FILE_FOR_PATCHING ".patch", O_WRONLY | O_CREAT | O_EXCL);
				patchstream->CopyStreamTo(patch);
			}
			// Make sure the stream is a plausible size for a patch containing only one new block
			TEST_THAT(TestGetFileSize(TEST_FILE_FOR_PATCHING ".patch") < (8*1024));
			// Upload it
			int64_t patchedID = 0;
			{
				FileStream uploadpatch(TEST_FILE_FOR_PATCHING ".patch");
				std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
					BackupProtocolClientListDirectory::RootDirectory,
					modtime,
					modtime, /* use it for attr hash too */
					uploads[UPLOAD_PATCH_EN].allocated_objid,		/* diff from ID */
					uploads[UPLOAD_PATCH_EN].name,
					uploadpatch));
				TEST_THAT(stored->GetObjectID() > 0);
				if(maxID < stored->GetObjectID()) maxID = stored->GetObjectID();
				patchedID = stored->GetObjectID();
			}
			// Then download it to check it's OK
			std::auto_ptr<BackupProtocolClientSuccess> getFile(protocol.QueryGetFile(BackupProtocolClientListDirectory::RootDirectory, patchedID));
			TEST_THAT(getFile->GetObjectID() == patchedID);
			std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
			BackupStoreFile::DecodeFile(*filestream, TEST_FILE_FOR_PATCHING ".downloaded", IOStream::TimeOutInfinite);
			// Check it's the same
			TEST_THAT(check_files_same(TEST_FILE_FOR_PATCHING ".downloaded", TEST_FILE_FOR_PATCHING ".mod"));
		}

		// Create a directory
		int64_t subdirid = 0;
		BackupStoreFilenameClear dirname("lovely_directory");
		{
			// Attributes
			MemBlockStream attr(attr1, sizeof(attr1));
			std::auto_ptr<BackupProtocolClientSuccess> dirCreate(protocol.QueryCreateDirectory(
				BackupProtocolClientListDirectory::RootDirectory,
				9837429842987984LL, dirname, attr));
			subdirid = dirCreate->GetObjectID(); 
			TEST_THAT(subdirid == maxID + 1);
		}
		// Stick a file in it
		int64_t subdirfileid = 0;
		{
			std::string filename("testfiles/test0");
			int64_t modtime;
			std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile(filename.c_str(), subdirid, uploads[0].name, &modtime));

			std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
				subdirid,
				modtime,
				modtime, /* use for attr hash too */
				0,							/* diff from ID */
				uploads[0].name,
				*upload));
			subdirfileid = stored->GetObjectID();
		}
		// Check the directories on the read only connection
		{
			// Command
			std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
					BackupProtocolClientListDirectory::RootDirectory,
					BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
					BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes! */)); // Stream
			BackupStoreDirectory dir;
			std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
			dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
			TEST_THAT(dir.GetNumberOfEntries() == UPLOAD_NUM + 3 /* for the first test file, the patched upload, and this new dir */);

			// Check the last one...
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;
			BackupStoreDirectory::Entry *t = 0;
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
			// Does it look right?
			TEST_THAT(en->GetName() == dirname);
			TEST_THAT(en->GetFlags() == BackupProtocolClientListDirectory::Flags_Dir);
			TEST_THAT(en->GetObjectID() == subdirid);
			TEST_THAT(en->GetModificationTime() == 0);	// dirs don't have modification times.
		}
		{
			// Command
			std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
					subdirid,
					BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
					BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, true /* get attributes */));
			TEST_THAT(dirreply->GetObjectID() == subdirid);
			// Stream
			BackupStoreDirectory dir;
			std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
			dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
			TEST_THAT(dir.GetNumberOfEntries() == 1);

			// Check the last one...
			BackupStoreDirectory::Iterator i(dir);
			// Discard first
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);
			// Does it look right?
			TEST_THAT(en->GetName() == uploads[0].name);
			TEST_THAT(en->GetFlags() == BackupProtocolClientListDirectory::Flags_File);
			TEST_THAT(en->GetObjectID() == subdirfileid);
			TEST_THAT(en->GetModificationTime() != 0);

			// Attributes
			TEST_THAT(dir.HasAttributes());
			TEST_THAT(dir.GetAttributesModTime() == 9837429842987984LL);
			StreamableMemBlock attr(attr1, sizeof(attr1));
			TEST_THAT(dir.GetAttributes() == attr);
		}
		// Check that we don't get attributes if we don't ask for them
		{
			// Command
			std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
					subdirid,
					BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
					BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes! */));
			// Stream
			BackupStoreDirectory dir;
			std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
			dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
			TEST_THAT(!dir.HasAttributes());
		}
		// Change attributes on the directory
		{
			MemBlockStream attrnew(attr2, sizeof(attr2));
			std::auto_ptr<BackupProtocolClientSuccess> changereply(protocol.QueryChangeDirAttributes(
					subdirid,
					329483209443598LL,
					attrnew));
			TEST_THAT(changereply->GetObjectID() == subdirid);
		}
		// Check the new attributes
		{
			// Command
			std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
					subdirid,
					0,	// no flags
					BackupProtocolClientListDirectory::Flags_EXCLUDE_EVERYTHING, true /* get attributes */));
			// Stream
			BackupStoreDirectory dir;
			std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
			dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
			TEST_THAT(dir.GetNumberOfEntries() == 0);

			// Attributes
			TEST_THAT(dir.HasAttributes());
			TEST_THAT(dir.GetAttributesModTime() == 329483209443598LL);
			StreamableMemBlock attrtest(attr2, sizeof(attr2));
			TEST_THAT(dir.GetAttributes() == attrtest);
		}
		
		// Test moving a file
		{
			BackupStoreFilenameClear newName("moved-files");
		
			std::auto_ptr<BackupProtocolClientSuccess> rep(protocol.QueryMoveObject(uploads[UPLOAD_FILE_TO_MOVE].allocated_objid,
				BackupProtocolClientListDirectory::RootDirectory,
				subdirid, BackupProtocolClientMoveObject::Flags_MoveAllWithSameName, newName));
			TEST_THAT(rep->GetObjectID() == uploads[UPLOAD_FILE_TO_MOVE].allocated_objid);
		}
		// Try some dodgy renames
		{
			BackupStoreFilenameClear newName("moved-files");
			TEST_CHECK_THROWS(protocol.QueryMoveObject(uploads[UPLOAD_FILE_TO_MOVE].allocated_objid,
					BackupProtocolClientListDirectory::RootDirectory,
					subdirid, BackupProtocolClientMoveObject::Flags_MoveAllWithSameName, newName),
				ConnectionException, Conn_Protocol_UnexpectedReply);
			TEST_CHECK_THROWS(protocol.QueryMoveObject(uploads[UPLOAD_FILE_TO_MOVE].allocated_objid,
					subdirid,
					subdirid, BackupProtocolClientMoveObject::Flags_MoveAllWithSameName, newName),
				ConnectionException, Conn_Protocol_UnexpectedReply);
		}
		// Rename within a directory
		{
			BackupStoreFilenameClear newName("moved-files-x");
			protocol.QueryMoveObject(uploads[UPLOAD_FILE_TO_MOVE].allocated_objid,
				subdirid,
				subdirid, BackupProtocolClientMoveObject::Flags_MoveAllWithSameName, newName);
		}
		// Check it's all gone from the root directory...
		{
			// Command
			std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
					BackupProtocolClientListDirectory::RootDirectory,
					BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
					BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
			// Stream
			BackupStoreDirectory dir;
			std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
			dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
			// Read all entries
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;
			while((en = i.Next()) != 0)
			{
				TEST_THAT(en->GetName() != uploads[UPLOAD_FILE_TO_MOVE].name);
			}
		}
		// Check the old and new versions are in the other directory
		{
			BackupStoreFilenameClear lookFor("moved-files-x");

			// Command
			std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
					subdirid,
					BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING,
					BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
			// Stream
			BackupStoreDirectory dir;
			std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
			dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
			// Check entries
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = 0;
			bool foundCurrent = false;
			bool foundOld = false;
			while((en = i.Next()) != 0)
			{
				if(en->GetName() == lookFor)
				{
					if(en->GetFlags() == (BackupStoreDirectory::Entry::Flags_File)) foundCurrent = true;
					if(en->GetFlags() == (BackupStoreDirectory::Entry::Flags_File | BackupStoreDirectory::Entry::Flags_OldVersion)) foundOld = true;
				}
			}
			TEST_THAT(foundCurrent);
			TEST_THAT(foundOld);
		}
		// make a little bit more of a thing to look at
		int64_t subsubdirid = 0;
		int64_t subsubfileid = 0;
		{
			BackupStoreFilenameClear nd("sub2");
			// Attributes
			MemBlockStream attr(attr1, sizeof(attr1));
			std::auto_ptr<BackupProtocolClientSuccess> dirCreate(protocol.QueryCreateDirectory(
				subdirid,
				9837429842987984LL, nd, attr));
			subsubdirid = dirCreate->GetObjectID(); 

			FileStream upload("testfiles/file1_upload1");
			BackupStoreFilenameClear nf("file2");
			std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
				subsubdirid,
				0x123456789abcdefLL,		/* modification time */
				0x7362383249872dfLL,		/* attr hash */
				0,							/* diff from ID */
				nf,
				upload));
			subsubfileid = stored->GetObjectID();
		}
		// Query names -- test that invalid stuff returns not found OK
		{
			std::auto_ptr<BackupProtocolClientObjectName> nameRep(protocol.QueryGetObjectName(3248972347823478927LL, subsubdirid));
			TEST_THAT(nameRep->GetNumNameElements() == 0);		
		}
		{
			std::auto_ptr<BackupProtocolClientObjectName> nameRep(protocol.QueryGetObjectName(subsubfileid, 2342378424LL));
			TEST_THAT(nameRep->GetNumNameElements() == 0);		
		}
		{
			std::auto_ptr<BackupProtocolClientObjectName> nameRep(protocol.QueryGetObjectName(38947234789LL, 2342378424LL));
			TEST_THAT(nameRep->GetNumNameElements() == 0);		
		}
		{
			std::auto_ptr<BackupProtocolClientObjectName> nameRep(protocol.QueryGetObjectName(BackupProtocolClientGetObjectName::ObjectID_DirectoryOnly, 2234342378424LL));
			TEST_THAT(nameRep->GetNumNameElements() == 0);		
		}
		// Query names... first, get info for the file
		{
			std::auto_ptr<BackupProtocolClientObjectName> nameRep(protocol.QueryGetObjectName(subsubfileid, subsubdirid));
			std::auto_ptr<IOStream> namestream(protocol.ReceiveStream());
		
			TEST_THAT(nameRep->GetNumNameElements() == 3);
			TEST_THAT(nameRep->GetFlags() == BackupProtocolClientListDirectory::Flags_File);
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
			std::auto_ptr<BackupProtocolClientObjectName> nameRep(protocol.QueryGetObjectName(BackupProtocolClientGetObjectName::ObjectID_DirectoryOnly, subsubdirid));
			std::auto_ptr<IOStream> namestream(protocol.ReceiveStream());
		
			TEST_THAT(nameRep->GetNumNameElements() == 2);
			TEST_THAT(nameRep->GetFlags() == BackupProtocolClientListDirectory::Flags_Dir);
			static const char *testnames[] = {"sub2","lovely_directory"};
			for(int l = 0; l < nameRep->GetNumNameElements(); ++l)
			{
				BackupStoreFilenameClear fn;
				fn.ReadFromStream(*namestream, 10000);
				TEST_THAT(fn.GetClearFilename() == testnames[l]);
			}
		}
		
//}	skip:
	
		// Create some nice recursive directories
		int64_t dirtodelete = create_test_data_subdirs(protocol,
			BackupProtocolClientListDirectory::RootDirectory, "test_delete", 6 /* depth */);
		
		// And delete them
		{
			std::auto_ptr<BackupProtocolClientSuccess> dirdel(protocol.QueryDeleteDirectory(
					dirtodelete));
			TEST_THAT(dirdel->GetObjectID() == dirtodelete);
		}

		// Get the root dir, checking for deleted items
		{
			// Command
			std::auto_ptr<BackupProtocolClientSuccess> dirreply(protocolReadOnly.QueryListDirectory(
					BackupProtocolClientListDirectory::RootDirectory,
					BackupProtocolClientListDirectory::Flags_Dir | BackupProtocolClientListDirectory::Flags_Deleted,
					BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
			// Stream
			BackupStoreDirectory dir;
			std::auto_ptr<IOStream> dirstream(protocolReadOnly.ReceiveStream());
			dir.ReadFromStream(*dirstream, IOStream::TimeOutInfinite);
			
			// Check there's only that one entry
			TEST_THAT(dir.GetNumberOfEntries() == 1);
			
			BackupStoreDirectory::Iterator i(dir);
			BackupStoreDirectory::Entry *en = i.Next();
			TEST_THAT(en != 0);
			if(en)
			{
				TEST_THAT(en->GetObjectID() == dirtodelete);
				BackupStoreFilenameClear n("test_delete");
				TEST_THAT(en->GetName() == n);
			}
			
			// Then... check everything's deleted
			test_everything_deleted(protocolReadOnly, dirtodelete);
		}
			
		// Finish the connections
#ifndef WIN32
		protocolReadOnly.QueryFinished();
#endif
		protocol.QueryFinished();
		
		// Close logs
#ifndef WIN32
		::fclose(protocolReadOnlyLog);
#endif
		::fclose(protocolLog);
	}
	
	return 0;
}

int test3(int argc, const char *argv[])
{
	// Now test encoded files
	// TODO: This test needs to check failure situations as well as everything working,
	// but this will be saved for the full implementation.
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
		
		// The test block to a file
		{
			FileStream f("testfiles" DIRECTORY_SEPARATOR 
				"testenc1", O_WRONLY | O_CREAT | O_EXCL);
			f.Write(encfile, sizeof(encfile));
		}
		
		// Encode it
		{
			FileStream out("testfiles" DIRECTORY_SEPARATOR 
				"testenc1_enc", O_WRONLY | O_CREAT | O_EXCL);
			BackupStoreFilenameClear name("testfiles"
				DIRECTORY_SEPARATOR "testenc1");

			std::auto_ptr<IOStream> encoded(
				BackupStoreFile::EncodeFile(
					"testfiles" DIRECTORY_SEPARATOR
					"testenc1", 32, name));
			encoded->CopyStreamTo(out);
		}
		
		// Verify it
		{
			FileStream enc("testfiles" DIRECTORY_SEPARATOR 
				"testenc1_enc");
			TEST_THAT(BackupStoreFile::VerifyEncodedFileFormat(enc) == true);
		}
		
		// Decode it
		{
			FileStream enc("testfiles" DIRECTORY_SEPARATOR 
				"testenc1_enc");
			BackupStoreFile::DecodeFile(enc, "testfiles"
				DIRECTORY_SEPARATOR "testenc1_orig", 
				IOStream::TimeOutInfinite);
		}
		
		// Read in rebuilt original, and compare contents
		{
			TEST_THAT(TestGetFileSize("testfiles" 
				DIRECTORY_SEPARATOR "testenc1_orig") 
				== sizeof(encfile));
			FileStream in("testfiles" DIRECTORY_SEPARATOR 
				"testenc1_orig");
			int encfile_i[ENCFILE_SIZE];
			in.Read(encfile_i, sizeof(encfile_i));
			TEST_THAT(memcmp(encfile, encfile_i, sizeof(encfile)) == 0);
		}
		
		// Check how many blocks it had, and test the stream based interface
		{
			FileStream enc("testfiles" DIRECTORY_SEPARATOR 
				"testenc1_enc");
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
			FileStream f("testfiles" DIRECTORY_SEPARATOR 
				"testenc2", O_WRONLY | O_CREAT | O_EXCL);
			f.Write(encfile + 2, FILE_SIZE_JUST_OVER);
			f.Close();
			BackupStoreFilenameClear name("testenc2");
			std::auto_ptr<IOStream> encoded(
				BackupStoreFile::EncodeFile(
					"testfiles" DIRECTORY_SEPARATOR
					"testenc2", 32, name));
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
			FileStream enc("testfiles" DIRECTORY_SEPARATOR 
				"testenc1_enc");
			std::auto_ptr<IOStream> reordered(BackupStoreFile::ReorderFileToStreamOrder(&enc, false));
			std::auto_ptr<BackupStoreFile::DecodedStream> decoded(BackupStoreFile::DecodeFileStream(*reordered, IOStream::TimeOutInfinite));
			CollectInBufferStream d;
			decoded->CopyStreamTo(d, IOStream::TimeOutInfinite, 971 /* buffer block size */);
			d.SetForReading();
			TEST_THAT(d.GetSize() == sizeof(encfile));
			TEST_THAT(memcmp(encfile, d.GetBuffer(), sizeof(encfile)) == 0);

			TEST_THAT(decoded->GetNumBlocks() == 3);
		}

#ifndef WIN32		
		// Try out doing this on a symlink
		{
			TEST_THAT(::symlink("does/not/exist", "testfiles/testsymlink") == 0);
			BackupStoreFilenameClear name("testsymlink");
			std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/testsymlink", 32, name));
			// Can't decode it from the stream, because it's in file order, and doesn't have the 
			// required properties to be able to reorder it. So buffer it...
			CollectInBufferStream b;
			encoded->CopyStreamTo(b);
			b.SetForReading();
			// Decode it
			BackupStoreFile::DecodeFile(b, "testfiles/testsymlink_2", IOStream::TimeOutInfinite);
		}
#endif
	}

	// Store info
	{
		RaidFileWrite::CreateDirectory(0, "test-info");
		BackupStoreInfo::CreateNew(76, "test-info" DIRECTORY_SEPARATOR, 
			0, 3461231233455433LL, 2934852487LL);
		TEST_CHECK_THROWS(BackupStoreInfo::CreateNew(76, 
			"test-info" DIRECTORY_SEPARATOR, 0, 0, 0), 
			RaidFileException, CannotOverwriteExistingFile);
		std::auto_ptr<BackupStoreInfo> info(
			BackupStoreInfo::Load(76, 
				"test-info" DIRECTORY_SEPARATOR, 0, true));
		TEST_CHECK_THROWS(info->Save(), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->ChangeBlocksUsed(1), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->ChangeBlocksInOldFiles(1), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->ChangeBlocksInDeletedFiles(1), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->RemovedDeletedDirectory(2), BackupStoreException, StoreInfoIsReadOnly);
		TEST_CHECK_THROWS(info->AddDeletedDirectory(2), BackupStoreException, StoreInfoIsReadOnly);
	}
	{
		std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(76, 
			"test-info" DIRECTORY_SEPARATOR, 0, false));
		info->ChangeBlocksUsed(8);
		info->ChangeBlocksInOldFiles(9);
		info->ChangeBlocksInDeletedFiles(10);
		info->ChangeBlocksUsed(-1);
		info->ChangeBlocksInOldFiles(-4);
		info->ChangeBlocksInDeletedFiles(-9);
		TEST_CHECK_THROWS(info->ChangeBlocksUsed(-100), BackupStoreException, StoreInfoBlockDeltaMakesValueNegative);
		TEST_CHECK_THROWS(info->ChangeBlocksInOldFiles(-100), BackupStoreException, StoreInfoBlockDeltaMakesValueNegative);
		TEST_CHECK_THROWS(info->ChangeBlocksInDeletedFiles(-100), BackupStoreException, StoreInfoBlockDeltaMakesValueNegative);
		info->AddDeletedDirectory(2);
		info->AddDeletedDirectory(3);
		info->AddDeletedDirectory(4);
		info->RemovedDeletedDirectory(3);
		TEST_CHECK_THROWS(info->RemovedDeletedDirectory(9), BackupStoreException, StoreInfoDirNotInList);
		info->Save();
	}
	{
		std::auto_ptr<BackupStoreInfo> info(BackupStoreInfo::Load(76, 
			"test-info" DIRECTORY_SEPARATOR, 0, true));
		TEST_THAT(info->GetBlocksUsed() == 7);
		TEST_THAT(info->GetBlocksInOldFiles() == 5);
		TEST_THAT(info->GetBlocksInDeletedFiles() == 1);
		TEST_THAT(info->GetBlocksSoftLimit() == 3461231233455433LL);
		TEST_THAT(info->GetBlocksHardLimit() == 2934852487LL);
		const std::vector<int64_t> &delfiles(info->GetDeletedDirectories());
		TEST_THAT(delfiles.size() == 2);
		TEST_THAT(delfiles[0] == 2);
		TEST_THAT(delfiles[1] == 4);
	}

//printf("SKIPPINGTESTS---------\n");
//return 0;

	// Context
	TLSContext context;
	context.Initialise(false /* client */,
			"testfiles" DIRECTORY_SEPARATOR "clientCerts.pem",
			"testfiles" DIRECTORY_SEPARATOR "clientPrivKey.pem",
			"testfiles" DIRECTORY_SEPARATOR "clientTrustedCAs.pem");

	// First, try logging in without an account having been created... just make sure login fails.

#ifdef WIN32
	int pid = LaunchServer("..\\..\\bin\\bbstored\\bbstored testfiles/bbstored.conf", "testfiles/bbstored.pid");
#else
	int pid = LaunchServer("../../bin/bbstored/bbstored testfiles/bbstored.conf", "testfiles/bbstored.pid");
#endif

	TEST_THAT(pid != -1 && pid != 0);
	if(pid > 0)
	{
		::sleep(1);
		TEST_THAT(ServerIsAlive(pid));

		// BLOCK
		{
			// Open a connection to the server
			SocketStreamTLS conn;
			conn.Open(context, Socket::TypeINET, "localhost", BOX_PORT_BBSTORED);

			// Make a protocol
			BackupProtocolClient protocol(conn);

			// Check the version
			std::auto_ptr<BackupProtocolClientVersion> serverVersion(protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION));
			TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);

			// Login
			TEST_CHECK_THROWS(std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, 0)),
				ConnectionException, Conn_Protocol_UnexpectedReply);
			
			// Finish the connection
			protocol.QueryFinished();
		}

		// Create an account for the test client
#ifdef WIN32
		TEST_THAT_ABORTONFAIL(::system("..\\..\\bin\\bbstoreaccounts\\bbstoreaccounts -c testfiles/bbstored.conf create 01234567 0 10000B 20000B") == 0);
#else
		TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf create 01234567 0 10000B 20000B") == 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
#endif

		TEST_THAT(TestDirExists("testfiles/0_0/backup/01234567"));
		TEST_THAT(TestDirExists("testfiles/0_1/backup/01234567"));
		TEST_THAT(TestDirExists("testfiles/0_2/backup/01234567"));
		TEST_THAT(TestGetFileSize("testfiles/accounts.txt") > 8);	// make sure something is written to it

		TEST_THAT(ServerIsAlive(pid));

		TEST_THAT(test_server("localhost") == 0);
		
		// Test the deletion of objects by the housekeeping system
		// First, things as they are now.
		recursive_count_objects_results before = {0,0,0};

		recursive_count_objects("localhost", BackupProtocolClientListDirectory::RootDirectory, before);
		
		TEST_THAT(before.objectsNotDel != 0);
		TEST_THAT(before.deleted != 0);
		TEST_THAT(before.old != 0);

		// Kill it
		TEST_THAT(KillServer(pid));
		::sleep(1);
		TEST_THAT(!ServerIsAlive(pid));
#ifndef WIN32
		TestRemoteProcessMemLeaks("bbstored.memleaks");
#endif
		
		// Set a new limit on the account -- leave the hard limit high to make sure the target for
		// freeing space is the soft limit.

#ifdef WIN32
		TEST_THAT_ABORTONFAIL(::system("..\\..\\bin\\bbstoreaccounts\\bbstoreaccounts -c testfiles/bbstored.conf setlimit 01234567 10B 20000B") == 0);
#else
		TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf setlimit 01234567 10B 20000B") == 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
#endif

		// Start things up
#ifdef WIN32
		pid = LaunchServer("..\\..\\bin\\bbstored\\bbstored testfiles/bbstored.conf", "testfiles/bbstored.pid");
#else
		pid = LaunchServer("../../bin/bbstored/bbstored testfiles/bbstored.conf", "testfiles/bbstored.pid");
#endif

		::sleep(1);
		TEST_THAT(ServerIsAlive(pid));
		
		// wait for housekeeping to happen
		printf("waiting for housekeeping:\n");
		for(int l = 0; l < 30; ++l)
		{
			::sleep(1);
			printf(".");
			fflush(stdout);
		}
		printf("\n");

		// Count the objects again
		recursive_count_objects_results after = {0,0,0};
		recursive_count_objects("localhost", BackupProtocolClientListDirectory::RootDirectory, after);
printf("after.objectsNotDel=%i, deleted=%i, old=%i\n",after.objectsNotDel, after.deleted, after.old);

		// If these tests fail then try increasing the timeout above
		TEST_THAT(after.objectsNotDel == before.objectsNotDel);
		TEST_THAT(after.deleted == 0);
		TEST_THAT(after.old == 0);
		
		// Set a really small hard limit
#ifdef WIN32
		TEST_THAT_ABORTONFAIL(::system("..\\..\\bin\\bbstoreaccounts\\bbstoreaccounts -c testfiles/bbstored.conf setlimit 01234567 10B 20B") == 0);
#else
		TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf setlimit 01234567 10B 20B") == 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
#endif

		// Try to upload a file and create a directory, and check an error is generated
		{
			// Open a connection to the server
			SocketStreamTLS conn;
			conn.Open(context, Socket::TypeINET, "localhost", BOX_PORT_BBSTORED);

			// Make a protocol
			BackupProtocolClient protocol(conn);

			// Check the version
			std::auto_ptr<BackupProtocolClientVersion> serverVersion(protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION));
			TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);

			// Login
			std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, 0));
			
			int64_t modtime = 0;
			
			BackupStoreFilenameClear fnx("exceed-limit");
			std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile("testfiles/test3", BackupProtocolClientListDirectory::RootDirectory, fnx, &modtime));
			TEST_THAT(modtime != 0);

			TEST_CHECK_THROWS(std::auto_ptr<BackupProtocolClientSuccess> stored(protocol.QueryStoreFile(
					BackupProtocolClientListDirectory::RootDirectory,
					modtime,
					modtime, /* use it for attr hash too */
					0,							/* diff from ID */
					fnx,
					*upload)),
				ConnectionException, Conn_Protocol_UnexpectedReply);

			MemBlockStream attr(&modtime, sizeof(modtime));
			BackupStoreFilenameClear fnxd("exceed-limit-dir");
			TEST_CHECK_THROWS(std::auto_ptr<BackupProtocolClientSuccess> dirCreate(protocol.QueryCreateDirectory(
					BackupProtocolClientListDirectory::RootDirectory,
					9837429842987984LL, fnxd, attr)),
				ConnectionException, Conn_Protocol_UnexpectedReply);


			// Finish the connection
			protocol.QueryFinished();
		}

		// Kill it again
		TEST_THAT(KillServer(pid));
		::sleep(1);
		TEST_THAT(!ServerIsAlive(pid));

#ifndef WIN32
		TestRemoteProcessMemLeaks("bbstored.memleaks");
#endif
	}

	return 0;
}

int multi_server()
{
	printf("Starting server for connection from remote machines...\n");

	// Create an account for the test client
	TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf create 01234567 0 30000B 40000B") == 0);

#ifndef WIN32
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
#endif

	// First, try logging in without an account having been created... just make sure login fails.

#ifdef WIN32
	int pid = LaunchServer("..\\..\\bin\\bbstored\\bbstored testfiles/bbstored_multi.conf", "testfiles/bbstored.pid");
#else
	int pid = LaunchServer("../../bin/bbstored/bbstored testfiles/bbstored_multi.conf", "testfiles/bbstored.pid");
#endif

	TEST_THAT(pid != -1 && pid != 0);
	if(pid > 0)
	{
		::sleep(1);
		TEST_THAT(ServerIsAlive(pid));

		// Wait for a keypress
		printf("Press ENTER to terminate the server\n");
		char line[512];
		fgets(line, 512, stdin);
		printf("Terminating server...\n");

		// Kill it
		TEST_THAT(KillServer(pid));
		::sleep(1);
		TEST_THAT(!ServerIsAlive(pid));
#ifndef WIN32
		TestRemoteProcessMemLeaks("bbstored.memleaks");
#endif
	}


	return 0;
}

WCHAR* ConvertUtf8ToWideString(const char* pString);
std::string ConvertPathToAbsoluteUnicode(const char *pFileName);

int test(int argc, const char *argv[])
{
#ifdef WIN32
	// Under win32 we must initialise the Winsock library
	// before using sockets

	WSADATA info;
	TEST_THAT(WSAStartup(0x0101, &info) != SOCKET_ERROR)

	// this had better work, or bbstored will die when combining diffs
	char* file = "foo";
	std::string abs = ConvertPathToAbsoluteUnicode(file);
	WCHAR* wfile = ConvertUtf8ToWideString(abs.c_str());

	DWORD accessRights = FILE_READ_ATTRIBUTES | 
		FILE_LIST_DIRECTORY | FILE_READ_EA | FILE_WRITE_ATTRIBUTES |
		FILE_WRITE_DATA | FILE_WRITE_EA /*| FILE_ALL_ACCESS*/;
	DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

	HANDLE h1 = CreateFileW(wfile, accessRights, shareMode,
		NULL, OPEN_ALWAYS, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	assert(h1 != INVALID_HANDLE_VALUE);
	TEST_THAT(h1 != INVALID_HANDLE_VALUE);

	accessRights = FILE_READ_ATTRIBUTES | 
		FILE_LIST_DIRECTORY | FILE_READ_EA;

	HANDLE h2 = CreateFileW(wfile, accessRights, shareMode,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	assert(h2 != INVALID_HANDLE_VALUE);
	TEST_THAT(h2 != INVALID_HANDLE_VALUE);

	CloseHandle(h2);
	CloseHandle(h1);

	h1 = openfile("foo", O_CREAT | O_RDWR, 0);
	assert(h1);
	TEST_THAT(h1);
	h2 = openfile("foo", O_RDWR, 0);
	assert(h2);
	TEST_THAT(h2);
	CloseHandle(h2);
	CloseHandle(h1);
#endif

	// SSL library
	SSLLib::Initialise();
	
	// Give a test key for the filenames
//	BackupStoreFilenameClear::SetBlowfishKey(FilenameEncodingKey, sizeof(FilenameEncodingKey));
	// And set the encoding to blowfish
//	BackupStoreFilenameClear::SetEncodingMethod(BackupStoreFilename::Encoding_Blowfish);
	
	// And for directory attributes -- need to set it, as used in file encoding
//	BackupClientFileAttributes::SetBlowfishKey(AttributesEncodingKey, sizeof(AttributesEncodingKey));
	
	// And finally for file encoding
//	BackupStoreFile::SetBlowfishKeys(FileEncodingKey, sizeof(FileEncodingKey), FileBlockEntryEncodingKey, sizeof(FileBlockEntryEncodingKey));

	// Use the setup crypto command to set up all these keys, so that the bbackupquery command can be used
	// for seeing what's going on.
#ifdef WIN32
	BackupClientCryptoKeys_Setup("testfiles\\bbackupd.keys");	
#else
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");	
#endif
	
	// encode in some filenames -- can't do static initialisation because the key won't be set up when these are initialised
	for(unsigned int l = 0; l < sizeof(ens_filenames) / sizeof(ens_filenames[0]); ++l)
	{
		ens[l].fn = BackupStoreFilenameClear(ens_filenames[l]);
	}
	for(unsigned int l = 0; l < sizeof(uploads_filenames) / sizeof(uploads_filenames[0]); ++l)
	{
		uploads[l].name = BackupStoreFilenameClear(uploads_filenames[l]);
	}
	
	// Trace errors out
	SET_DEBUG_SSLLIB_TRACE_ERRORS

	if(argc == 2 && strcmp(argv[1], "server") == 0)
	{
		return multi_server();
	}
	if(argc == 3 && strcmp(argv[1], "client") == 0)
	{
		return test_server(argv[2]);
	}
// large file test	
/*	{
		int64_t modtime = 0;
		std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile("/Users/ben/temp/large.tar",
			BackupProtocolClientListDirectory::RootDirectory, uploads[0].name, &modtime));
		TEST_THAT(modtime != 0);
		FileStream write("testfiles/large.enc", O_WRONLY | O_CREAT);
		upload->CopyStreamTo(write);
	}	
printf("SKIPPING TESTS ------------------------------------------------------\n");
return 0;*/
	int r = 0;
	r = test1(argc, argv);
	if(r != 0) return r;
	r = test2(argc, argv);
	if(r != 0) return r;
	r = test3(argc, argv);
	if(r != 0) return r;
	return 0;
}

