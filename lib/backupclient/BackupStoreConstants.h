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
//		Name:    BackupStoreContants.h
//		Purpose: constants for the backup system
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTORECONSTANTS__H
#define BACKUPSTORECONSTANTS__H

#define BACKUPSTORE_ROOT_DIRECTORY_ID	1

#define BACKUP_STORE_SERVER_VERSION		1

// Minimum size for a chunk to be compressed
#define BACKUP_FILE_MIN_COMPRESSED_CHUNK_SIZE	256

// min and max sizes for blocks
#define BACKUP_FILE_MIN_BLOCK_SIZE				4096
#define BACKUP_FILE_MAX_BLOCK_SIZE				(512*1024)

// Increase the block size if there are more than this number of blocks
#define BACKUP_FILE_INCREASE_BLOCK_SIZE_AFTER 	4096

// Avoid creating blocks smaller than this
#define	BACKUP_FILE_AVOID_BLOCKS_LESS_THAN		128

// Maximum number of sizes to do an rsync-like scan for
#define BACKUP_FILE_DIFF_MAX_BLOCK_SIZES		8

// When doing rsync scans, do not scan for blocks smaller than
#define BACKUP_FILE_DIFF_MIN_BLOCK_SIZE			256

// A limit to stop diffing running out of control: If more than this
// times the number of blocks in the original index are found, stop
// looking. This stops really bad cases of diffing files containing
// all the same byte using huge amounts of memory and processor time.
// This is a multiple of the number of blocks in the diff from file.
#define BACKUP_FILE_DIFF_MAX_BLOCK_FIND_MULTIPLE	4096

// How many seconds to wait before deleting unused root directory entries?
#ifndef NDEBUG
	// Debug: 30 seconds (easier to test)
	#define BACKUP_DELETE_UNUSED_ROOT_ENTRIES_AFTER		30
#else
	// Release: 2 days (plenty of time for sysadmins to notice, or change their mind)
	#define BACKUP_DELETE_UNUSED_ROOT_ENTRIES_AFTER		172800
#endif

#endif // BACKUPSTORECONSTANTS__H

