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

#define BACKUP_STORE_DEFAULT_ACCOUNT_DATABASE_FILE	"/etc/box/backupstoreaccounts"

// 15 minutes to timeout (milliseconds)
#define	BACKUP_STORE_TIMEOUT			(15*60*1000)

// Should the store daemon convert files to Raid immediately?
#define	BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY	true

#endif // BACKUPCONSTANTS__H


