// --------------------------------------------------------------------------
//
// File
//		Name:    RaidFileWrite.h
//		Purpose: Writing RAID like files
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------

#ifndef RAIDFILEWRITE__H
#define RAIDFILEWRITE__H

#include <string>

#include "IOStream.h"

class RaidFileDiscSet;

// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileWrite
//		Purpose: Writing RAID like files
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
class RaidFileWrite : public IOStream
{
public:
	RaidFileWrite(int SetNumber, const std::string &Filename);
	RaidFileWrite(int SetNumber, const std::string &Filename, int refcount);
	~RaidFileWrite();
private:
	RaidFileWrite(const RaidFileWrite &rToCopy);

public:
	// IOStream interface
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);	// will exception
	virtual void Write(const void *pBuffer, int NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual void Close();		// will discard the file! Use commit instead.
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	// Extra bits
	void Open(bool AllowOverwrite = false);
	void Commit(bool ConvertToRaidNow = false);
	void Discard();
	void TransformToRaidStorage();
	void Delete();
	pos_type GetFileSize();
	pos_type GetDiscUsageInBlocks();
	
	static void CreateDirectory(int SetNumber, const std::string &rDirName, bool Recursive = false, int mode = 0777);
	static void CreateDirectory(const RaidFileDiscSet &rSet, const std::string &rDirName, bool Recursive = false, int mode = 0777);
	
private:

private:
	int mSetNumber;
	std::string mFilename;
	int mOSFileHandle;
	int mRefCount;
};

#endif // RAIDFILEWRITE__H

