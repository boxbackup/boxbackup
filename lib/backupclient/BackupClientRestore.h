// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientRestore.h
//		Purpose: Functions to restore files from a backup store
//		Created: 23/11/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTRESTORE_H
#define BACKUPCLIENTRESTORE_H

#include <string>

class BackupProtocolCallable;

enum
{
	Restore_Complete = 0,
	Restore_ResumePossible,
	Restore_TargetExists,
	Restore_TargetPathNotFound,
	Restore_UnknownError,
	Restore_CompleteWithErrors,
};

int BackupClientRestore(BackupProtocolCallable &rConnection,
	int64_t DirectoryID,
	const std::string& RemoteDirectoryName,
	const std::string& LocalDirectoryName,
	bool PrintDots,
	bool RestoreDeleted,
	bool UndeleteAfterRestoreDeleted,
	bool Resume,
	bool ContinueAfterErrors);

#endif // BACKUPCLIENTRESTORE_H

