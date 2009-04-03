// --------------------------------------------------------------------------
//
// File
//		Name:    RaidFileRead.h
//		Purpose: Read Raid like Files
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------

#ifndef RAIDFILEREAD__H
#define RAIDFILEREAD__H

#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>

#include "IOStream.h"

class RaidFileDiscSet;


// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileRead
//		Purpose: Read RAID like files
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
class RaidFileRead : public IOStream
{
protected:
	RaidFileRead(int SetNumber, const std::string &Filename);
public:
	virtual ~RaidFileRead();
private:
	RaidFileRead(const RaidFileRead &rToCopy);
	
public:
	// Open a raid file
	static std::auto_ptr<RaidFileRead> Open(int SetNumber, const std::string &Filename, int64_t *pRevisionID = 0, int BufferSizeHint = 4096);

	// Extra info
	virtual pos_type GetFileSize() const = 0;

	// Utility functions
	static bool FileExists(int SetNumber, const std::string &rFilename, int64_t *pRevisionID = 0);
	static bool DirectoryExists(const RaidFileDiscSet &rSet, const std::string &rDirName);
	static bool DirectoryExists(int SetNumber, const std::string &rDirName);
	enum
	{
		DirReadType_FilesOnly = 0,
		DirReadType_DirsOnly = 1
	};
	static bool ReadDirectoryContents(int SetNumber, const std::string &rDirName, int DirReadType, std::vector<std::string> &rOutput);

	// Common IOStream interface implementation
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamClosed();
	virtual pos_type BytesLeftToRead();

	pos_type GetDiscUsageInBlocks();

	typedef int64_t FileSizeType;

protected:
	int mSetNumber;
	std::string mFilename;
};

#endif // RAIDFILEREAD__H

