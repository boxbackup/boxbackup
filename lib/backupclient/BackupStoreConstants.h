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
#define BACKUP_FILE_DIFF_MAX_BLOCK_SIZES		64

// When doing rsync scans, do not scan for blocks smaller than
#define BACKUP_FILE_DIFF_MIN_BLOCK_SIZE			128

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

