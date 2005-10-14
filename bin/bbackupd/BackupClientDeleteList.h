// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientDeleteList.h
//		Purpose: List of pending deletes for backup
//		Created: 10/11/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTDELETELIST__H
#define BACKUPCLIENTDELETELIST__H

#include "BackupStoreFilename.h"

class BackupClientContext;

#include <vector>
#include <utility>
#include <set>

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupClientDeleteList
//		Purpose: List of pending deletes for backup
//		Created: 10/11/03
//
// --------------------------------------------------------------------------
class BackupClientDeleteList
{
public:
	BackupClientDeleteList();
	~BackupClientDeleteList();
	
	void AddDirectoryDelete(int64_t ObjectID);
	void AddFileDelete(int64_t DirectoryID, const BackupStoreFilename &rFilename);

	void StopDirectoryDeletion(int64_t ObjectID);
	void StopFileDeletion(int64_t DirectoryID, const BackupStoreFilename &rFilename);
	
	void PerformDeletions(BackupClientContext &rContext);
	
private:
	std::vector<int64_t> mDirectoryList;
	std::set<int64_t> mDirectoryNoDeleteList;	// note: things only get in this list if they're not present in mDirectoryList when they are 'added'
	std::vector<std::pair<int64_t, BackupStoreFilename> > mFileList;
	std::vector<std::pair<int64_t, BackupStoreFilename> > mFileNoDeleteList;
};

#endif // BACKUPCLIENTDELETELIST__H

