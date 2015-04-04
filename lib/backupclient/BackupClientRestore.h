// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientRestore.h
//		Purpose: Functions to restore files from a backup store
//		Created: 23/11/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPSCLIENTRESTORE_H
#define BACKUPSCLIENTRESTORE__H

class BackupProtocolClient;

enum
{
	Restore_Complete = 0,
	Restore_ResumePossible,
	Restore_TargetExists,
	Restore_TargetPathNotFound,
	Restore_UnknownError,
	Restore_CompleteWithErrors,
};

int BackupClientRestore(BackupProtocolClient &rConnection,
	int64_t DirectoryID,
	const char *LocalDirectoryName,
	bool PrintDots = false,
	bool RestoreDeleted = false,
	bool UndeleteAfterRestoreDeleted = false,
	bool Resume = false,
	bool ContinueAfterErrors = false);

#endif // BACKUPSCLIENTRESTORE__H

