// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
// --------------------------------------------------------------------------
//
// File
//		Name:    createtestfiles.cpp
//		Purpose: Create the test files for the backupdiff test
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "FileStream.h"
#include "PartialReadStream.h"
#include "Test.h"
#include "RollingChecksum.h"

#include "MemLeakFindOn.h"

#define ACT_END		0
#define ACT_COPY	1
#define	ACT_NEW		2
#define	ACT_SKIP	3
#define ACT_COPYEND	4

typedef struct
{
	int action, length, seed;
} gen_action;

#define INITIAL_FILE_LENGTH (128*1024 + 342)


gen_action file1actions[] = {
	{ACT_COPYEND, 0, 0},
	{ACT_END, 0, 0} };

gen_action file2actions[] = {
	{ACT_COPY, 16*1024, 0},
	// Do blocks on block boundaries, but swapped around a little
	{ACT_SKIP, 4*1024, 0},
	{ACT_COPY, 8*1024, 0},
	{ACT_SKIP, -12*1024, 0},
	{ACT_COPY, 4*1024, 0},
	{ACT_SKIP, 8*1024, 0},
	// Get rest of file with some new data inserted
	{ACT_COPY, 37*1024 + 12, 0},
	{ACT_NEW, 23*1024 + 129, 23990},
	{ACT_COPYEND, 0, 0},
	{ACT_END, 0, 0} };

gen_action file3actions[] = {
	{ACT_COPY, 12*1024 + 983, 0},
	{ACT_SKIP, 37*1024 + 12, 0},
	{ACT_COPYEND, 0, 0},
	{ACT_END, 0, 0} };

gen_action file4actions[] = {
	{ACT_COPY, 20*1024 + 2385, 0},
	{ACT_NEW, 12, 2334},
	{ACT_COPY, 16*1024 + 385, 0},
	{ACT_SKIP, 9*1024 + 42, 0},
	{ACT_COPYEND, 0, 0},
	{ACT_END, 0, 0} };

// insert 1 byte a block into the file, between two other blocks
gen_action file5actions[] = {
	{ACT_COPY, 4*1024, 0},
	{ACT_NEW, 1, 2334},
	{ACT_COPYEND, 0, 0},
	{ACT_END, 0, 0} };

gen_action file6actions[] = {
	{ACT_NEW, 6*1024, 12353452},
	{ACT_COPYEND, 0, 0},
	{ACT_END, 0, 0} };

// but delete that one byte block, it's annoying
gen_action file7actions[] = {
	{ACT_COPY, 10*1024, 0},
	{ACT_SKIP, 1, 0},
	{ACT_COPYEND, 0, 0},
	{ACT_NEW, 7*1024, 1235352},
	{ACT_END, 0, 0} };

gen_action file8actions[] = {
	{ACT_NEW, 54*1024 + 9, 125352},
	{ACT_END, 0, 0} };

gen_action file9actions[] = {
	{ACT_END, 0, 0} };

gen_action *testfiles[] = {file1actions, file2actions, file3actions, file4actions,
	file5actions, file6actions, file7actions, file8actions, file9actions, 0};


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

void make_random_data(void *buffer, int size, int seed)
{
	R250 rand(seed);

	int n = size / sizeof(int);
	int *b = (int*)buffer;
	for(int l = 0; l < n; ++l)
	{
		b[l] = rand.next();
	}
}

void write_test_data(IOStream &rstream, int size, int seed)
{
	R250 rand(seed);
	
	while(size > 0)
	{
		// make a nice buffer of data
		int buffer[2048/sizeof(int)];
		for(unsigned int l = 0; l < (sizeof(buffer) / sizeof(int)); ++l)
		{
			buffer[l] = rand.next();
		}
		
		// Write out...
		unsigned int w = size;
		if(w > sizeof(buffer)) w = sizeof(buffer);
		rstream.Write(buffer, w);
		
		size -= w;
	}	
}

void gen_varient(IOStream &out, char *sourcename, gen_action *pact)
{
	// Open source
	FileStream source(sourcename);
	
	while(true)
	{
		switch(pact->action)
		{
		case ACT_END:
			{
				// all done
				return;
			}
		case ACT_COPY:
			{
				PartialReadStream copy(source, pact->length);
				copy.CopyStreamTo(out);
				break;
			}
		case ACT_NEW:
			{
				write_test_data(out, pact->length, pact->seed);
				break;
			}
		case ACT_SKIP:
			{
				source.Seek(pact->length, IOStream::SeekType_Relative);
				break;
			}
		case ACT_COPYEND:
			{
				source.CopyStreamTo(out);
				break;
			}
		}
	
		++pact;
	}
}

void create_test_files()
{
	// First, the keys for the crypto
	{
		FileStream keys("testfiles/backup.keys", O_WRONLY | O_CREAT);
		write_test_data(keys, 1024, 237);
	}
	
	// Create the initial file -- needs various special properties...
	// 1) Two blocks much be the different, but have the same weak checksum
	// 2) A block must exist twice, but at an offset which isn't a multiple of the block size.
	{
		FileStream f0("testfiles/f0", O_WRONLY | O_CREAT);
		// Write first bit.
		write_test_data(f0, (16*1024), 20012);
		// Now repeated checksum blocks
		uint8_t blk[4096];
		make_random_data(blk, sizeof(blk), 12201);
		// Three magic numbers which make the checksum work: Use this perl to find them:
		/*
			for($z = 1; $z < 4096; $z++)
			{
				for($n = 0; $n <= 255; $n++)
				{
					for($m = 0; $m <= 255; $m++)
					{
						if($n != $m && (($n*4096 + $m*(4096-$z)) % (64*1024) == ($n*(4096-$z) + $m*4096) % (64*1024)))
						{
							print "$z: $n $m\n";
						}
					}
				}
			}
		*/
		blk[0] = 255;
		blk[1024] = 191;
		// Checksum to check
		RollingChecksum c1(blk, sizeof(blk));
		// Write
		f0.Write(blk, sizeof(blk));
		// Adjust block and write again
		uint8_t blk2[4096];
		memcpy(blk2, blk, sizeof(blk2));
		blk2[1024] = 255;
		blk2[0] = 191;
		TEST_THAT(::memcmp(blk2, blk, sizeof(blk)) != 0);
		RollingChecksum c2(blk2, sizeof(blk2));
		f0.Write(blk2, sizeof(blk2));
		// Check checksums
		TEST_THAT(c1.GetChecksum() == c2.GetChecksum());
		
		// Another 4k block
		write_test_data(f0, (4*1024), 99209);
		// Offset block
		make_random_data(blk, 2048, 1234199);
		f0.Write(blk, 2048);
		f0.Write(blk, 2048);
		f0.Write(blk, 2048);
		make_random_data(blk, 2048, 1343278);
		f0.Write(blk, 2048);
	
		write_test_data(f0, INITIAL_FILE_LENGTH - (16*1024) - ((4*1024)*2) - (4*1024) - (2048*4), 202);
	
	}
	
	// Then... create the varients
	for(int l = 0; testfiles[l] != 0; ++l)
	{
		char n1[256];
		char n2[256];
		sprintf(n1, "testfiles/f%d", l + 1);
		sprintf(n2, "testfiles/f%d", l);

		FileStream f1(n1, O_WRONLY | O_CREAT);
		gen_varient(f1, n2, testfiles[l]);
	}
}


