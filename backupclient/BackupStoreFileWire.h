// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFileWire.h
//		Purpose: On the wire / disc formats for backup store files
//		Created: 12/1/04
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREFILEWIRE__H
#define BACKUPSTOREFILEWIRE__H

#include "MD5Digest.h"

// set packing to one byte
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

typedef struct
{
	int32_t mMagicValue;	// also the version number
	int64_t mNumBlocks;		// number of blocks contained in the file
	int64_t mContainerID;
	int64_t mModificationTime;
	int32_t mMaxBlockClearSize;		// Maximum clear size that can be expected for a block
	int32_t mOptions;		// bitmask of options used
	// Then a BackupStoreFilename
	// Then a BackupClientFileAttributes
} file_StreamFormat;

typedef struct
{
	int32_t mMagicValue;	// different magic value
	int64_t mOtherFileID;	// the file ID of the 'other' file which may be referenced by the index
	uint64_t mEntryIVBase;	// base value for block IV
	int64_t mNumBlocks;		// repeat of value in file header
} file_BlockIndexHeader;

typedef struct
{
	int32_t mSize;			// size in clear
	uint32_t mWeakChecksum;	// weak, rolling checksum
	uint8_t mStrongChecksum[MD5Digest::DigestLength];	// strong digest based checksum
} file_BlockIndexEntryEnc;

typedef struct
{
	union
	{
		int64_t mEncodedSize;		// size encoded, if > 0
		int64_t mOtherBlockIndex;	// 0 - block number in other file, if <= 0
	};
	uint8_t mEnEnc[sizeof(file_BlockIndexEntryEnc)];	// Encoded section
} file_BlockIndexEntry;

// Use default packing
#ifdef STRUCTURE_PATCKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif

// header for blocks of compressed data in files
#define HEADER_CHUNK_IS_COMPRESSED		1	// bit
#define HEADER_ENCODING_SHIFT			1	// shift value
#define HEADER_BLOWFISH_ENCODING		1	// value stored in bits 1 -- 7
#define HEADER_AES_ENCODING				2	// value stored in bits 1 -- 7


#endif // BACKUPSTOREFILEWIRE__H

