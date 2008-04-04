// --------------------------------------------------------------------------
//
// File
//		Name:    test/raidfile/test.cpp  
//		Purpose: Test RaidFile system
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_SYSCALL
#include <sys/syscall.h>
#endif

#include <string.h>

#include "Test.h"
#include "RaidFileController.h"
#include "RaidFileWrite.h"
#include "RaidFileException.h"
#include "RaidFileRead.h"
#include "Guards.h"

#include "MemLeakFindOn.h"

#define RAID_BLOCK_SIZE	2048
#define RAID_NUMBER_DISCS 3

#define TEST_DATA_SIZE	(8*1024 + 173)

#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	#define	TRF_CAN_INTERCEPT
#endif


#ifdef TRF_CAN_INTERCEPT
// function in intercept.cpp for setting up errors
void intercept_setup_error(const char *filename, unsigned int errorafter, int errortoreturn, int syscalltoerror);
bool intercept_triggered();
void intercept_clear_setup();
#endif

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

void testReadingFileContents(int set, const char *filename, void *data, int datasize, bool TestRAIDProperties, int UsageInBlocks = -1)
{
	// Work out which disc is the "start" disc.
	int h = 0;
	int n = 0;
	while(filename[n] != 0)
	{
		h += filename[n];
		n++;
	}
	int startDisc = h % RAID_NUMBER_DISCS;

//printf("UsageInBlocks = %d\n", UsageInBlocks);

	// sizes of data to read
	static int readsizes[] = {2047, 1, 1, 2047, 12, 1, 1, RAID_BLOCK_SIZE - (12+1+1), RAID_BLOCK_SIZE, RAID_BLOCK_SIZE + 246, (RAID_BLOCK_SIZE * 3) + 3, 243};
	
	// read the data in to test it
	char testbuff[(RAID_BLOCK_SIZE * 3) + 128];	// bigger than the max request above!
	std::auto_ptr<RaidFileRead> pread = RaidFileRead::Open(set, filename);
	if(UsageInBlocks != -1)
	{
		TEST_THAT(UsageInBlocks == pread->GetDiscUsageInBlocks());
	}
	//printf("%d, %d\n", pread->GetFileSize(), datasize);
	TEST_THAT(pread->GetFileSize() == datasize);
	IOStream &readstream1 = *(pread.get());	
	int dataread = 0;
	int r;
	int readsize = readsizes[0];
	int bsc = 1;
	while((r = readstream1.Read(testbuff, readsize)) > 0)
	{
		//printf("== read, asked: %d actual: %d\n", readsize, r);
		TEST_THAT(((dataread+r) == datasize) || r == readsize);
		TEST_THAT(r > 0);
		TEST_THAT(readstream1.StreamDataLeft());		// check IOStream interface is correct
		for(int z = 0; z < r; ++z)
		{
			TEST_THAT(((char*)data)[dataread+z] == testbuff[z]);
			/*if(((char*)data)[dataread+z] != testbuff[z])
			{
				printf("z = %d\n", z);
			}*/
		}
		// Next size...
		if(bsc <= (int)((sizeof(readsizes) / sizeof(readsizes[0])) - 1))
		{
			readsize = readsizes[bsc++];
		}
		dataread += r;
	}
	TEST_THAT(dataread == datasize);
	pread->Close();
	
	// open and close it...
	pread.reset(RaidFileRead::Open(set, filename).release());
	if(UsageInBlocks != -1)
	{
		TEST_THAT(UsageInBlocks == pread->GetDiscUsageInBlocks());
	}
	IOStream &readstream2 = *(pread.get());
	
	// positions to try seeking too..
	static int seekpos[] = {0, 1, 2, 887, 887+256 /* no seek required */, RAID_BLOCK_SIZE, RAID_BLOCK_SIZE + 1, RAID_BLOCK_SIZE - 1, RAID_BLOCK_SIZE*3, RAID_BLOCK_SIZE + 23, RAID_BLOCK_SIZE * 4, RAID_BLOCK_SIZE * 4 + 1};
	
	for(unsigned int p = 0; p < (sizeof(seekpos) / sizeof(seekpos[0])); ++p)
	{
		//printf("== seekpos = %d\n", seekpos[p]);
		// only try if test file size is big enough
		if(seekpos[p]+256 > datasize) continue;
		
		readstream2.Seek(seekpos[p], IOStream::SeekType_Absolute);
		TEST_THAT(readstream2.Read(testbuff, 256) == 256);
		TEST_THAT(readstream2.GetPosition() == seekpos[p] + 256);
		TEST_THAT(::memcmp(((char*)data) + seekpos[p], testbuff, 256) == 0);
	}

	// open and close it...
	pread.reset(RaidFileRead::Open(set, filename).release());
	if(UsageInBlocks != -1)
	{
		TEST_THAT(UsageInBlocks == pread->GetDiscUsageInBlocks());
	}
	IOStream &readstream3 = *(pread.get());	
	
	int pos = 0;
	for(unsigned int p = 0; p < (sizeof(seekpos) / sizeof(seekpos[0])); ++p)
	{
		// only try if test file size is big enough
		if(seekpos[p]+256 > datasize) continue;
		
		//printf("pos %d, seekpos %d, p %d\n", pos, seekpos[p], p);
		
		readstream3.Seek(seekpos[p] - pos, IOStream::SeekType_Relative);
		TEST_THAT(readstream3.Read(testbuff, 256) == 256);
		pos = seekpos[p] + 256;
		TEST_THAT(readstream3.GetPosition() == pos);
		TEST_THAT(::memcmp(((char*)data) + seekpos[p], testbuff, 256) == 0);
	}
	
	// Straight read of file
	pread.reset(RaidFileRead::Open(set, filename).release());
	if(UsageInBlocks != -1)
	{
		TEST_THAT(UsageInBlocks == pread->GetDiscUsageInBlocks());
	}
	IOStream &readstream4 = *(pread.get());
	pos = 0;
	int bytesread = 0;
	while((r = readstream4.Read(testbuff, 988)) != 0)
	{
		TEST_THAT(readstream4.StreamDataLeft());		// check IOStream interface is behaving as expected
		
		// check contents
		TEST_THAT(::memcmp(((char*)data) + pos, testbuff, r) == 0);
		
		// move on
		pos += r;
		bytesread += r;
	}
	TEST_THAT(!readstream4.StreamDataLeft());		// check IOStream interface is correct
	pread.reset();

	// Be nasty, and create some errors for the RAID stuff to recover from...
	if(TestRAIDProperties)
	{
		char stripe1fn[256], stripe1fnRename[256];
		sprintf(stripe1fn, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR "%s.rf", set, startDisc, filename);
		sprintf(stripe1fnRename, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR "%s.rf-REMOVED", set, startDisc, 
			filename);
		char stripe2fn[256], stripe2fnRename[256];
		sprintf(stripe2fn, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR "%s.rf", set, 
			(startDisc + 1) % RAID_NUMBER_DISCS, filename);
		sprintf(stripe2fnRename, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR "%s.rf-REMOVED", set, 
			(startDisc + 1) % RAID_NUMBER_DISCS, filename);

		// Read with stripe1 + parity		
		TEST_THAT(::rename(stripe2fn, stripe2fnRename) == 0);
		testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
		TEST_THAT(::rename(stripe2fnRename, stripe2fn) == 0);

		// Read with stripe2 + parity
		TEST_THAT(::rename(stripe1fn, stripe1fnRename) == 0);
		testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
		TEST_THAT(::rename(stripe1fnRename, stripe1fn) == 0);

		// Munged filename for avoidance
		char mungefilename[256];
		char filenamepart[256];
		sprintf(filenamepart, "%s.rf", filename);
		int m = 0, s = 0;
		while(filenamepart[s] != '\0')
		{
			if(filenamepart[s] == '/')
			{
				mungefilename[m++] = '_';
			}
			else if(filenamepart[s] == '_')
			{
				mungefilename[m++] = '_';
				mungefilename[m++] = '_';
			}
			else
			{
				mungefilename[m++] = filenamepart[s];
			}
			s++;
		}
		mungefilename[m++] = '\0';
		char stripe1munge[256];
		sprintf(stripe1munge, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR ".raidfile-unreadable"
			DIRECTORY_SEPARATOR "%s", set, startDisc, 
			mungefilename);
		char stripe2munge[256];
		sprintf(stripe2munge, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR ".raidfile-unreadable"
			DIRECTORY_SEPARATOR "%s", set, 
			(startDisc + 1) % RAID_NUMBER_DISCS, mungefilename);
		

#ifdef TRF_CAN_INTERCEPT
		// Test I/O errors on opening
		// stripe 1
		intercept_setup_error(stripe1fn, 0, EIO, SYS_open);
		testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
		TEST_THAT(intercept_triggered());
		intercept_clear_setup();

		// Check that the file was moved correctly.
		TEST_THAT(TestFileExists(stripe1munge));
		TEST_THAT(::rename(stripe1munge, stripe1fn) == 0);
				
		// Test error in reading stripe 2
		intercept_setup_error(stripe2fn, 0, EIO, SYS_open);
		testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
		TEST_THAT(intercept_triggered());
		intercept_clear_setup();

		// Check that the file was moved correctly.
		TEST_THAT(TestFileExists(stripe2munge));
		TEST_THAT(::rename(stripe2munge, stripe2fn) == 0);

		// Test I/O errors on seeking
		// stripe 1, if the file is bigger than the minimum thing that it'll get seeked for
		if(datasize > 257)
		{
			intercept_setup_error(stripe1fn, 1, EIO, SYS_lseek);
			testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
			TEST_THAT(intercept_triggered());
			intercept_clear_setup();

			// Check that the file was moved correctly.
			TEST_THAT(TestFileExists(stripe1munge));
			TEST_THAT(::rename(stripe1munge, stripe1fn) == 0);
		}

		// Stripe 2, only if the file is big enough to merit this
		if(datasize > (RAID_BLOCK_SIZE + 4))
		{
			intercept_setup_error(stripe2fn, 1, EIO, SYS_lseek);
			testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
			TEST_THAT(intercept_triggered());
			intercept_clear_setup();

			// Check that the file was moved correctly.
			TEST_THAT(TestFileExists(stripe2munge));
			TEST_THAT(::rename(stripe2munge, stripe2fn) == 0);
		}

		// Test I/O errors on read, but only if the file is size greater than 0
		if(datasize > 0)
		{
			// Where shall we error after?
			int errafter = datasize / 4;

			// Test error in reading stripe 1
			intercept_setup_error(stripe1fn, errafter, EIO, SYS_readv);
			testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
			TEST_THAT(intercept_triggered());
			intercept_clear_setup();

			// Check that the file was moved correctly.
			TEST_THAT(TestFileExists(stripe1munge));
			TEST_THAT(::rename(stripe1munge, stripe1fn) == 0);

			// Can only test error if file size > RAID_BLOCK_SIZE, as otherwise stripe2 has nothing in it
			if(datasize > RAID_BLOCK_SIZE)
			{
				// Test error in reading stripe 2
				intercept_setup_error(stripe2fn, errafter, EIO, SYS_readv);
				testReadingFileContents(set, filename, data, datasize, false /* avoid recursion! */, UsageInBlocks);
				TEST_THAT(intercept_triggered());
				intercept_clear_setup();

				// Check that the file was moved correctly.
				TEST_THAT(TestFileExists(stripe2munge));
				TEST_THAT(::rename(stripe2munge, stripe2fn) == 0);
			}
		}
#endif // TRF_CAN_INTERCEPT
	}
}


void testReadWriteFileDo(int set, const char *filename, void *data, int datasize, bool DoTransform)
{
	// Work out which disc is the "start" disc.
	int h = 0;
	int n = 0;
	while(filename[n] != 0)
	{
		h += filename[n];
		n++;
	}
	int startDisc = h % RAID_NUMBER_DISCS;

	// Another to test the transform works OK...
	RaidFileWrite write4(set, filename);
	write4.Open();
	write4.Write(data, datasize);
	// This time, don't discard and transform it to a RAID File
	char writefnPre[256];
	sprintf(writefnPre, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
		DIRECTORY_SEPARATOR "%s.rfwX", set, startDisc, filename);
	TEST_THAT(TestFileExists(writefnPre));
	char writefn[256];
	sprintf(writefn, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
		DIRECTORY_SEPARATOR "%s.rfw", set, startDisc, filename);
	int usageInBlocks = write4.GetDiscUsageInBlocks();
	write4.Commit(DoTransform);
	// Check that files are nicely done...
	if(!DoTransform)
	{
		TEST_THAT(TestFileExists(writefn));
		TEST_THAT(!TestFileExists(writefnPre));
	}
	else
	{
		TEST_THAT(!TestFileExists(writefn));
		TEST_THAT(!TestFileExists(writefnPre));
		// Stripe file sizes
		int fullblocks = datasize / RAID_BLOCK_SIZE;
		int leftover = datasize - (fullblocks * RAID_BLOCK_SIZE);
		int fs1 = -2;
		if((fullblocks & 1) == 0)
		{
			// last block of data will be on the first stripe
			fs1 = ((fullblocks / 2) * RAID_BLOCK_SIZE) + leftover;
		}
		else
		{
			// last block is on second stripe
			fs1 = ((fullblocks / 2)+1) * RAID_BLOCK_SIZE;
		}
		char stripe1fn[256];
		sprintf(stripe1fn, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR "%s.rf", set, startDisc, filename);
		TEST_THAT(TestGetFileSize(stripe1fn) == fs1);
		char stripe2fn[256];
		sprintf(stripe2fn, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR "%s.rf", set, 
			(startDisc + 1) % RAID_NUMBER_DISCS, filename);
		TEST_THAT(TestGetFileSize(stripe2fn) == (int)(datasize - fs1));
		// Parity file size
		char parityfn[256];
		sprintf(parityfn, "testfiles" DIRECTORY_SEPARATOR "%d_%d"
			DIRECTORY_SEPARATOR "%s.rf", set, 
			(startDisc + 2) % RAID_NUMBER_DISCS, filename);
		// Mildly complex calculation
		unsigned int blocks = datasize / RAID_BLOCK_SIZE;
		unsigned int bytesOver = datasize % RAID_BLOCK_SIZE;
		int paritysize = (blocks / 2) * RAID_BLOCK_SIZE;
		// Then add in stuff for the last couple of blocks
		if((blocks & 1) == 0)
		{
			if(bytesOver == 0)
			{
				paritysize += sizeof(RaidFileRead::FileSizeType);
			}
			else
			{
				paritysize += (bytesOver == sizeof(RaidFileRead::FileSizeType))?(RAID_BLOCK_SIZE+sizeof(RaidFileRead::FileSizeType)):bytesOver;
			}
		}
		else
		{
			paritysize += RAID_BLOCK_SIZE;
			if(bytesOver == 0 || bytesOver >= (RAID_BLOCK_SIZE-sizeof(RaidFileRead::FileSizeType)))
			{
				paritysize += sizeof(RaidFileRead::FileSizeType);
			}
		}
		//printf("datasize = %d, calc paritysize = %d, actual size of file = %d\n", datasize, paritysize, TestGetFileSize(parityfn));
		TEST_THAT(TestGetFileSize(parityfn) == paritysize);
		//printf("stripe1 size = %d, stripe2 size = %d, parity size = %d\n", TestGetFileSize(stripe1fn), TestGetFileSize(stripe2fn), TestGetFileSize(parityfn));
	
		// Check that block calculation is correct
		//printf("filesize = %d\n", datasize);
		#define TO_BLOCKS_ROUND_UP(x) (((x) + (RAID_BLOCK_SIZE-1)) / RAID_BLOCK_SIZE)
		TEST_THAT(usageInBlocks == (TO_BLOCKS_ROUND_UP(paritysize) + TO_BLOCKS_ROUND_UP(fs1) + TO_BLOCKS_ROUND_UP(datasize - fs1)));
	
		// See about whether or not the files look correct
		char testblock[1024];	// compiler bug? This can't go in the block below without corrupting stripe2fn...
		if(datasize > (3*1024))
		{
			int f;
			TEST_THAT((f = ::open(stripe1fn, O_RDONLY | O_BINARY, 
				0)) != -1);
			TEST_THAT(sizeof(testblock) == ::read(f, testblock, sizeof(testblock)));
			for(unsigned int q = 0; q < sizeof(testblock); ++q)
			{
				TEST_THAT(testblock[q] == ((char*)data)[q]);
			}
			::close(f);
			TEST_THAT((f = ::open(stripe2fn, O_RDONLY | O_BINARY, 
				0)) != -1);
			TEST_THAT(sizeof(testblock) == ::read(f, testblock, sizeof(testblock)));
			for(unsigned int q = 0; q < sizeof(testblock); ++q)
			{
				TEST_THAT(testblock[q] == ((char*)data)[q+RAID_BLOCK_SIZE]);
			}
			::close(f);
		}
	}
	
	// See if the contents look right
	testReadingFileContents(set, filename, data, datasize, DoTransform /* only test RAID stuff if it has been transformed to RAID */, usageInBlocks);
}

void testReadWriteFile(int set, const char *filename, void *data, int datasize)
{
	// Test once, transforming it...
	testReadWriteFileDo(set, filename, data, datasize, true);
	
	// And then again, not transforming it
	std::string fn(filename);
	fn += "NT";
	testReadWriteFileDo(set, fn.c_str(), data, datasize, false);	
}

bool list_matches(const std::vector<std::string> &rList, const char *compareto[])
{
	// count in compare to
	int count = 0;
	while(compareto[count] != 0)
		count++;
	
	if((int)rList.size() != count)
	{
		return false;
	}
	
	// Space for bools
	bool *found = new bool[count];
	
	for(int c = 0; c < count; ++c)
	{
		found[c] = false;
	}
	
	for(int c = 0; c < count; ++c)
	{
		bool f = false;
		for(int l = 0; l < (int)rList.size(); ++l)
		{
			if(rList[l] == compareto[c])
			{
				f = true;
				break;
			}
		}
		found[c] = f;
	}

	bool ret = true;
	for(int c = 0; c < count; ++c)
	{
		if(found[c] == false)
		{
			ret = false;
		}
	}

	delete [] found;
	
	return ret;
}

void test_overwrites()
{
	// Opening twice is bad
	{
		RaidFileWrite writeA(0, "overwrite_A");
		writeA.Open();
		writeA.Write("TESTTEST", 8);
	
		{
#if defined(HAVE_FLOCK) || HAVE_DECL_O_EXLOCK
			RaidFileWrite writeA2(0, "overwrite_A");
			TEST_CHECK_THROWS(writeA2.Open(), RaidFileException, FileIsCurrentlyOpenForWriting);
#endif
		}
	}
	
	// But opening a file which has previously been open, but isn't now, is OK.
	
	// Generate a random pre-existing write file (and ensure that it doesn't exist already)
	int f;
	TEST_THAT((f = ::open("testfiles" DIRECTORY_SEPARATOR "0_2" 
		DIRECTORY_SEPARATOR "overwrite_B.rfwX", 
		O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0755)) != -1);
	TEST_THAT(::write(f, "TESTTEST", 8) == 8);
	::close(f);

	// Attempt to overwrite it, which should work nicely.
	RaidFileWrite writeB(0, "overwrite_B");
	writeB.Open();
	writeB.Write("TEST", 4);
	TEST_THAT(writeB.GetFileSize() == 4);
	writeB.Commit();
}


int test(int argc, const char *argv[])
{
	#ifndef TRF_CAN_INTERCEPT
		printf("NOTE: Skipping intercept based tests on this platform.\n\n");
	#endif

	// Initialise the controller
	RaidFileController &rcontroller = RaidFileController::GetController();
	rcontroller.Initialise("testfiles" DIRECTORY_SEPARATOR "raidfile.conf");

	// some data
	char data[TEST_DATA_SIZE];
	R250 random(619);
	for(unsigned int l = 0; l < sizeof(data); ++l)
	{
		data[l] = random.next() & 0xff;
	}
	char data2[57];
	for(unsigned int l = 0; l < sizeof(data2); ++l)
	{
		data2[l] = l;
	}
	
	// Try creating a directory
	RaidFileWrite::CreateDirectory(0, "test-dir");
	TEST_THAT(TestDirExists("testfiles" DIRECTORY_SEPARATOR "0_0" 
		DIRECTORY_SEPARATOR "test-dir"));
	TEST_THAT(TestDirExists("testfiles" DIRECTORY_SEPARATOR "0_1"
		DIRECTORY_SEPARATOR "test-dir"));
	TEST_THAT(TestDirExists("testfiles" DIRECTORY_SEPARATOR "0_2"
		DIRECTORY_SEPARATOR "test-dir"));
	TEST_THAT(RaidFileRead::DirectoryExists(0, "test-dir"));
	TEST_THAT(!RaidFileRead::DirectoryExists(0, "test-dir-not"));


	// Test converting to disc set names
	{
		std::string n1(RaidFileController::DiscSetPathToFileSystemPath(0, "testX", 0));
		std::string n2(RaidFileController::DiscSetPathToFileSystemPath(0, "testX", 1));
		std::string n3(RaidFileController::DiscSetPathToFileSystemPath(0, "testX", 2));
		std::string n4(RaidFileController::DiscSetPathToFileSystemPath(0, "testX", 3));
		TEST_THAT(n1 != n2);
		TEST_THAT(n2 != n3);
		TEST_THAT(n1 != n3);
		TEST_THAT(n1 == n4);		// ie wraps around
		TRACE3("Gen paths= '%s','%s',%s'\n", n1.c_str(), n2.c_str(), n3.c_str());
	}

	// Create a RaidFile
	RaidFileWrite write1(0, "test1");
	IOStream &write1stream = write1;	// use the stream interface where possible
	write1.Open();
	write1stream.Write(data, sizeof(data));
	write1stream.Seek(1024, IOStream::SeekType_Absolute);
	write1stream.Write(data2, sizeof(data2));
	write1stream.Seek(1024, IOStream::SeekType_Relative);
	write1stream.Write(data2, sizeof(data2));
	write1stream.Seek(0, IOStream::SeekType_End);
	write1stream.Write(data, sizeof(data));
	
	// Before it's deleted, check to see the contents are as expected
	int f;
	TEST_THAT((f = ::open("testfiles" DIRECTORY_SEPARATOR "0_2"
		DIRECTORY_SEPARATOR "test1.rfwX", O_RDONLY | O_BINARY, 0)) 
		>= 0);
	char buffer[sizeof(data)];
	int bytes_read = ::read(f, buffer, sizeof(buffer));
	TEST_THAT(bytes_read == sizeof(buffer));
	for(unsigned int l = 0; l < 1024; ++l)
	{
		TEST_THAT(buffer[l] == data[l]);
	}
	for(unsigned int l = 0; l < sizeof(data2); ++l)
	{
		TEST_THAT(buffer[l+1024] == data2[l]);
	}
	for(unsigned int l = 0; l < sizeof(data2); ++l)
	{
		TEST_THAT(buffer[l+2048+sizeof(data2)] == data2[l]);
	}
	TEST_THAT(::lseek(f, sizeof(data), SEEK_SET) == sizeof(buffer));
	bytes_read = ::read(f, buffer, sizeof(buffer));
	TEST_THAT(bytes_read == sizeof(buffer));
	for(unsigned int l = 0; l < 1024; ++l)
	{
		TEST_THAT(buffer[l] == data[l]);
	}
	// make sure that's the end of the file
	TEST_THAT(::read(f, buffer, sizeof(buffer)) == 0);
	::close(f);
	
	// Commit the data
	write1.Commit();
	TEST_THAT((f = ::open("testfiles" DIRECTORY_SEPARATOR "0_2"
		DIRECTORY_SEPARATOR "test1.rfw", O_RDONLY | O_BINARY, 0)) 
		>= 0);
	::close(f);

	// Now try and read it
	{
		std::auto_ptr<RaidFileRead> pread = RaidFileRead::Open(0, "test1");
		TEST_THAT(pread->GetFileSize() == sizeof(buffer)*2);
		
		char buffer[sizeof(data)];
		TEST_THAT(pread->Read(buffer, sizeof(buffer)) == sizeof(buffer));
		for(unsigned int l = 0; l < 1024; ++l)
		{
			TEST_THAT(buffer[l] == data[l]);
		}
		for(unsigned int l = 0; l < sizeof(data2); ++l)
		{
			TEST_THAT(buffer[l+1024] == data2[l]);
		}
		for(unsigned int l = 0; l < sizeof(data2); ++l)
		{
			TEST_THAT(buffer[l+2048+sizeof(data2)] == data2[l]);
		}
		pread->Seek(sizeof(data), IOStream::SeekType_Absolute);
		TEST_THAT(pread->Read(buffer, sizeof(buffer)) == sizeof(buffer));
		for(unsigned int l = 0; l < 1024; ++l)
		{
			TEST_THAT(buffer[l] == data[l]);
		}
		// make sure that's the end of the file
		TEST_THAT(pread->Read(buffer, sizeof(buffer)) == 0);
		// Seek backwards a bit
		pread->Seek(-1024, IOStream::SeekType_Relative);
		TEST_THAT(pread->Read(buffer, 1024) == 1024);
		// make sure that's the end of the file
		TEST_THAT(pread->Read(buffer, sizeof(buffer)) == 0);
		// Test seeking to end works
		pread->Seek(-1024, IOStream::SeekType_Relative);
		TEST_THAT(pread->Read(buffer, 512) == 512);
		pread->Seek(0, IOStream::SeekType_End);
		TEST_THAT(pread->Read(buffer, sizeof(buffer)) == 0);
	}
	
	// Delete it
	RaidFileWrite writeDel(0, "test1");
	writeDel.Delete();

	// And again...
	RaidFileWrite write2(0, "test1");
	write2.Open();
	write2.Write(data, sizeof(data));
	// This time, discard it
	write2.Discard();
	TEST_THAT((f = ::open("testfiles" DIRECTORY_SEPARATOR "0_2"
		DIRECTORY_SEPARATOR "test1.rfw", O_RDONLY | O_BINARY, 0)) 
		== -1);
	
	// And leaving it there...
	RaidFileWrite writeLeave(0, "test1");
	writeLeave.Open();
	writeLeave.Write(data, sizeof(data));
	// This time, commit it
	writeLeave.Commit();
	TEST_THAT((f = ::open("testfiles" DIRECTORY_SEPARATOR "0_2" 
		DIRECTORY_SEPARATOR "test1.rfw", O_RDONLY | O_BINARY, 0)) 
		!= -1);
	::close(f);
	
	// Then check that the thing will refuse to open it again.
	RaidFileWrite write3(0, "test1");
	TEST_CHECK_THROWS(write3.Open(), RaidFileException, CannotOverwriteExistingFile);
	
	// Test overwrite behaviour
	test_overwrites();

	// Then... open it again allowing overwrites
	RaidFileWrite write3b(0, "test1");
	write3b.Open(true);
	// Write something
	write3b.Write(data + 3, sizeof(data) - 3);
	write3b.Commit();
	// Test it
	testReadingFileContents(0, "test1", data+3, sizeof(data) - 3, false 
		/* TestRAIDProperties */);

	// And once again, but this time making it a raid file
	RaidFileWrite write3c(0, "test1");
	write3c.Open(true);
	// Write something
	write3c.Write(data + 7, sizeof(data) - 7);
	write3c.Commit(true);	// make RAID
	// Test it
	testReadingFileContents(0, "test1", data+7, sizeof(data) - 7, false 
		/*TestRAIDProperties*/);

	// Test opening a file which doesn't exist
	TEST_CHECK_THROWS(
		std::auto_ptr<RaidFileRead> preadnotexist = RaidFileRead::Open(1, "doesnt-exist"),
		RaidFileException, RaidFileDoesntExist);

	{
		// Test unrecoverable damage
		RaidFileWrite w(0, "damage");
		w.Open();
		w.Write(data, sizeof(data));
		w.Commit(true);

		// Try removing the parity file
		TEST_THAT(::rename("testfiles" DIRECTORY_SEPARATOR "0_0"
			DIRECTORY_SEPARATOR "damage.rf", 
			"testfiles" DIRECTORY_SEPARATOR "0_0"
			DIRECTORY_SEPARATOR "damage.rf-NT") == 0);
		{
			std::auto_ptr<RaidFileRead> pr0 = RaidFileRead::Open(0, "damage");
			pr0->Read(buffer, sizeof(data));
		}		
		TEST_THAT(::rename("testfiles" DIRECTORY_SEPARATOR "0_0" DIRECTORY_SEPARATOR "damage.rf-NT", "testfiles" DIRECTORY_SEPARATOR "0_0" DIRECTORY_SEPARATOR "damage.rf") == 0);
	
		// Delete one of the files
		TEST_THAT(::unlink("testfiles" DIRECTORY_SEPARATOR "0_1" DIRECTORY_SEPARATOR "damage.rf") == 0); // stripe 1
		
#ifdef TRF_CAN_INTERCEPT
		// Open it and read...
		{
			intercept_setup_error("testfiles" DIRECTORY_SEPARATOR "0_2" DIRECTORY_SEPARATOR "damage.rf", 0, EIO, SYS_read);	// stripe 2
			std::auto_ptr<RaidFileRead> pr1 = RaidFileRead::Open(0, "damage");
			TEST_CHECK_THROWS(
				pr1->Read(buffer, sizeof(data)),
				RaidFileException, OSError);

			TEST_THAT(intercept_triggered());
			intercept_clear_setup();
		}
#endif //TRF_CAN_INTERCEPT

		// Delete another
		TEST_THAT(::unlink("testfiles" DIRECTORY_SEPARATOR "0_0" DIRECTORY_SEPARATOR "damage.rf") == 0); // parity
		
		TEST_CHECK_THROWS(
			std::auto_ptr<RaidFileRead> pread2 = RaidFileRead::Open(0, "damage"),
			RaidFileException, FileIsDamagedNotRecoverable);
	}

	// Test reading a directory
	{
		RaidFileWrite::CreateDirectory(0, "dirread");
		// Make some contents
		RaidFileWrite::CreateDirectory(0, "dirread" DIRECTORY_SEPARATOR "dfsdf1");
		RaidFileWrite::CreateDirectory(0, "dirread" DIRECTORY_SEPARATOR "ponwq2");
		{
			RaidFileWrite w(0, "dirread" DIRECTORY_SEPARATOR "sdf9873241");
			w.Open();
			w.Write(data, sizeof(data));
			w.Commit(true);
		}
		{
			RaidFileWrite w(0, "dirread" DIRECTORY_SEPARATOR "fsdcxjni3242");
			w.Open();
			w.Write(data, sizeof(data));
			w.Commit(true);
		}
		{
			RaidFileWrite w(0, "dirread" DIRECTORY_SEPARATOR "cskjnds3");
			w.Open();
			w.Write(data, sizeof(data));
			w.Commit(false);
		}
		
		const static char *dir_list1[] = {"dfsdf1", "ponwq2", 0};
		const static char *file_list1[] = {"sdf9873241", "fsdcxjni3242", "cskjnds3", 0};
		const static char *file_list2[] = {"fsdcxjni3242", "cskjnds3", 0};
		
		std::vector<std::string> names;
		TEST_THAT(true == RaidFileRead::ReadDirectoryContents(0, std::string("dirread"), RaidFileRead::DirReadType_FilesOnly, names));
		TEST_THAT(list_matches(names, file_list1));
		TEST_THAT(true == RaidFileRead::ReadDirectoryContents(0, std::string("dirread"), RaidFileRead::DirReadType_DirsOnly, names));
		TEST_THAT(list_matches(names, dir_list1));
		// Delete things
		TEST_THAT(::unlink("testfiles" DIRECTORY_SEPARATOR "0_0" DIRECTORY_SEPARATOR "dirread" DIRECTORY_SEPARATOR "sdf9873241.rf") == 0);
		TEST_THAT(true == RaidFileRead::ReadDirectoryContents(0, std::string("dirread"), RaidFileRead::DirReadType_FilesOnly, names));
		TEST_THAT(list_matches(names, file_list1));
		// Delete something else so that it's not recoverable
		TEST_THAT(::unlink("testfiles" DIRECTORY_SEPARATOR "0_1" DIRECTORY_SEPARATOR "dirread" DIRECTORY_SEPARATOR "sdf9873241.rf") == 0);
		TEST_THAT(false == RaidFileRead::ReadDirectoryContents(0, std::string("dirread"), RaidFileRead::DirReadType_FilesOnly, names));
		TEST_THAT(list_matches(names, file_list1));
		// And finally...
		TEST_THAT(::unlink("testfiles" DIRECTORY_SEPARATOR "0_2" DIRECTORY_SEPARATOR "dirread" DIRECTORY_SEPARATOR "sdf9873241.rf") == 0);
		TEST_THAT(true == RaidFileRead::ReadDirectoryContents(0, std::string("dirread"), RaidFileRead::DirReadType_FilesOnly, names));
		TEST_THAT(list_matches(names, file_list2));
	}

	// Check that sizes are reported correctly for non-raid discs
	{
		int sizeInBlocks = (sizeof(data) + RAID_BLOCK_SIZE - 1) / RAID_BLOCK_SIZE;
		// for writing
		{
			RaidFileWrite write(2, "testS");
			write.Open();
			write.Write(data, sizeof(data));
			TEST_THAT(write.GetDiscUsageInBlocks() == sizeInBlocks);
			write.Commit();
		}
		// for reading
		{
			std::auto_ptr<RaidFileRead> pread(RaidFileRead::Open(2, "testS"));
			TEST_THAT(pread->GetDiscUsageInBlocks() == sizeInBlocks);
		}
	}
	
//printf("SKIPPING tests ------------------\n");
//return 0;

	// Test a load of transformed things
	#define BIG_BLOCK_SIZE (25*1024 + 19)
	MemoryBlockGuard<void*> bigblock(BIG_BLOCK_SIZE);
	R250 randomX2(2165);
	for(unsigned int l = 0; l < BIG_BLOCK_SIZE; ++l)
	{
		((char*)(void*)bigblock)[l] = randomX2.next() & 0xff;
	}
	
	// First on one size of data, on different discs
	testReadWriteFile(0, "testdd", data, sizeof(data));
	testReadWriteFile(0, "test2", bigblock, BIG_BLOCK_SIZE);
	testReadWriteFile(1, "testThree", bigblock, BIG_BLOCK_SIZE - 2048);
	testReadWriteFile(1, "testX", bigblock, BIG_BLOCK_SIZE - 2289);
	testReadWriteFile(1, "testSmall0", data, 0);
	testReadWriteFile(1, "testSmall1", data, 1);
	testReadWriteFile(1, "testSmall2", data, 2);
	testReadWriteFile(1, "testSmall3", data, 3);
	testReadWriteFile(1, "testSmall4", data, 4);
	testReadWriteFile(0, "testSmall5", data, 5);
	testReadWriteFile(0, "testSmall6", data, 6);
	testReadWriteFile(1, "testSmall7", data, 7);
	testReadWriteFile(1, "testSmall8", data, 8);
	testReadWriteFile(1, "testSmall9", data, 9);
	testReadWriteFile(1, "testSmall10", data, 10);
	// See about a file which is one block bigger than the previous tests
	{
		char dataonemoreblock[TEST_DATA_SIZE + RAID_BLOCK_SIZE];
		R250 random(715);
		for(unsigned int l = 0; l < sizeof(dataonemoreblock); ++l)
		{
			dataonemoreblock[l] = random.next() & 0xff;
		}
		testReadWriteFile(0, "testfour", dataonemoreblock, sizeof(dataonemoreblock));
	}
	
	// Some more nasty sizes
	static int nastysize[] = {0, 1, 2, 7, 8, 9, (RAID_BLOCK_SIZE/2)+3,
			RAID_BLOCK_SIZE-9, RAID_BLOCK_SIZE-8, RAID_BLOCK_SIZE-7, RAID_BLOCK_SIZE-6, RAID_BLOCK_SIZE-5,
			RAID_BLOCK_SIZE-4, RAID_BLOCK_SIZE-3, RAID_BLOCK_SIZE-2, RAID_BLOCK_SIZE-1};
	for(int o = 0; o <= 5; ++o)
	{
		for(unsigned int n = 0; n < (sizeof(nastysize)/sizeof(nastysize[0])); ++n)
		{
			int s = (o*RAID_BLOCK_SIZE)+nastysize[n];
			char fn[64];
			sprintf(fn, "testN%d", s);
			testReadWriteFile(n&1, fn, bigblock, s);
		}
	}
	
	// Finally, a mega test (not necessary for every run, I would have thought)
/*	unsigned int megamax = (1024*128) + 9;
	MemoryBlockGuard<void*> megablock(megamax);
	R250 randomX3(183);
	for(unsigned int l = 0; l < megamax; ++l)
	{
		((char*)(void*)megablock)[l] = randomX3.next() & 0xff;
	}
	for(unsigned int s = 0; s < megamax; ++s)
	{
		testReadWriteFile(s & 1, "mega", megablock, s);
		RaidFileWrite deleter(s & 1, "mega");
		deleter.Delete();
		RaidFileWrite deleter2(s & 1, "megaNT");
		deleter2.Delete();
	}*/
	
	return 0;
}
