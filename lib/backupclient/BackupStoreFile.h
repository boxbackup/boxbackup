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
//		Name:    BackupStoreFile.h
//		Purpose: Utils for manipulating files
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREFILE__H
#define BACKUPSTOREFILE__H

#include "IOStream.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreFilename.h"

#include <memory>

typedef struct 
{
	int64_t mBytesInEncodedFiles;
	int64_t mBytesAlreadyOnServer;
	int64_t mTotalFileStreamSize;
} BackupStoreFileStats;



// Output buffer to EncodeChunk and input data to DecodeChunk must
// have specific alignment, see function comments.
#define BACKUPSTOREFILE_CODING_BLOCKSIZE		16
#define BACKUPSTOREFILE_CODING_OFFSET			15

// Have some memory allocation commands, note closing "Off" at end of file.
#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreFile
//		Purpose: Class to hold together utils for maniplating files.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
class BackupStoreFile
{
public:
	class DecodedStream : public IOStream
	{
		friend class BackupStoreFile;
	private:
		DecodedStream(IOStream &rEncodedFile, int Timeout);
		DecodedStream(const DecodedStream &); // not allowed
		DecodedStream &operator=(const DecodedStream &); // not allowed
	public:
		~DecodedStream();

		// Stream functions		
		virtual int Read(void *pBuffer, int NBytes, int Timeout);
		virtual void Write(const void *pBuffer, int NBytes);
		virtual bool StreamDataLeft();
		virtual bool StreamClosed();
		
		// Accessor functions
		const BackupClientFileAttributes &GetAttributes() {return mAttributes;}
		const BackupStoreFilename &GetFilename() {return mFilename;}
		int64_t GetNumBlocks() {return mNumBlocks;}	// primarily for tests
		
		bool IsSymLink();
		
	private:
		void Setup(const BackupClientFileAttributes *pAlterativeAttr);
		void ReadBlockIndex(bool MagicAlreadyRead);
			
	private:
		IOStream &mrEncodedFile;
		int mTimeout;
		BackupClientFileAttributes mAttributes;
		BackupStoreFilename mFilename;
		int64_t mNumBlocks;
		void *mpBlockIndex;
		uint8_t *mpEncodedData;
		uint8_t *mpClearData;
		int mClearDataSize;
		int mCurrentBlock;
		int mCurrentBlockClearSize;
		int mPositionInCurrentBlock;
		uint64_t mEntryIVBase;
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		bool mIsOldVersion;
#endif
	};


	// Main interface
	static std::auto_ptr<IOStream> EncodeFile(const char *Filename, int64_t ContainerID, const BackupStoreFilename &rStoreFilename, int64_t *pModificationTime = 0);
	static std::auto_ptr<IOStream> EncodeFileDiff(const char *Filename, int64_t ContainerID,
		const BackupStoreFilename &rStoreFilename, int64_t DiffFromObjectID, IOStream &rDiffFromBlockIndex,
		int Timeout, int64_t *pModificationTime = 0, bool *pIsCompletelyDifferent = 0);
	static bool VerifyEncodedFileFormat(IOStream &rFile, int64_t *pDiffFromObjectIDOut = 0, int64_t *pContainerIDOut = 0);
	static void CombineFile(IOStream &rDiff, IOStream &rDiff2, IOStream &rFrom, IOStream &rOut);
	static void CombineDiffs(IOStream &rDiff1, IOStream &rDiff2, IOStream &rDiff2b, IOStream &rOut);
	static void ReverseDiffFile(IOStream &rDiff, IOStream &rFrom, IOStream &rFrom2, IOStream &rOut, int64_t ObjectIDOfFrom, bool *pIsCompletelyDifferent = 0);
	static void DecodeFile(IOStream &rEncodedFile, const char *DecodedFilename, int Timeout, const BackupClientFileAttributes *pAlterativeAttr = 0);
	static std::auto_ptr<BackupStoreFile::DecodedStream> DecodeFileStream(IOStream &rEncodedFile, int Timeout, const BackupClientFileAttributes *pAlterativeAttr = 0);
	static bool CompareFileContentsAgainstBlockIndex(const char *Filename, IOStream &rBlockIndex, int Timeout);
	static std::auto_ptr<IOStream> CombineFileIndices(IOStream &rDiff, IOStream &rFrom, bool DiffIsIndexOnly = false, bool FromIsIndexOnly = false);

	// Stream manipulation
	static std::auto_ptr<IOStream> ReorderFileToStreamOrder(IOStream *pStream, bool TakeOwnership);
	static void MoveStreamPositionToBlockIndex(IOStream &rStream);

	// Crypto setup
	static void SetBlowfishKeys(const void *pKey, int KeyLength, const void *pBlockEntryKey, int BlockEntryKeyLength);
#ifndef PLATFORM_OLD_OPENSSL
	static void SetAESKey(const void *pKey, int KeyLength);
#endif

	// Allocation of properly aligning chunks for decoding and encoding chunks
	inline static void *CodingChunkAlloc(int Size)
	{
		uint8_t *a = (uint8_t*)malloc((Size) + (BACKUPSTOREFILE_CODING_BLOCKSIZE * 3));
		if(a == 0) return 0;
		// Align to main block size
		ASSERT(sizeof(uint32_t) == sizeof(void*));	// make sure casting the right pointer size, will need to fix on platforms with 64 bit pointers
		uint32_t adjustment = BACKUPSTOREFILE_CODING_BLOCKSIZE - (((uint32_t)a) % BACKUPSTOREFILE_CODING_BLOCKSIZE);
		uint8_t *b = (a + adjustment);
		// Store adjustment
		*b = (uint8_t)adjustment;
		// Return offset
		return b + BACKUPSTOREFILE_CODING_OFFSET;
	}
	inline static void CodingChunkFree(void *Block)
	{
		// Check alignment is as expected
		ASSERT(sizeof(uint32_t) == sizeof(void*));	// make sure casting the right pointer size, will need to fix on platforms with 64 bit pointers
		ASSERT((((uint32_t)Block) % BACKUPSTOREFILE_CODING_BLOCKSIZE) == BACKUPSTOREFILE_CODING_OFFSET);
		uint8_t *a = (uint8_t*)Block;
		a -= BACKUPSTOREFILE_CODING_OFFSET;
		// Adjust downwards...
		a -= *a;
		free(a);
	}

	// Limits
	static void SetMaximumDiffingTime(int Seconds);

	// Building blocks
	class EncodingBuffer
	{
	public:
		EncodingBuffer();
		~EncodingBuffer();
	private:
		// No copying
		EncodingBuffer(const EncodingBuffer &);
		EncodingBuffer &operator=(const EncodingBuffer &);
	public:
		void Allocate(int Size);
		void Reallocate(int NewSize);
		
		uint8_t *mpBuffer;
		int mBufferSize;
	};
	static int MaxBlockSizeForChunkSize(int ChunkSize);
	static int EncodeChunk(const void *Chunk, int ChunkSize, BackupStoreFile::EncodingBuffer &rOutput);

	// Caller should know how big the output size is, but also allocate a bit more memory to cover various
	// overheads allowed for in checks
	static inline int OutputBufferSizeForKnownOutputSize(int KnownChunkSize)
	{
		// Plenty big enough
		return KnownChunkSize + 256;
	}
	static int DecodeChunk(const void *Encoded, int EncodedSize, void *Output, int OutputSize);

	// Statisitics, not designed to be completely reliable	
	static void ResetStats();
	static BackupStoreFileStats msStats;
	
	// For debug
#ifndef NDEBUG
	static bool TraceDetailsOfDiffProcess;
#endif

	// For decoding encoded files
	static void DumpFile(void *clibFileHandle, bool ToTrace, IOStream &rFile);
};

#include "MemLeakFindOff.h"

#endif // BACKUPSTOREFILE__H

