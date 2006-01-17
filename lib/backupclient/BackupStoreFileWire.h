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

