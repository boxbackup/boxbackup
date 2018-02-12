// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientInodeToIDMap.h
//		Purpose: Map of inode numbers to file IDs on the store
//		Created: 11/11/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTINODETOIDMAP_H
#define BACKUPCLIENTINODETOIDMAP_H

#include <sys/types.h>

#include <map>
#include <string>
#include <utility>

// avoid having to include the DB files when not necessary
#ifndef BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION
	class DEPOT;
#endif

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupClientInodeToIDMap
//		Purpose: Map of inode numbers to file IDs on the store
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
class BackupClientInodeToIDMap
{
public:
	BackupClientInodeToIDMap();
	~BackupClientInodeToIDMap();
private:
	BackupClientInodeToIDMap(const BackupClientInodeToIDMap &rToCopy);	// not allowed
public:

	void Open(const char *Filename, bool ReadOnly, bool CreateNew);
	void OpenEmpty();

	void AddToMap(InodeRefType InodeRef, int64_t ObjectID,
		int64_t InDirectory, const std::string& LocalPath);
	bool Lookup(InodeRefType InodeRef, int64_t &rObjectIDOut,
		int64_t &rInDirectoryOut, std::string* pLocalPathOut = NULL) const;

	void Close();

private:
	bool mReadOnly;
	bool mEmpty;
	std::string mFilename;
	DEPOT *mpDepot;
};

#endif // BACKUPCLIENTINODETOIDMAP_H


