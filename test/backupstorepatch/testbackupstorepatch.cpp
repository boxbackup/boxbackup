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

#include "autogen_BackupProtocol.h"
#include "BackupClientCryptoKeys.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileEncodeStream.h"
#include "BackupStoreFilenameClear.h"
#include "BackupStoreInfo.h"
#include "BoxPortsAndFiles.h"
#include "CollectInBufferStream.h"
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
#include "TLSContext.h"
#include "Test.h"

#include "MemLeakFindOn.h"

typedef struct
{
	int ChangePoint, InsertBytes, DeleteBytes;
	int64_t IDOnServer;
	bool IsCompletelyDifferent;
	bool HasBeenDeleted;
	int64_t DepNewer, DepOlder;
} file_info;

file_info test_files[] =
{
//   ChPnt,	Insert,	Delete, ID, IsCDf,	BeenDel
	{0, 	0,		0,		0,	false,	false},	// 0 dummy first entry
	{32000,	2087,	0,		0,	false,	false}, // 1
	{1000,	1998,	2976,	0,	false,	false}, // 2
	{27800,	0,		288,	0,	false,	false}, // 3
	{3208,	1087,	98,		0,	false,	false}, // 4
	{56000,	23087,	98,		0,	false,	false}, // 5
	{0,		98765,	9999999,0,	false,	false},	// 6 completely different, make a break in the storage
	{9899,	9887,	2,		0,	false,	false}, // 7
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

bool files_identical(const char *file1, const char *file2)
{
	FileStream f1(file1);
	FileStream f2(file2);
	
	if(f1.BytesLeftToRead() != f2.BytesLeftToRead())
	{
		return false;
	}
	
	while(f1.StreamDataLeft())
	{
		char buffer1[2048];
		char buffer2[2048];
		int s = f1.Read(buffer1, sizeof(buffer1));
		if(f2.Read(buffer2, s) != s)
		{
			return false;
		}
		if(::memcmp(buffer1, buffer2, s) != 0)
		{
			return false;
		}
	}
	
	if(f2.StreamDataLeft())
	{
		return false;
	}
	
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

void test_depends_in_dirs()
{
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
		en->SetDependsNewer(4);
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
			TEST_THAT(en->GetDependsNewer() == ((i == 3)?4:0));
			TEST_THAT(en->GetDependsOlder() == 0);
		}
		dir2.Dump(0, true);
		// Test that numbers go in and out as required
		for(int i = 2; i <= 5; ++i)
		{
			BackupStoreDirectory::Entry *en = dir2.FindEntryByID(i);
			TEST_THAT(en != 0);
			en->SetDependsNewer(i + 1);
			en->SetDependsOlder(i - 1);
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
			dir3.Dump(0, true);
			for(int i = 2; i <= 5; ++i)
			{
				BackupStoreDirectory::Entry *en = dir2.FindEntryByID(i);
				TEST_THAT(en != 0);
				TEST_THAT(en->GetDependsNewer() == (i + 1));
				TEST_THAT(en->GetDependsOlder() == (i - 1));
			}
		}
	}
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
	TLSContext context;
	context.Initialise(false /* client */,
		"testfiles/clientCerts.pem",
		"testfiles/clientPrivKey.pem",
		"testfiles/clientTrustedCAs.pem",
		0 // SSLSecurityLevel, allow use of our old
		  // hard-coded certificates in tests for now
	);

	// Create an account
	TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS
		" -c testfiles/bbstored.conf "
		"create 01234567 0 30000B 40000B") == 0);
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

	// Create test files
	create_test_files();
	
	// Check the basic directory stuff works
	test_depends_in_dirs();
	
	std::string storeRootDir;
	int discSet = 0;
	{
		std::auto_ptr<BackupStoreAccountDatabase> apDatabase(
			BackupStoreAccountDatabase::Read("testfiles/accounts.txt"));
		BackupStoreAccounts accounts(*apDatabase);
		accounts.GetAccountRoot(0x1234567, storeRootDir, discSet);
	}
	RaidFileDiscSet rfd(rcontroller.GetDiscSet(discSet));

	int pid = LaunchServer(BBSTORED " testfiles/bbstored.conf", 
		"testfiles/bbstored.pid");
	TEST_THAT(pid != -1 && pid != 0);
	if(pid > 0)
	{
		TEST_THAT(ServerIsAlive(pid));

		{
			// Open a connection to the server
			SocketStreamTLS *pConn = new SocketStreamTLS;
			std::auto_ptr<SocketStream> apConn(pConn);
			pConn->Open(context, Socket::TypeINET, "localhost",
				BOX_PORT_BBSTORED_TEST);
	
			// Make a protocol
			BackupProtocolClient protocol(apConn);
	
			// Login
			{
				// Check the version
				std::auto_ptr<BackupProtocolVersion> serverVersion(protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION));
				TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);
	
				// Login
				protocol.QueryLogin(0x01234567, 0);
			}
	
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
					// Found an old version -- get the index
					std::auto_ptr<IOStream> blockIndexStream(protocol.ReceiveStream());
				
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
							*blockIndexStream,
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
					TEST_THAT(en->GetDependsNewer() == 0);
					TEST_THAT(en->GetDependsOlder() == 0);
				}
			}

			// Finish the connection
			protocol.QueryFinished();
		}

		// Fill in initial dependency information
		for(unsigned int f = 0; f < NUMBER_FILES; ++f)
		{
			int64_t newer = (f < (NUMBER_FILES - 1))?test_files[f + 1].IDOnServer:0;
			int64_t older = (f > 0)?test_files[f - 1].IDOnServer:0;
			if(test_files[f].IsCompletelyDifferent)
			{
				older = 0;
			}
			if(f < (NUMBER_FILES - 1) && test_files[f + 1].IsCompletelyDifferent)
			{
				newer = 0;
			}
			test_files[f].DepNewer = newer;
			test_files[f].DepOlder = older;
		}

		// Check the stuff on the server
		int deleteIndex = 0;
		while(true)
		{
			// Load up the root directory
			BackupStoreDirectory dir;
			{
				std::auto_ptr<RaidFileRead> dirStream(RaidFileRead::Open(0, "backup/01234567/o01"));
				dir.ReadFromStream(*dirStream, SHORT_TIMEOUT);
				dir.Dump(0, true);
				
				// Check that dependency info is correct
				for(unsigned int f = 0; f < NUMBER_FILES; ++f)
				{
					//TRACE1("t f = %d\n", f);
					BackupStoreDirectory::Entry *en = dir.FindEntryByID(test_files[f].IDOnServer);
					if(en == 0)
					{
						TEST_THAT(test_files[f].HasBeenDeleted);
						// check that unreferenced
						// object was removed by
						// housekeeping
						std::string filenameOut;
						int startDisc = 0;
						StoreStructure::MakeObjectFilename(
							test_files[f].IDOnServer,
							storeRootDir, discSet,
							filenameOut,
							false /* don't bother ensuring the directory exists */);
						std::ostringstream msg;
						msg << "Unreferenced object " <<
							test_files[f].IDOnServer <<
							" was not deleted by housekeeping";
						TEST_EQUAL_LINE(RaidFileUtil::NoFile,
							RaidFileUtil::RaidFileExists(
								rfd, filenameOut), msg.str());
					}
					else
					{
						TEST_THAT(!test_files[f].HasBeenDeleted);
						TEST_THAT(en->GetDependsNewer() == test_files[f].DepNewer);
						TEST_THAT(en->GetDependsOlder() == test_files[f].DepOlder);
						// Test that size is plausible
						if(en->GetDependsNewer() == 0)
						{
							// Should be a full file
							TEST_THAT(en->GetSizeInBlocks() > 40);
						}
						else
						{
							// Should be a patch
							TEST_THAT(en->GetSizeInBlocks() < 40);
						}
					}
				}
			}
			
			// Open a connection to the server (need to do this each time, otherwise housekeeping won't delete files)
			SocketStreamTLS *pConn = new SocketStreamTLS;
			std::auto_ptr<SocketStream> apConn(pConn);
			pConn->Open(context, Socket::TypeINET, "localhost",
				BOX_PORT_BBSTORED_TEST);
			BackupProtocolClient protocol(apConn);
			{
				std::auto_ptr<BackupProtocolVersion> serverVersion(protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION));
				TEST_THAT(serverVersion->GetVersion() == BACKUP_STORE_SERVER_VERSION);
				protocol.QueryLogin(0x01234567, 0);
			}

			// Pull all the files down, and check that they match the files on disc
			for(unsigned int f = 0; f < NUMBER_FILES; ++f)
			{
				::printf("r=%d, f=%d\n", deleteIndex, f);
				
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
				{
					std::auto_ptr<BackupProtocolSuccess> getobj(protocol.QueryGetFile(
						BackupProtocolListDirectory::RootDirectory,
						test_files[f].IDOnServer));
					TEST_THAT(getobj->GetObjectID() == test_files[f].IDOnServer);
					// BLOCK
					{
						// Get stream
						std::auto_ptr<IOStream> filestream(protocol.ReceiveStream());
						// Get and decode
						BackupStoreFile::DecodeFile(*filestream, filename_fetched, SHORT_TIMEOUT);
					}
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

			// Mark one of the elements as deleted
			if(test_file_remove_order[deleteIndex] == -1)
			{
				// Nothing left to do
				break;
			}
			int todel = test_file_remove_order[deleteIndex++];
			
			// Modify the entry
			BackupStoreDirectory::Entry *pentry = dir.FindEntryByID(test_files[todel].IDOnServer);
			TEST_THAT(pentry != 0);
			pentry->AddFlags(BackupStoreDirectory::Entry::Flags_RemoveASAP);
			// Save it back
			{
				RaidFileWrite writedir(0, "backup/01234567/o01");
				writedir.Open(true /* overwrite */);
				dir.WriteToStream(writedir);
				writedir.Commit(true);
			}

			// Get the revision number of the root directory, before housekeeping makes any changes.
			int64_t first_revision = 0;
			RaidFileRead::FileExists(0, "backup/01234567/o01", &first_revision);

#ifdef WIN32
			// Cannot signal bbstored to do housekeeping now, and we don't need to, as we will
			// wait up to 32 seconds and detect automatically when it has finished.
#else
			// Send the server a restart signal, so it does 
			// housekeeping immediately, and wait for it to happen
			// Wait for old connections to terminate
			::sleep(1);	
			::kill(pid, SIGHUP);
#endif

			// Wait for changes to be written back to the root directory.
			for(int secs_remaining = 32; secs_remaining >= 0; secs_remaining--)
			{
				// Sleep a while, and print a dot
				::sleep(1);
				::printf(".");
				::fflush(stdout);
				
				// Early end?
				if(!TestFileExists("testfiles/0_0/backup/01234567/write.lock"))
				{
					int64_t revid = 0;
					RaidFileRead::FileExists(0, "backup/01234567/o01", &revid);
					if(revid != first_revision)
					{
						break;
					}
				}

				TEST_LINE(secs_remaining != 0, "No changes detected to root directory after 32 seconds");
			}
			::printf("\n");
			
			// Flag for test
			test_files[todel].HasBeenDeleted = true;
			// Update dependency info
			int z = todel;
			while(z > 0 && test_files[z].HasBeenDeleted && test_files[z].DepOlder != 0)
			{
				--z;
			}
			if(z >= 0) test_files[z].DepNewer = test_files[todel].DepNewer;
			z = todel;
			while(z < (int)NUMBER_FILES && test_files[z].HasBeenDeleted && test_files[z].DepNewer != 0)
			{
				++z;
			}
			if(z < (int)NUMBER_FILES) test_files[z].DepOlder = test_files[todel].DepOlder;
		}
		
		// Kill store server
		TEST_THAT(KillServer(pid));
		TEST_THAT(!ServerIsAlive(pid));

		#ifndef WIN32
		TestRemoteProcessMemLeaks("bbstored.memleaks");
		#endif
	}
	
	::free(buffer);
	
	return 0;
}
