// --------------------------------------------------------------------------
//
// File
//		Name:    testbackupstorepatch.cpp
//		Purpose: Test storage of patches on the backup store server
//		Created: 13/7/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <iostream>

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
#include "BackupStoreFileEncodeStream.h"
#include "BackupStoreFilenameClear.h"
#include "BackupStoreInfo.h"
#include "BoxPortsAndFiles.h"
#include "CollectInBufferStream.h"
#include "ClientTestUtils.h"
#include "FileStream.h"
#include "MemBlockStream.h"
#include "RaidFileController.h"
#include "RaidFileException.h"
#include "RaidFileRead.h"
#include "RaidFileUtil.h"
#include "RaidFileWrite.h"
#include "SSLLib.h"
#include "ServerControl.h"
#include "Socket.h"
#include "SocketStreamTLS.h"
#include "StoreStructure.h"
#include "StoreTestUtils.h" // for run_housekeeping()
#include "TLSContext.h"
#include "Test.h"

#include "MemLeakFindOn.h"

typedef struct
{
	int ChangePoint, InsertBytes, DeleteBytes;
	int64_t IDOnServer;
	bool IsCompletelyDifferent;
	bool HasBeenDeleted;
	int64_t DependsOn, RequiredBy;
	int64_t CurrentSizeInBlocks;
} file_info;

file_info test_files[] =
{
//	ChPnt,	Insert,	Delete, ID,	IsCDf,	BeenDel
	{0, 	0,	0,	0,	false,	false},	// 0 dummy first entry
	{32000,	2087,	0,	0,	false,	false}, // 1
	{1000,	1998,	2976,	0,	false,	false}, // 2
	{27800,	0,	288,	0,	false,	false}, // 3
	{3208,	1087,	98,	0,	false,	false}, // 4 - this entry is deleted from middle of patch chain on r=1
	{56000,	23087,	98,	0,	false,	false}, // 5
	{0,	98765,	9999999,0,	false,	false},	// 6 completely different, make a break in the storage
	{9899,	9887,	2,	0,	false,	false}, // 7
	{12984,	12345,	1234,	0,	false,	false}, // 8
	{1209,	29885,	3498,	0,	false,	false}  // 9
};

// Order in which the files will be removed from the server
int test_file_remove_order[] = {0, 2, 3, 5, 8, 1, 4, -1};

#define NUMBER_FILES	((sizeof(test_files) / sizeof(test_files[0])))
#define FIRST_FILE_SIZE	(64*1024+3)
#define BUFFER_SIZE		(256*1024)
#define SHORT_TIMEOUT 5000

// Chunk of memory to use for copying files, etc
static void *buffer = 0;

int64_t ModificationTime = 7766333330000LL;
#define MODIFICATION_TIME_INC	10000000;

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

// will overrun the buffer!
void make_random_data(void *buffer, int size, int seed)
{
	R250 rand(seed);

	int n = (size / sizeof(int)) + 1;
	int *b = (int*)buffer;
	for(int l = 0; l < n; ++l)
	{
		b[l] = rand.next();
	}
}

bool files_identical(const std::string& filename_1, const std::string& filename_2)
{
	FileStream f1(filename_1);
	FileStream f2(filename_2);
	char buffer1[2048];
	char buffer2[2048];

	TEST_EQUAL_OR(f1.BytesLeftToRead(), f2.BytesLeftToRead(), return false);

	while(f1.StreamDataLeft())
	{
		int s = f1.Read(buffer1, sizeof(buffer1));
		TEST_THAT_OR(f2.Read(buffer2, s) == s, return false);
		TEST_THAT_OR(::memcmp(buffer1, buffer2, s) == 0, return false);
	}

	TEST_THAT_OR(!f2.StreamDataLeft(), return false);

	return true;
}

void create_test_files()
{
	// Create first file
	{
		make_random_data(buffer, FIRST_FILE_SIZE, 98);
		FileStream out("testfiles/0.test", O_WRONLY | O_CREAT);
		out.Write(buffer, FIRST_FILE_SIZE);
	}

	// Create other files
	int seed = 987;
	for(unsigned int f = 1; f < NUMBER_FILES; ++f)
	{
		// Open files
		char fnp[64];
		sprintf(fnp, "testfiles/%d.test", f - 1);
		FileStream previous(fnp);
		char fnt[64];
		sprintf(fnt, "testfiles/%d.test", f);
		FileStream out(fnt, O_WRONLY | O_CREAT);

		// Copy up to the change point
		int b = previous.Read(buffer, test_files[f].ChangePoint, SHORT_TIMEOUT);
		out.Write(buffer, b);

		// Add new bytes?
		if(test_files[f].InsertBytes > 0)
		{
			make_random_data(buffer, test_files[f].InsertBytes, ++seed);
			out.Write(buffer, test_files[f].InsertBytes);
		}
		// Delete bytes?
		if(test_files[f].DeleteBytes > 0)
		{
			previous.Seek(test_files[f].DeleteBytes, IOStream::SeekType_Relative);
		}
		// Copy rest of data
		b = previous.Read(buffer, BUFFER_SIZE, SHORT_TIMEOUT);
		out.Write(buffer, b);
	}
}

bool test_depends_in_dirs()
{
	SETUP();

	BackupStoreFilenameClear storeFilename("test");

	{
		// Save directory with no dependency info
		BackupStoreDirectory dir(1000, 1001); // some random ids
		dir.AddEntry(storeFilename, 1, 2, 3, BackupStoreDirectory::Entry::Flags_File, 4);
		dir.AddEntry(storeFilename, 1, 3, 3, BackupStoreDirectory::Entry::Flags_File, 4);
		dir.AddEntry(storeFilename, 1, 4, 3, BackupStoreDirectory::Entry::Flags_File, 4);
		dir.AddEntry(storeFilename, 1, 5, 3, BackupStoreDirectory::Entry::Flags_File, 4);
		{
			FileStream out("testfiles/dir.0", O_WRONLY | O_CREAT);
			dir.WriteToStream(out);
		}
		// Add some dependency info to one of them
		BackupStoreDirectory::Entry *en = dir.FindEntryByID(3);
		TEST_THAT(en != 0);
		en->SetDependsOnObject(4);
		// Save again
		{
			FileStream out("testfiles/dir.1", O_WRONLY | O_CREAT);
			dir.WriteToStream(out);
		}
		// Check that the file size increases as expected.
		TEST_THAT(TestGetFileSize("testfiles/dir.1") == (TestGetFileSize("testfiles/dir.0") + (4*16)));
	}
	{
		// Load the directory back in
		BackupStoreDirectory dir2;
		FileStream in("testfiles/dir.1");
		dir2.ReadFromStream(in, SHORT_TIMEOUT);
		// Check entries
		TEST_THAT(dir2.GetNumberOfEntries() == 4);
		for(int i = 2; i <= 5; ++i)
		{
			BackupStoreDirectory::Entry *en = dir2.FindEntryByID(i);
			TEST_THAT(en != 0);
			TEST_THAT(en->GetDependsOnObject() == ((i == 3)?4:0));
			TEST_THAT(en->GetRequiredByObject() == 0);
		}
		dir2.Dump(std::cout, true);
		// Test that numbers go in and out as required
		for(int i = 2; i <= 5; ++i)
		{
			BackupStoreDirectory::Entry *en = dir2.FindEntryByID(i);
			TEST_THAT(en != 0);
			en->SetDependsOnObject(i + 1);
			en->SetRequiredByObject(i - 1);
		}
		// Save
		{
			FileStream out("testfiles/dir.2", O_WRONLY | O_CREAT);
			dir2.WriteToStream(out);
		}
		// Load and check
		{
			BackupStoreDirectory dir3;
			FileStream in("testfiles/dir.2");
			dir3.ReadFromStream(in, SHORT_TIMEOUT);
			dir3.Dump(std::cout, true);
			for(int i = 2; i <= 5; ++i)
			{
				BackupStoreDirectory::Entry *en = dir2.FindEntryByID(i);
				TEST_THAT(en != 0);
				TEST_THAT(en->GetDependsOnObject() == (i + 1));
				TEST_THAT(en->GetRequiredByObject() == (i - 1));
			}
		}
	}

	TEARDOWN();
}

TLSContext context;

bool test_housekeeping_patch_merging(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	std::string storeRootDir;
	int discSet = 0;

	// Open a connection to the server
	BackupStoreContext context(fs, NULL, // mpHousekeeping
		"fake test connection"); // rConnectionDetails
	std::auto_ptr<BackupProtocolLocal2> ap_protocol(
		new BackupProtocolLocal2(context, 0x01234567, false)); // !ReadOnly
	BackupProtocolLocal2& protocol(*ap_protocol);

	// Filename for server
	BackupStoreFilenameClear storeFilename("test");

	// Upload the first file
	{
		std::auto_ptr<IOStream> upload(BackupStoreFile::EncodeFile("testfiles/0.test",
				BackupProtocolListDirectory::RootDirectory, storeFilename));
		std::auto_ptr<BackupProtocolSuccess> stored(protocol.QueryStoreFile(
				BackupProtocolListDirectory::RootDirectory, ModificationTime,
				ModificationTime, 0 /* no diff from file ID */, storeFilename, upload));
		test_files[0].IDOnServer = stored->GetObjectID();
		test_files[0].IsCompletelyDifferent = true;
		ModificationTime += MODIFICATION_TIME_INC;
	}

	// Upload the other files, using the diffing process
	for(unsigned int f = 1; f < NUMBER_FILES; ++f)
	{
		// Get an index for the previous version
		std::auto_ptr<BackupProtocolSuccess> getBlockIndex(protocol.QueryGetBlockIndexByName(
				BackupProtocolListDirectory::RootDirectory, storeFilename));
		int64_t diffFromID = getBlockIndex->GetObjectID();
		TEST_THAT(diffFromID != 0);

		if(diffFromID != 0)
		{
			// Found an old version -- get the index. On Windows we can't keep the block index
			// stream open, because that keeps the source RaidFiles open, which stops us from
			// rewriting them in QueryStoreFile below. So we buffer it:
			CollectInBufferStream block_index;
			protocol.ReceiveStream()->CopyStreamTo(block_index);
			block_index.SetForReading();

			// Diff the file
			char filename[64];
			::sprintf(filename, "testfiles/%d.test", f);
			bool isCompletelyDifferent = false;
			std::auto_ptr<IOStream> patchStream(
				BackupStoreFile::EncodeFileDiff(
					filename,
					BackupProtocolListDirectory::RootDirectory,	/* containing directory */
					storeFilename,
					diffFromID,
					block_index,
					protocol.GetTimeout(),
					NULL, // DiffTimer impl
					0 /* not interested in the modification time */,
					&isCompletelyDifferent));

			// Upload the patch to the store
			std::auto_ptr<BackupProtocolSuccess> stored(protocol.QueryStoreFile(
					BackupProtocolListDirectory::RootDirectory, ModificationTime,
					ModificationTime, isCompletelyDifferent?(0):(diffFromID),
					storeFilename, patchStream));
			ModificationTime += MODIFICATION_TIME_INC;

			// Store details
			test_files[f].IDOnServer = stored->GetObjectID();
			test_files[f].IsCompletelyDifferent = isCompletelyDifferent;
			set_refcount(test_files[f].IDOnServer, 1);

#ifdef WIN32
			printf("ID %I64d, completely different: %s\n",
#else
			printf("ID %lld, completely different: %s\n",
#endif
				test_files[f].IDOnServer,
				test_files[f].IsCompletelyDifferent?"yes":"no");
		}
		else
		{
			::printf("WARNING: Block index not obtained when diffing file %d!\n", f);
		}
	}

	// List the directory from the server, and check that no dependency info is sent -- waste of bytes
	{
		std::auto_ptr<BackupProtocolSuccess> dirreply(protocol.QueryListDirectory(
				BackupProtocolListDirectory::RootDirectory,
				BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
				BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING, false /* no attributes */));
		// Stream
		BackupStoreDirectory dir;
		std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
		dir.ReadFromStream(*dirstream, SHORT_TIMEOUT);

		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next()) != 0)
		{
			TEST_THAT(en->GetDependsOnObject() == 0);
			TEST_THAT(en->GetRequiredByObject() == 0);
			bool found = false;

			for(int tfi = 0; tfi < NUMBER_FILES; tfi++)
			{
				if(test_files[tfi].IDOnServer == en->GetObjectID())
				{
					found = true;
					test_files[tfi].CurrentSizeInBlocks =
						en->GetSizeInBlocks();
					break;
				}
			}

			TEST_LINE(found, "Unexpected file found on server: " <<
				en->GetObjectID());
		}
	}

	// Fill in initial dependency information
	for(unsigned int f = 0; f < NUMBER_FILES; ++f)
	{
		bool newer_exists = (f < (NUMBER_FILES - 1));
		bool older_exists = (f > 0);
		int64_t newer = newer_exists ? test_files[f + 1].IDOnServer : 0;
		int64_t older = older_exists ? test_files[f - 1].IDOnServer : 0;

		if(spec.name() == "s3")
		{
			bool newer_depends_on_us = newer_exists &&
				!test_files[f + 1].IsCompletelyDifferent;
			test_files[f].DependsOn = test_files[f].IsCompletelyDifferent ? 0 : older;
			test_files[f].RequiredBy = newer_depends_on_us ? newer : 0;
		}
		else if(spec.name() == "store")
		{
			bool depends_on_newer = newer_exists &&
				!test_files[f + 1].IsCompletelyDifferent;
			test_files[f].DependsOn = depends_on_newer ? newer : 0;
			test_files[f].RequiredBy = test_files[f].IsCompletelyDifferent ? 0 : older;
		}

		test_files[f].HasBeenDeleted = false;
	}

	// Check the stuff on the server
	int deleteIndex = 0;
	for(int i = 0; ; i++)
	{
		// Unlock the store to allow us to lock and access it directly:
		protocol.QueryFinished();

		// Load up the root directory
		BackupStoreDirectory dir;
		{
			// Take a lock before actually reading files from disk,
			// to avoid them changing under our feet.
			fs.GetLock(30); // try for up to 30 seconds

			std::auto_ptr<IOStream> dirStream(
				fs.GetObject(BACKUPSTORE_ROOT_DIRECTORY_ID)
			);
			dir.ReadFromStream(*dirStream, SHORT_TIMEOUT);
			dir.Dump(std::cout, true);

			// Find the test_files entry for the file that was just deleted:
			int just_deleted = deleteIndex == 0 ? -1 : test_file_remove_order[deleteIndex - 1];

			file_info* p_just_deleted;
			if(just_deleted == 0 || just_deleted == -1)
			{
				p_just_deleted = NULL;
			}
			else
			{
				p_just_deleted = test_files + just_deleted;
				set_refcount(test_files[just_deleted].IDOnServer, 0);
			}

			// Check that dependency info is correct
			for(unsigned int f = 1; f < NUMBER_FILES; ++f)
			{
				//TRACE1("t f = %d\n", f);
				BackupStoreDirectory::Entry *en = dir.FindEntryByID(test_files[f].IDOnServer);
				if(en == 0)
				{
					TEST_LINE(test_files[f].HasBeenDeleted,
						"Test file " << f << " (id " <<
						BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
						") was unexpectedly deleted by housekeeping");
					// check that unreferenced object was removed by
					// housekeeping
					std::string filenameOut;
					StoreStructure::MakeObjectFilename(
						test_files[f].IDOnServer,
						storeRootDir, discSet,
						filenameOut,
						false /* don't bother ensuring the directory exists */);
					std::ostringstream msg;
					msg << "Unreferenced object " <<
						test_files[f].IDOnServer <<
						" was not deleted by housekeeping";
					TEST_LINE(!fs.ObjectExists(test_files[f].IDOnServer),
						msg.str());
				}
				else
				{
					TEST_LINE(!test_files[f].HasBeenDeleted,
						"Test file " << f << " (id " <<
						BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
						") was unexpectedly not deleted by housekeeping");
					TEST_EQUAL_LINE(test_files[f].DependsOn, en->GetDependsOnObject(),
						"Test file " << f << " (id " <<
						BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
						") depends on unexpected object after housekeeping");
					TEST_EQUAL_LINE(test_files[f].RequiredBy, en->GetRequiredByObject(),
						"Test file " << f << " (id " <<
						BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
						") depended on by unexpected object after housekeeping");

					// Test that size is plausible
#ifdef BOX_RELEASE_BUILD
					int minimum_size = (spec.name() == "s3" ? 1 : 40);
#else
					int minimum_size = (spec.name() == "s3" ? 4 : 40);
#endif
					if(en->GetDependsOnObject() == 0)
					{
						// Should be a full file
						TEST_LINE(en->GetSizeInBlocks() >= minimum_size,
							"Test file " << f << " (id " <<
							BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
							") was smaller than expected: "
							"wanted a full file with >= " << minimum_size <<
							" blocks, but found " << en->GetSizeInBlocks());
					}
					else
					{
						// Should be a patch
						TEST_LINE(en->GetSizeInBlocks() <= minimum_size,
							"Test file " << f << " (id " <<
							BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
							") was larger than expected: "
							"wanted a patch file with <= " << minimum_size <<
							" blocks, but found " << en->GetSizeInBlocks());
					}
				}

				// All the files that we've deleted so far should have had
				// HasBeenDeleted set to true.
				if(test_files[f].HasBeenDeleted)
				{
					TEST_LINE(en == NULL, "File " << f << " should have been "
						"deleted by this point")
				}
				else if(en == 0)
				{
					TEST_FAIL_WITH_MESSAGE("File " << f << " has been unexpectedly "
						"deleted, cannot check its size");
				}
				// If the file that was just deleted was a patch that this file depended on,
				// then it should have been merged with this file, which should have made this
				// file larger. But that might not translate to a larger number of blocks.
				else if(test_files[just_deleted].RequiredBy == test_files[f].IDOnServer)
				{
					TEST_LINE(en->GetSizeInBlocks() >= test_files[f].CurrentSizeInBlocks,
						"File " << f << " has been merged with an older patch, "
						"so it should be larger than its previous size of " <<
						test_files[f].CurrentSizeInBlocks << " blocks, but it is " <<
						en->GetSizeInBlocks() << " blocks now");
				}
				else
				{
					// This file should not have changed in size.
					TEST_EQUAL_LINE(test_files[f].CurrentSizeInBlocks, en->GetSizeInBlocks(),
						"File " << f << " unexpectedly changed size");
				}

				if(en != 0)
				{
					// Update test_files to record new size for next pass:
					test_files[f].CurrentSizeInBlocks = en->GetSizeInBlocks();
				}
			}

			fs.ReleaseLock();
		}

		// Pull all the files down, and check that they (still) match the files
		// that we uploaded earlier.
		protocol.Reopen();

		for(unsigned int f = 0; f < NUMBER_FILES; ++f)
		{
			std::cout << "r=" << deleteIndex << ", f=" << f <<
				", id=" << BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
				", blocks=" << test_files[f].CurrentSizeInBlocks <<
				", deleted=" << (test_files[f].HasBeenDeleted ? "true" : "false") <<
				std::endl;

			// Might have been deleted
			if(test_files[f].HasBeenDeleted)
			{
				continue;
			}

			// Filenames
			char filename[64], filename_fetched[64];
			::sprintf(filename, "testfiles/%d.test", f);
			::sprintf(filename_fetched, "testfiles/%d.test.fetched", f);
			EMU_UNLINK(filename_fetched);

			// Fetch the file
			try
			{
				std::auto_ptr<BackupProtocolSuccess> getobj(protocol.QueryGetFile(
					BackupProtocolListDirectory::RootDirectory,
					test_files[f].IDOnServer));
				TEST_THAT(getobj->GetObjectID() == test_files[f].IDOnServer);
			}
			catch(ConnectionException &e)
			{
				TEST_FAIL_WITH_MESSAGE("Failed to get test file " << f <<
					" (id " << BOX_FORMAT_OBJECTID(test_files[f].IDOnServer) <<
					") from server: " << e.what());
				continue;
			}

			// BLOCK
			{
				// Get stream
				std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
				// Get and decode
				BackupStoreFile::DecodeFile(*filestream, filename_fetched, SHORT_TIMEOUT);
			}

			// Test for identicalness
			TEST_THAT(files_identical(filename_fetched, filename));

			// Download the index, and check it looks OK
			{
				std::auto_ptr<BackupProtocolSuccess> getblockindex(protocol.QueryGetBlockIndexByID(test_files[f].IDOnServer));
				TEST_THAT(getblockindex->GetObjectID() == test_files[f].IDOnServer);
				std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
				TEST_THAT(BackupStoreFile::CompareFileContentsAgainstBlockIndex(filename, *blockIndexStream, SHORT_TIMEOUT));
			}
		}

		// Close the connection
		protocol.QueryFinished();

		// Take a lock before modifying the directory
		fs.GetLock();
		fs.GetDirectory(BackupProtocolListDirectory::RootDirectory, dir);

		// Mark one of the elements as deleted
		if(test_file_remove_order[deleteIndex] == -1)
		{
			// Nothing left to do
			break;
		}

		int todel = test_file_remove_order[deleteIndex++];

		std::cout << std::endl << "Pass " << i << ": delete file " << todel <<
			" (ID " << test_files[todel].IDOnServer << ")" << std::endl;

		// Modify the entry
		BackupStoreDirectory::Entry *pentry = dir.FindEntryByID(test_files[todel].IDOnServer);
		TEST_LINE_OR(pentry != 0, "Cannot delete test file " << todel << " (id " <<
			BOX_FORMAT_OBJECTID(test_files[todel].IDOnServer) << "): not found on server",
			break);

		pentry->AddFlags(BackupStoreDirectory::Entry::Flags_RemoveASAP);
		fs.PutDirectory(dir);

		// Get the revision number of the root directory, before we release
		// the lock (and therefore before housekeeping makes any changes).
		int64_t first_revision = 0;
		TEST_THAT(fs.ObjectExists(BackupProtocolListDirectory::RootDirectory,
			&first_revision));
		fs.ReleaseLock();

		// Housekeeping wants to open both a temporary and a permanent refcount DB,
		// and after committing the temporary one, it becomes the permanent one and
		// not ReadOnly, and the BackupFileSystem does not allow opening another
		// temporary refcount DB if the permanent one is open for writing (with good
		// reason), so we need to close it here so that housekeeping can open it
		// again, read-only, on the second and subsequent passes.
		if(fs.GetCurrentRefCountDatabase() != NULL)
		{
			fs.CloseRefCountDatabase(fs.GetCurrentRefCountDatabase());
		}

		TEST_THAT(run_housekeeping_and_check_account(fs));

		// Flag for test
		test_files[todel].HasBeenDeleted = true;

		// Update dependency info. When a file is deleted, find the last non-deleted file
		// before it, and the first non-deleted file after it, and replace one of their
		// dependencies with the same one from the deleted file. Which one depends on the
		// specialisation: bbstored files are stored as reverse patches, so they depend on
		// the newer version, but s3 files are stored as forward patches, so they depend on
		// the older version.
		int older;
		bool older_is_completely_different;
		for(older = todel - 1; older >= 0; older--)
		{
			// If this file is deleted, but was completely different, then its blocks
			// have been incorporated into the later file (s3 stores), so that file is
			// now completely different.
			if(spec.name() == "store")
			{
				older_is_completely_different = test_files[older].IsCompletelyDifferent;
			}
			else
			{
				older_is_completely_different |= test_files[older].IsCompletelyDifferent;
			}

			if(!test_files[older].HasBeenDeleted)
			{
				break;
			}
		}

		int newer;
		bool newer_is_completely_different;
		for(newer = todel + 1; newer < (int)NUMBER_FILES; newer++)
		{
			// If this file is deleted, but was completely different, then its blocks
			// have been incorporated into the older file (backupstore stores), so that
			// file is now completely different.
			if(spec.name() == "s3")
			{
				newer_is_completely_different = test_files[newer].IsCompletelyDifferent;
			}
			else
			{
				newer_is_completely_different |= test_files[newer].IsCompletelyDifferent;
			}


			if(!test_files[newer].HasBeenDeleted)
			{
				break;
			}
		}

		if(spec.name() == "s3")
		{
			if(newer < (int)NUMBER_FILES)
			{
				// If this file is deleted, but was completely different, then its
				// blocks have been incorporated into the newer file, so that file
				// is now completely different.
				test_files[newer].IsCompletelyDifferent |=
					test_files[todel].IsCompletelyDifferent;
				test_files[newer].DependsOn =
					(older >= 0 && !test_files[newer].IsCompletelyDifferent) ?
					test_files[older].IDOnServer : 0;
			}

			if(older >= 0)
			{
				test_files[older].RequiredBy =
					(newer < (int)NUMBER_FILES && !test_files[newer].IsCompletelyDifferent) ?
					test_files[newer].IDOnServer : 0;
			}
		}
		else if(spec.name() == "store")
		{
			if(newer < (int)NUMBER_FILES)
			{
				// If this file is deleted, but was completely different, then its
				// blocks have been incorporated into the older file, so that file
				// is now completely different. But because replaced objects are
				// converted to reverse diffs, if 0x8 is completely different on
				// upload then 0x7 becomes completely different on the store, and
				// thus doesn't depend on 0x8. So we propagate IsCompletelyDifferent
				// to the newer file, even though it isn't modified by deleting
				// todel, because that's where we look to determine if the older
				// file should depend on it or not.
				test_files[newer].IsCompletelyDifferent |=
					test_files[todel].IsCompletelyDifferent;
			}

			if(older >= 0)
			{
				test_files[older].DependsOn =
					(newer < (int)NUMBER_FILES && !test_files[newer].IsCompletelyDifferent) ?
					test_files[newer].IDOnServer : 0;
			}

			if(newer < (int)NUMBER_FILES)
			{
				test_files[newer].RequiredBy =
					(older >= 0 && !test_files[newer].IsCompletelyDifferent) ?
					test_files[older].IDOnServer : 0;
			}
		}
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

int test(int argc, const char *argv[])
{
	// Allocate a buffer
	buffer = ::malloc(BUFFER_SIZE);
	TEST_THAT(buffer != 0);

	// SSL library
	SSLLib::Initialise();

	// Use the setup crypto command to set up all these keys, so that the bbackupquery command can be used
	// for seeing what's going on.
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	// Trace errors out
	SET_DEBUG_SSLLIB_TRACE_ERRORS

	// Initialise the raid file controller
	RaidFileController &rcontroller = RaidFileController::GetController();
	rcontroller.Initialise("testfiles/raidfile.conf");

	// Context
	context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	std::auto_ptr<Configuration> s3config = load_config_file(
		DEFAULT_BBACKUPD_CONFIG_FILE, BackupDaemonConfigVerify);
	// Use an auto_ptr so we can release it, and thus the lock, before stopping the
	// daemon on which locking relies:
	std::auto_ptr<S3BackupAccountControl> ap_s3control(
		new S3BackupAccountControl(*s3config));

	std::auto_ptr<Configuration> storeconfig = load_config_file(
		DEFAULT_BBSTORED_CONFIG_FILE, BackupConfigFileVerify);
	BackupStoreAccountControl storecontrol(*storeconfig, 0x01234567);

	TEST_THAT(kill_running_daemons());
	TEST_THAT(StartSimulator());

	std::auto_ptr<RaidAndS3TestSpecs> specs(
		new RaidAndS3TestSpecs(DEFAULT_BBACKUPD_CONFIG_FILE));

	// Create test files
	create_test_files();

	// Check the basic directory stuff works
	TEST_THAT(test_depends_in_dirs());

	// Run all tests that take a RaidAndS3TestSpecs::Specialisation argument twice, once with
	// each specialisation that we have (S3 and BackupStore).

	for(auto i = specs->specs().begin(); i != specs->specs().end(); i++)
	{
		TEST_THAT(test_housekeeping_patch_merging(*i));
	}

	::free(buffer);

	// Release lock before shutting down the simulator:
	ap_s3control.reset();
	TEST_THAT(StopSimulator());

	return finish_test_suite();
}
