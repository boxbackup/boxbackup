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
#include "BoxTime.h"

class BackupProtocolCallable;

enum
{
	Restore_Undefined = -1,
	Restore_Complete = 0,
	Restore_ResumePossible,
	Restore_TargetExists,
	Restore_TargetPathNotFound,
	Restore_UnknownError,
	Restore_CompleteWithErrors,
};

// TODO : add statistics to the restore and print to the log
class RestoreInfos  {

	public:
		RestoreInfos() :
			totalWarnings(0),
			totalFilesRestored(0),
			totalBytesRestored(0),
			totalFilesSkipped(0),
			totalBytesSkipped(0),
			totalFilesFailed(0),
			totalBytesFailed(0),
			totalDirsRestored(0),
			endTime(0) {
				startTime=GetCurrentBoxTime();
			};

 	box_time_t startTime;
    box_time_t endTime;
	int64_t totalWarnings;
	int64_t totalFilesRestored;
	int64_t totalBytesRestored;
	int64_t totalFilesSkipped;
	int64_t totalBytesSkipped;
	int64_t totalFilesFailed;
	int64_t totalBytesFailed;
	int64_t totalDirsRestored;

};

int BackupClientRestore(BackupProtocolCallable &rConnection,
	int64_t DirectoryID,
	const std::string& RemoteDirectoryName,
	const std::string& LocalDirectoryName,
	bool PrintDots,
	bool RestoreDeleted,
	bool UndeleteAfterRestoreDeleted,
	bool Resume,
	bool ContinueAfterErrors,
	RestoreInfos &infos);

#endif // BACKUPCLIENTRESTORE_H

