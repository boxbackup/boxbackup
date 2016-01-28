// --------------------------------------------------------------------------
//
// File
//		Name:    BackupFileSystem.h
//		Purpose: Generic interface for reading and writing files and
//			 directories, abstracting over RaidFile, S3, FTP etc.
//		Created: 2015/08/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPFILESYSTEM__H
#define BACKUPFILESYSTEM__H

#include <string>

#include "HTTPResponse.h"
#include "NamedLock.h"
#include "S3Client.h"
#include "SimpleDBClient.h"

class BackupStoreDirectory;
class BackupStoreInfo;
class BackupStoreRefCountDatabase;
class Configuration;
class IOStream;

#define S3_NOTIONAL_BLOCK_SIZE 1048576

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupFileSystem
//		Purpose: Generic interface for reading and writing files and
//			 directories, abstracting over RaidFile, S3, FTP etc.
//		Created: 2015/08/03
//
// --------------------------------------------------------------------------
class BackupFileSystem
{
public:
	class Transaction
	{
	public:
		virtual ~Transaction() { }
		virtual void Commit() = 0;
		virtual int64_t GetNumBlocks() = 0;
		virtual bool IsNewFileIndependent() = 0;
		virtual int64_t GetChangeInBlocksUsedByOldFile() { return 0; }
	};

	BackupFileSystem() { }
	virtual ~BackupFileSystem() { }
	virtual void TryGetLock() = 0;
	virtual void GetLock();
	virtual void ReleaseLock() = 0;
	virtual int GetBlockSize() = 0;
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfo(int32_t AccountID,
		bool ReadOnly) = 0;
	virtual void PutBackupStoreInfo(BackupStoreInfo& rInfo) = 0;
	virtual std::auto_ptr<BackupStoreRefCountDatabase> GetRefCountDatabase(int32_t AccountID,
		bool ReadOnly) = 0;
	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0) = 0;
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut) = 0;
	virtual void PutDirectory(BackupStoreDirectory& rDir) = 0;
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData) = 0;
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData) = 0;
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID) = 0;
	virtual std::auto_ptr<IOStream> GetFilePatch(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain) = 0;
	virtual void DeleteFile(int64_t ObjectID) = 0;
	virtual void DeleteDirectory(int64_t ObjectID) = 0;
};

class RaidBackupFileSystem : public BackupFileSystem
{
private:
	const std::string mAccountRootDir;
	const int mStoreDiscSet;
	std::string GetObjectFileName(int64_t ObjectID, bool EnsureDirectoryExists);
	NamedLock mWriteLock;

public:
	RaidBackupFileSystem(const std::string &AccountRootDir, int discSet)
	: BackupFileSystem(),
	  mAccountRootDir(AccountRootDir),
	  mStoreDiscSet(discSet)
	{ }
	~RaidBackupFileSystem()
	{
		if(mWriteLock.GotLock())
		{
			ReleaseLock();
		}
	}
	virtual void TryGetLock();
	virtual void ReleaseLock()
	{
		mWriteLock.ReleaseLock();
	}
	virtual int GetBlockSize();
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfo(int32_t AccountID,
		bool ReadOnly);
	virtual void PutBackupStoreInfo(BackupStoreInfo& rInfo);
	virtual std::auto_ptr<BackupStoreRefCountDatabase> GetRefCountDatabase(int32_t AccountID,
		bool ReadOnly);
	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0);
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut);
	virtual void PutDirectory(BackupStoreDirectory& rDir);
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData);
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData);
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID);
	virtual std::auto_ptr<IOStream> GetFilePatch(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain);
	virtual void DeleteFile(int64_t ObjectID);
	virtual void DeleteDirectory(int64_t ObjectID)
	{
		DeleteFile(ObjectID);
	}
};

class S3BackupFileSystem : public BackupFileSystem
{
private:
	const Configuration& mrConfig;
	std::string mBasePath;
	S3Client& mrClient;
	std::auto_ptr<SimpleDBClient> mapSimpleDBClient;
	int64_t GetRevisionID(const std::string& uri, HTTPResponse& response) const;
	bool mHaveLock;
	std::string mSimpleDBDomain, mLockName, mLockValue, mCurrentUserName,
		mCurrentHostName;
	SimpleDBClient::str_map_t mLockAttributes;
	void ReportLockMismatches(str_map_diff_t mismatches);

	S3BackupFileSystem(const S3BackupFileSystem& forbidden); // no copying
	S3BackupFileSystem& operator=(const S3BackupFileSystem& forbidden); // no assignment

public:
	S3BackupFileSystem(const Configuration& config, const std::string& BasePath,
		S3Client& rClient);
	~S3BackupFileSystem();
	virtual void TryGetLock();
	virtual void ReleaseLock();
	virtual int GetBlockSize();
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfo(int32_t AccountID,
		bool ReadOnly);
	virtual void PutBackupStoreInfo(BackupStoreInfo& rInfo);
	virtual std::auto_ptr<BackupStoreRefCountDatabase> GetRefCountDatabase(int32_t AccountID,
		bool ReadOnly)
	{
		return std::auto_ptr<BackupStoreRefCountDatabase>();
	}
	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0);
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut);
	virtual void PutDirectory(BackupStoreDirectory& rDir);
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData)
	{
		return std::auto_ptr<Transaction>();
	}
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData)
	{
		return std::auto_ptr<Transaction>();
	}
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID)
	{
		return std::auto_ptr<IOStream>();
	}
	virtual std::auto_ptr<IOStream> GetFilePatch(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void DeleteFile(int64_t ObjectID)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void DeleteDirectory(int64_t ObjectID)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	std::string GetObjectURI(const std::string& ObjectPath) const
	{
		return mBasePath + ObjectPath;
	}
	std::string GetObjectURL(const std::string& ObjectPath) const;
	HTTPResponse GetObject(const std::string& ObjectPath)
	{
		return mrClient.GetObject(GetObjectURI(ObjectPath));
	}
	HTTPResponse HeadObject(const std::string& ObjectPath)
	{
		return mrClient.HeadObject(GetObjectURI(ObjectPath));
	}
	HTTPResponse PutObject(const std::string& ObjectPath,
		IOStream& rStreamToSend, const char* pContentType = NULL)
	{
		return mrClient.PutObject(GetObjectURI(ObjectPath), rStreamToSend,
			pContentType);
	}

	// These should not really be APIs, but they are public to make them testable:
	const std::string& GetSimpleDBDomain() const { return mSimpleDBDomain; }
	const std::string& GetSimpleDBLockName() const { return mLockName; }
	const std::string& GetSimpleDBLockValue() const { return mLockValue; }
	const std::string& GetCurrentUserName() const { return mCurrentUserName; }
	const std::string& GetCurrentHostName() const { return mCurrentHostName; }
	const box_time_t GetSinceTime() const
	{
		// Unfortunately operator[] is not const, so use a const_iterator to
		// get the value that we want.
		const std::string& since(mLockAttributes.find("since")->second);
		return strtoull(since.c_str(), NULL, 10);
	}

	// And these are public to help with writing tests ONLY:
	std::string GetDirectoryURI(int64_t ObjectID);
	std::string GetFileURI(int64_t ObjectID);
	int GetSizeInBlocks(int64_t bytes)
	{
		return (bytes + S3_NOTIONAL_BLOCK_SIZE - 1) / S3_NOTIONAL_BLOCK_SIZE;
	}
};

#define S3_INFO_FILE_NAME "boxbackup.info"

#endif // BACKUPFILESYSTEM__H
