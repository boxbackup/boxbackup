// --------------------------------------------------------------------------
//
// File
//		Name:    testcompress.cpp
//		Purpose: Test lib/compress
//		Created: 5/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <string.h>

#include "Test.h"
#include "Compress.h"
#include "CompressStream.h"
#include "CollectInBufferStream.h"

#include "MemLeakFindOn.h"

#define DATA_SIZE			(1024*128+103)
#define CHUNK_SIZE			2561
#define DECOMP_CHUNK_SIZE	3

// Stream for testing
class CopyInToOutStream : public IOStream
{
public:
	CopyInToOutStream() : currentBuffer(0) {buffers[currentBuffer].SetForReading();}
	~CopyInToOutStream() {}
	int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite)
	{
		if(buffers[currentBuffer].StreamDataLeft())
		{
			return buffers[currentBuffer].Read(pBuffer, NBytes, Timeout);
		}
		
		// Swap buffers?
		if(buffers[(currentBuffer + 1) & 1].GetSize() > 0)
		{
			buffers[currentBuffer].Reset();
			currentBuffer = (currentBuffer + 1) & 1;
			buffers[currentBuffer].SetForReading();
			return buffers[currentBuffer].Read(pBuffer, NBytes, Timeout);		
		}

		return 0;
	}
	void Write(const void *pBuffer, int NBytes)
	{
		buffers[(currentBuffer + 1) & 1].Write(pBuffer, NBytes);
	}
	bool StreamDataLeft()
	{
		return buffers[currentBuffer].StreamDataLeft() || buffers[(currentBuffer + 1) % 1].GetSize() > 0;
	}
	bool StreamClosed()
	{
		return false;
	}
	int currentBuffer;
	CollectInBufferStream buffers[2];
};

// Test stream based interface
int test_stream()
{
	// Make a load of compressible data to compress
	CollectInBufferStream source;
	uint16_t data[1024];
	for(int x = 0; x < 1024; ++x)
	{
		data[x] = x;
	}
	for(int x = 0; x < (32*1024); ++x)
	{
		source.Write(data, (x % 1024) * 2);
	}
	source.SetForReading();

	// Straight compress from one stream to another
	{
		CollectInBufferStream *poutput = new CollectInBufferStream;
		CompressStream compress(poutput, true /* take ownership */, false /* read */, true /* write */);
		
		source.CopyStreamTo(compress);
		compress.Close();
		poutput->SetForReading();
		
		// Check sizes
		TEST_THAT(poutput->GetSize() < source.GetSize());
		TRACE2("compressed size = %d, source size = %d\n", poutput->GetSize(), source.GetSize());
		
		// Decompress the data
		{
			CollectInBufferStream decompressed;
			CompressStream decompress(poutput, false /* don't take ownership */, true /* read */, false /* write */);
			decompress.CopyStreamTo(decompressed);
			decompress.Close();
			
			TEST_THAT(decompressed.GetSize() == source.GetSize());
			TEST_THAT(::memcmp(decompressed.GetBuffer(), source.GetBuffer(), decompressed.GetSize()) == 0);
		}
		
		// Don't delete poutput, let mem leak testing ensure it's deleted.
	}

	// Set source to the beginning
	source.Seek(0, IOStream::SeekType_Absolute);

	// Test where the same stream compresses and decompresses, should be fun!
	{
		CollectInBufferStream output;
		CopyInToOutStream copyer;
		CompressStream compress(&copyer, false /* no ownership */, true, true);

		bool done = false;
		int count = 0;
		int written = 0;
		while(!done)
		{
			++count;
			bool do_sync = (count % 256) == 0;
			uint8_t buffer[4096];
			int r = source.Read(buffer, sizeof(buffer), IOStream::TimeOutInfinite);
			if(r == 0)
			{
				done = true;
				compress.Close();
			}
			else
			{
				compress.Write(buffer, r);
				written += r;
				if(do_sync)
				{
					compress.WriteAllBuffered();
				}
			}

			int r2 = 0;
			do
			{
				r2 = compress.Read(buffer, sizeof(buffer), IOStream::TimeOutInfinite);
				if(r2 > 0)
				{
					output.Write(buffer, r2);
				}
			} while(r2 > 0);
			if(do_sync && r != 0)
			{
				// Check that everything is synced
				TEST_THAT(output.GetSize() == written);
				TEST_THAT(::memcmp(output.GetBuffer(), source.GetBuffer(), output.GetSize()) == 0);
			}
		}
		output.SetForReading();
		
		// Test that it's the same
		TEST_THAT(output.GetSize() == source.GetSize());
		TEST_THAT(::memcmp(output.GetBuffer(), source.GetBuffer(), output.GetSize()) == 0);
	}

	return 0;
}

// Test basic interface
int test(int argc, const char *argv[])
{
	// Bad data to compress!
	char *data = (char *)malloc(DATA_SIZE);
	for(int l = 0; l < DATA_SIZE; ++l)
	{
		data[l] = l*23;
	}
	
	// parameters about compression	
	int maxOutput = Compress_MaxSizeForCompressedData(DATA_SIZE);
	TEST_THAT(maxOutput >= DATA_SIZE);

	char *compressed = (char *)malloc(maxOutput);
	int compressedSize = 0;
	
	// Do compression, in small chunks
	{
		Compress<true> compress;
		
		int in_loc = 0;
		while(!compress.OutputHasFinished())
		{
			int ins = DATA_SIZE - in_loc;
			if(ins > CHUNK_SIZE) ins = CHUNK_SIZE;
			
			if(ins == 0)
			{
				compress.FinishInput();
			}
			else
			{
				compress.Input(data + in_loc, ins);
			}
			in_loc += ins;
			
			// Get output data
			int s = 0;
			do
			{
				TEST_THAT(compressedSize < maxOutput);
				s = compress.Output(compressed + compressedSize, maxOutput - compressedSize);
				compressedSize += s;
			} while(s > 0);
		}
	}
	
	// a reasonable test, especially given the compressability of the input data.
	TEST_THAT(compressedSize < DATA_SIZE);

	// decompression
	char *decompressed = (char*)malloc(DATA_SIZE * 2);
	int decomp_size = 0;
	{	
		Compress<false> decompress;
		
		int in_loc = 0;
		while(!decompress.OutputHasFinished())
		{
			int ins = compressedSize - in_loc;
			if(ins > DECOMP_CHUNK_SIZE) ins = DECOMP_CHUNK_SIZE;
			
			if(ins == 0)
			{
				decompress.FinishInput();
			}
			else
			{
				decompress.Input(compressed + in_loc, ins);
			}
			in_loc += ins;
			
			// Get output data
			int s = 0;
			do
			{
				TEST_THAT(decomp_size <= DATA_SIZE);
				s = decompress.Output(decompressed + decomp_size, (DATA_SIZE*2) - decomp_size);
				decomp_size += s;
			} while(s > 0);
		}
	}
	
	TEST_THAT(decomp_size == DATA_SIZE);
	TEST_THAT(::memcmp(data, decompressed, DATA_SIZE) == 0);

	::free(data);
	::free(compressed);
	::free(decompressed);
	
	return test_stream();
}
