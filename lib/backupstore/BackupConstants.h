// --------------------------------------------------------------------------
//
// File
//		Name:    BackupConstants.h
//		Purpose: Constants for the backup server and client
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BACKUPCONSTANTS__H
#define BACKUPCONSTANTS__H

// 15 minutes to timeout (milliseconds)
#define BACKUP_STORE_TIMEOUT			(15*60*1000)

// Time to wait for retry after a backup error
#define BACKUP_ERROR_RETRY_SECONDS 100

// Should the store daemon convert files to Raid immediately?
#define BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY	true

#define WRITE_LOCK_FILENAME "write.lock"

#endif // BACKUPCONSTANTS__H

