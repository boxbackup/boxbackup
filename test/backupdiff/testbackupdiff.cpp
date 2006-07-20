// --------------------------------------------------------------------------
//
// File
//		Name:    testbackupdiff.cpp
//		Purpose: Test diffing routines for backup store files
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <string.h>

#include "Test.h"
#include "BackupClientCryptoKeys.h"
#include "BackupStoreFile.h"
#include "BackupStoreFilenameClear.h"
#include "FileStream.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreObjectMagic.h"
#include "BackupStoreFileCryptVar.h"
#include "BackupStoreException.h"
#include "CollectInBufferStream.h"

#include "MemLeakFindOn.h"

using namespace BackupStoreFileCryptVar;


// from another file
void create_test_files();

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

void make_file_of_zeros(const char *filename, size_t size)
{
	static const size_t bs = 0x10000;
	size_t remSize = size;
	void *b = malloc(bs);
	memset(b, 0, bs);
	FILE *f = fopen(filename, "wb");

	// Using largish blocks like this is much faster, while not consuming too much RAM
	while(remSize > bs)
	{
		fwrite(b, bs, 1, f);
		remSize -= bs;
	}
	fwrite(b, remSize, 1, f);

	fclose(f);
	free(b);

	TEST_THAT((size_t)TestGetFileSize(filename) == size);
}


void check_encoded_file(const char *filename, int64_t OtherFileID, int new_blocks_expected, int old_blocks_expected)
{
	FileStream enc(filename);
	
	// Use the interface verify routine
	int64_t otherIDFromFile = 0;
	TEST_THAT(BackupStoreFile::VerifyEncodedFileFormat(enc, &otherIDFromFile));
	TEST_THAT(otherIDFromFile == OtherFileID);
	
	// Now do our own reading
	enc.Seek(0, IOStream::SeekType_Absolute);
	BackupStoreFile::MoveStreamPositionToBlockIndex(enc);
	// Read in header to check magic value is as expected
	file_BlockIndexHeader hdr;
	TEST_THAT(enc.ReadFullBuffer(&hdr, sizeof(hdr), 0));
	TEST_THAT(hdr.mMagicValue == (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1));
	TEST_THAT((uint64_t)box_ntoh64(hdr.mOtherFileID) == (uint64_t)OtherFileID);
	// number of blocks
	int64_t nblocks = box_ntoh64(hdr.mNumBlocks);
	TRACE2("Reading index from '%s', has %lld blocks\n", filename, nblocks);
	TRACE0("======== ===== ========== ======== ========\n   Index Where  EncSz/Idx     Size  WChcksm\n");
	// Read them all in
	int64_t nnew = 0, nold = 0;
	for(int64_t b = 0; b < nblocks; ++b)
	{
		file_BlockIndexEntry en;
		TEST_THAT(enc.ReadFullBuffer(&en, sizeof(en), 0));
		int64_t s = box_ntoh64(en.mEncodedSize);
		if(s > 0)
		{
			nnew++;
			#ifdef WIN32
			TRACE2("%8I64d this  s=%8I64d", b, s);
			#else
			TRACE2("%8lld this  s=%8lld", b, s);
			#endif
		}
		else
		{
			nold++;
			#ifdef WIN32
			TRACE2("%8I64d other i=%8I64d", b, 0 - s);		
			#else
			TRACE2("%8lld other i=%8lld", b, 0 - s);		
			#endif
		}
		// Decode the rest
		uint64_t iv = box_ntoh64(hdr.mEntryIVBase);
		iv += b;
		sBlowfishDecryptBlockEntry.SetIV(&iv);			
		file_BlockIndexEntryEnc entryEnc;
		sBlowfishDecryptBlockEntry.TransformBlock(&entryEnc, sizeof(entryEnc),
				en.mEnEnc, sizeof(en.mEnEnc));
		TRACE2(" %8d %08x\n", ntohl(entryEnc.mSize), ntohl(entryEnc.mWeakChecksum));
		
	}
	TRACE0("======== ===== ========== ======== ========\n");
	TEST_THAT(new_blocks_expected == nnew);
	TEST_THAT(old_blocks_expected == nold);
}

void test_diff(int from, int to, int new_blocks_expected, int old_blocks_expected, bool expect_completely_different = false)
{
	// First, get the block index of the thing it's comparing against
	char from_encoded[256];
	sprintf(from_encoded, "testfiles/f%d.encoded", from);
	FileStream blockindex(from_encoded);
	BackupStoreFile::MoveStreamPositionToBlockIndex(blockindex);

	// make filenames
	char from_orig[256];
	sprintf(from_orig, "testfiles/f%d", from);
	char to_encoded[256];
	sprintf(to_encoded, "testfiles/f%d.encoded", to);
	char to_diff[256];
	sprintf(to_diff, "testfiles/f%d.diff", to);
	char to_orig[256];
	sprintf(to_orig, "testfiles/f%d", to);
	char rev_diff[256];
	sprintf(rev_diff, "testfiles/f%d.revdiff", to);
	char from_rebuild[256];
	sprintf(from_rebuild, "testfiles/f%d.rebuilt", to);
	char from_rebuild_dec[256];
	sprintf(from_rebuild_dec, "testfiles/f%d.rebuilt_dec", to);
	
	// Then call the encode varient for diffing files
	bool completelyDifferent = !expect_completely_different;	// oposite of what we want
	{
		BackupStoreFilenameClear f1name("filename");
		FileStream out(to_diff, O_WRONLY | O_CREAT | O_EXCL);
		std::auto_ptr<IOStream> encoded(
			BackupStoreFile::EncodeFileDiff(
				to_orig, 
				1 /* dir ID */, 
				f1name,
				1000 + from /* object ID of the file diffing from */, 
				blockindex, 
				IOStream::TimeOutInfinite,
				NULL, // DiffTimer interface
				0, 
				&completelyDifferent));
		encoded->CopyStreamTo(out);
	}
	TEST_THAT(completelyDifferent == expect_completely_different);
	
	// Test that the number of blocks in the file match what's expected
	check_encoded_file(to_diff, expect_completely_different?(0):(1000 + from), new_blocks_expected, old_blocks_expected);

	// filename
	char to_testdec[256];
	sprintf(to_testdec, "testfiles/f%d.testdec", to);
	
	if(!completelyDifferent)
	{
		// Then produce a combined file
		{
			FileStream diff(to_diff);
			FileStream diff2(to_diff);
			FileStream from(from_encoded);
			FileStream out(to_encoded, O_WRONLY | O_CREAT | O_EXCL);
			BackupStoreFile::CombineFile(diff, diff2, from, out);
		}
		
		// And check it
		check_encoded_file(to_encoded, 0, new_blocks_expected + old_blocks_expected, 0);
	}
	else
	{
#ifdef WIN32
		// Emulate the above stage!
		char src[256], dst[256];
		sprintf(src, "testfiles\\f%d.diff", to);
		sprintf(dst, "testfiles\\f%d.encoded", to);
		TEST_THAT(CopyFile(src, dst, FALSE) != 0)
#else
		// Emulate the above stage!
		char cmd[256];
		sprintf(cmd, "cp testfiles/f%d.diff testfiles/f%d.encoded", to, to);
		::system(cmd);
#endif
	}

	// Decode it
	{
		FileStream enc(to_encoded);
		BackupStoreFile::DecodeFile(enc, to_testdec, IOStream::TimeOutInfinite);
		TEST_THAT(files_identical(to_orig, to_testdec));
	}
	
	// Then do some comparisons against the block index
	{
		FileStream index(to_encoded);
		BackupStoreFile::MoveStreamPositionToBlockIndex(index);
		TEST_THAT(BackupStoreFile::CompareFileContentsAgainstBlockIndex(to_orig, index, IOStream::TimeOutInfinite) == true);
	}
	{
		char from_orig[256];
		sprintf(from_orig, "testfiles/f%d", from);
		FileStream index(to_encoded);
		BackupStoreFile::MoveStreamPositionToBlockIndex(index);
		TEST_THAT(BackupStoreFile::CompareFileContentsAgainstBlockIndex(from_orig, index, IOStream::TimeOutInfinite) == files_identical(from_orig, to_orig));
	}
	
	// Check that combined index creation works as expected
	{
		// Load a combined index into memory
		FileStream diff(to_diff);
		FileStream from(from_encoded);
		std::auto_ptr<IOStream> indexCmbStr(BackupStoreFile::CombineFileIndices(diff, from));
		CollectInBufferStream indexCmb;
		indexCmbStr->CopyStreamTo(indexCmb);
		// Then check that it's as expected!
		FileStream result(to_encoded);
		BackupStoreFile::MoveStreamPositionToBlockIndex(result);
		CollectInBufferStream index;
		result.CopyStreamTo(index);
		TEST_THAT(indexCmb.GetSize() == index.GetSize());
		TEST_THAT(::memcmp(indexCmb.GetBuffer(), index.GetBuffer(), index.GetSize()) == 0);
	}
	
	// Check that reverse delta can be made, and that it decodes OK
	{
		// Create reverse delta
		{
			bool reversedCompletelyDifferent = !completelyDifferent;
			FileStream diff(to_diff);
			FileStream from(from_encoded);
			FileStream from2(from_encoded);
			FileStream reversed(rev_diff, O_WRONLY | O_CREAT);
			BackupStoreFile::ReverseDiffFile(diff, from, from2, reversed, to, &reversedCompletelyDifferent);
			TEST_THAT(reversedCompletelyDifferent == completelyDifferent);
		}
		// Use it to combine a file
		{
			FileStream diff(rev_diff);
			FileStream diff2(rev_diff);
			FileStream from(to_encoded);
			FileStream out(from_rebuild, O_WRONLY | O_CREAT | O_EXCL);
			BackupStoreFile::CombineFile(diff, diff2, from, out);
		}
		// And then confirm that this file is actually the one we want
		{
			FileStream enc(from_rebuild);
			BackupStoreFile::DecodeFile(enc, from_rebuild_dec, IOStream::TimeOutInfinite);
			TEST_THAT(files_identical(from_orig, from_rebuild_dec));
		}
		// Do some extra checking
		{
			TEST_THAT(files_identical(from_rebuild, from_encoded));
		}
	}
}

void test_combined_diff(int version1, int version2, int serial)
{
	char combined_file[256];
	char last_diff[256];
	sprintf(last_diff, "testfiles/f%d.diff", version1 + 1);	// ie from version1 to version1 + 1

	for(int v = version1 + 2; v <= version2; ++v)
	{
		FileStream diff1(last_diff);
		char next_diff[256];
		sprintf(next_diff, "testfiles/f%d.diff", v);
		FileStream diff2(next_diff);
		FileStream diff2b(next_diff);
		sprintf(combined_file, "testfiles/comb%d_%d.cmbdiff", version1, v);
		FileStream out(combined_file, O_WRONLY | O_CREAT);
		BackupStoreFile::CombineDiffs(diff1, diff2, diff2b, out);
		strcpy(last_diff, combined_file);
	}

	// Then do a combine on it, and check that it decodes to the right thing
	char orig_enc[256];
	sprintf(orig_enc, "testfiles/f%d.encoded", version1);	
	char combined_out[256];
	sprintf(combined_out, "testfiles/comb%d_%d.out", version1, version2);

	{
		FileStream diff(combined_file);
		FileStream diff2(combined_file);
		FileStream from(orig_enc);
		FileStream out(combined_out, O_WRONLY | O_CREAT);
		BackupStoreFile::CombineFile(diff, diff2, from, out);
	}

	char combined_out_dec[256];
	sprintf(combined_out_dec, "testfiles/comb%d_%d_s%d.dec", version1, version2, serial);
	char to_orig[256];
	sprintf(to_orig, "testfiles/f%d", version2);

	{
		FileStream enc(combined_out);
		BackupStoreFile::DecodeFile(enc, combined_out_dec, IOStream::TimeOutInfinite);
		TEST_THAT(files_identical(to_orig, combined_out_dec));
	}

}

#define MAX_DIFF 9
void test_combined_diffs()
{
	int serial = 0;

	// Number of items to combine at once
	for(int stages = 2; stages <= 4; ++stages)
	{
		// Offset to get complete coverage
		for(int offset = 0; offset < stages; ++offset)
		{
			// And then actual end file number
			for(int f = 0; f <= (MAX_DIFF - stages - offset); ++f)
			{
				// And finally, do something!
				test_combined_diff(offset + f, offset + f + stages, ++serial);
			}
		}
	}
}

int test(int argc, const char *argv[])
{
	// Want to trace out all the details
	#ifndef NDEBUG
	#ifndef WIN32
	BackupStoreFile::TraceDetailsOfDiffProcess = true;
	#endif
	#endif

	// Create all the test files
	create_test_files();

	// Setup the crypto
	BackupClientCryptoKeys_Setup("testfiles/backup.keys");	

	// Encode the first file
	{
		BackupStoreFilenameClear f0name("f0");
		FileStream out("testfiles/f0.encoded", O_WRONLY | O_CREAT | O_EXCL);
		std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/f0", 1 /* dir ID */, f0name));
		encoded->CopyStreamTo(out);
		out.Close();
		check_encoded_file("testfiles/f0.encoded", 0, 33, 0);
	}
	
	// Check the "seek to index" code
	{
		FileStream enc("testfiles/f0.encoded");
		BackupStoreFile::MoveStreamPositionToBlockIndex(enc);
		// Read in header to check magic value is as expected
		file_BlockIndexHeader hdr;
		TEST_THAT(enc.ReadFullBuffer(&hdr, sizeof(hdr), 0));
		TEST_THAT(hdr.mMagicValue == (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1));
	}

	// Diff some files -- parameters are from number, to number,
	// then the number of new blocks expected, and the number of old blocks expected.
	
	// Diff the original file to a copy of itself, and check that there is no data in the file
	// This checks that the hash table is constructed properly, because two of the blocks share
	// the same weak checksum.
	test_diff(0, 1, 0, 33);

	// Insert some new data
	// Blocks from old file moved whole, but put in different order
	test_diff(1, 2, 7, 32);

	// Delete some data, but not on block boundaries
	test_diff(2, 3, 1, 29);

	// Add a very small amount of data, not on block boundary
	// delete a little data
	test_diff(3, 4, 3, 25);

	// 1 byte insertion between two blocks
	test_diff(4, 5, 1, 28);

	// a file with some new content at the very beginning
	// NOTE: You might expect the last numbers to be 2, 29, but the small 1 byte block isn't searched for
	test_diff(5, 6, 3, 28);
	
	// some new content at the very end
	// NOTE: 1 byte block deleted, so number aren't what you'd initial expect.
	test_diff(6, 7, 2, 30);
	
	// a completely different file, with no blocks matching.
	test_diff(7, 8, 14, 0, true /* completely different expected */);
	
	// diff to zero sized file
	test_diff(8, 9, 0, 0, true /* completely different expected */);
	
	// Test that combining diffs works
	test_combined_diffs();
	
	// Check zero sized file works OK to encode on its own, using normal encoding
	{
		{
			// Encode
			BackupStoreFilenameClear fn("filename");
			FileStream out("testfiles/f9.zerotest", O_WRONLY | O_CREAT | O_EXCL);
			std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/f9", 1 /* dir ID */, fn));
			encoded->CopyStreamTo(out);
			out.Close();
			check_encoded_file("testfiles/f9.zerotest", 0, 0, 0);		
		}
		{
			// Decode
			FileStream enc("testfiles/f9.zerotest");
			BackupStoreFile::DecodeFile(enc, "testfiles/f9.testdec.zero", IOStream::TimeOutInfinite);
			TEST_THAT(files_identical("testfiles/f9", "testfiles/f9.testdec.zero"));
		}
	}

#ifndef WIN32	
	// Check that symlinks aren't diffed
	TEST_THAT(::symlink("f2", "testfiles/f2.symlink") == 0)
	// And go and diff it against the previous encoded file
	{
		bool completelyDifferent = false;
		{
			FileStream blockindex("testfiles/f1.encoded");
			BackupStoreFile::MoveStreamPositionToBlockIndex(blockindex);
			
			BackupStoreFilenameClear f1name("filename");
			FileStream out("testfiles/f2.symlink.diff", O_WRONLY | O_CREAT | O_EXCL);
			std::auto_ptr<IOStream> encoded(
				BackupStoreFile::EncodeFileDiff(
					"testfiles/f2.symlink", 
					1 /* dir ID */, 
					f1name,
					1001 /* object ID of the file diffing from */, 
					blockindex, 
					IOStream::TimeOutInfinite,
					NULL, // DiffTimer interface
					0, 
					&completelyDifferent));
			encoded->CopyStreamTo(out);
		}
		TEST_THAT(completelyDifferent == true);
		check_encoded_file("testfiles/f2.symlink.diff", 0, 0, 0);		
	}
#endif

	// Check that diffing against a file which isn't "complete" and 
	// references another isn't allowed
	{
		FileStream blockindex("testfiles/f1.diff");
		BackupStoreFile::MoveStreamPositionToBlockIndex(blockindex);
		
		BackupStoreFilenameClear f1name("filename");
		FileStream out("testfiles/f2.testincomplete", O_WRONLY | O_CREAT | O_EXCL);
		TEST_CHECK_THROWS(BackupStoreFile::EncodeFileDiff("testfiles/f2", 1 /* dir ID */, f1name,
			1001 /* object ID of the file diffing from */, blockindex, IOStream::TimeOutInfinite,
			0, 0), BackupStoreException, CannotDiffAnIncompleteStoreFile);
	}

	// Found a nasty case where files of lots of the same thing 
	// suck up lots of processor time -- because of lots of matches 
	// found. Check this out!
	make_file_of_zeros("testfiles/zero.0", 20*1024*1024);
	make_file_of_zeros("testfiles/zero.1", 200*1024*1024);
	// Generate a first encoded file
	{
		BackupStoreFilenameClear f0name("zero.0");
		FileStream out("testfiles/zero.0.enc", O_WRONLY | O_CREAT | O_EXCL);
		std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("testfiles/zero.0", 1 /* dir ID */, f0name));
		encoded->CopyStreamTo(out);
	}
	// Then diff from it -- time how long it takes...
	{
		int beginTime = time(0);
		FileStream blockindex("testfiles/zero.0.enc");
		BackupStoreFile::MoveStreamPositionToBlockIndex(blockindex);

		BackupStoreFilenameClear f1name("zero.1");
		FileStream out("testfiles/zero.1.enc", O_WRONLY | O_CREAT | O_EXCL);
		std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFileDiff("testfiles/zero.1", 1 /* dir ID */, f1name,
			2000 /* object ID of the file diffing from */, blockindex, IOStream::TimeOutInfinite,
			0, 0));
		encoded->CopyStreamTo(out);

		printf("Time taken: %d seconds\n", time(0) - beginTime);

		#ifdef WIN32
		TEST_THAT(time(0) < (beginTime + 120));
		#else
		TEST_THAT(time(0) < (beginTime + 80));
		#endif
	}
	// Remove zero-files to save disk space
	remove("testfiles/zero.0");
	remove("testfiles/zero.1");

#if 0
	// Code for a nasty real world example! (16Mb files, won't include them in the distribution
	// for obvious reasons...)
	// Generate a first encoded file
	{
		BackupStoreFilenameClear f0name("0000000000000000.old");
		FileStream out("testfiles/0000000000000000.enc.0", O_WRONLY | O_CREAT | O_EXCL);
		std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFile("/Users/ben/Desktop/0000000000000000.old", 1 /* dir ID */, f0name));
		encoded->CopyStreamTo(out);
	}
	// Then diff from it -- time how long it takes...
	{
		int beginTime = time(0);
		FileStream blockindex("testfiles/0000000000000000.enc.0");
		BackupStoreFile::MoveStreamPositionToBlockIndex(blockindex);

		BackupStoreFilenameClear f1name("0000000000000000.new");
		FileStream out("testfiles/0000000000000000.enc.1", O_WRONLY | O_CREAT | O_EXCL);
		std::auto_ptr<IOStream> encoded(BackupStoreFile::EncodeFileDiff("/Users/ben/Desktop/0000000000000000.new", 1 /* dir ID */, f1name,
			2000 /* object ID of the file diffing from */, blockindex, IOStream::TimeOutInfinite,
			0, 0));
		encoded->CopyStreamTo(out);
		TEST_THAT(time(0) < (beginTime + 20));
	}
#endif // 0

	return 0;
}


