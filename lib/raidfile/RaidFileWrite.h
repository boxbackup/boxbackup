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
	// TODO FIXME we should remove this constructor, and ensure that
	// anyone who writes to a RaidFile knows what the reference count
	// is before doing so. That requires supporting regenerating the
	// reference count database in BackupStoreCheck, and using a real
	// database instead of an in-memory array in HousekeepStoreAccount,
	// and supporting multiple databases at a time (old and new) in
	// BackupStoreRefCountDatabase, and I don't have time to make those
	// changes right now. We may even absolutely need to have a full
	// reference database, not just reference counts, to implement
	// snapshots.
	RaidFileWrite(int SetNumber, const std::string &Filename);

	RaidFileWrite(int SetNumber, const std::string &Filename, int refcount);
	~RaidFileWrite();

private:
	RaidFileWrite(const RaidFileWrite &rToCopy);

public:
	// IOStream interface
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);	// will exception
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite);
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
	int mSetNumber;
	std::string mFilename, mTempFilename;
	int mOSFileHandle;
	int mRefCount;
	bool mAllowOverwrite;
};

#endif // RAIDFILEWRITE__H

