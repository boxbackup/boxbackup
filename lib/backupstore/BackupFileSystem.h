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
#include "Utils.h"

class BackupStoreDirectory;
class BackupStoreInfo;
class BackupStoreRefCountDatabase;
class Configuration;
class IOStream;

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
	virtual bool TryGetLock() = 0;
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
	virtual bool TryGetLock();
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

#define S3_INFO_FILE_NAME "boxbackup.info"
#define S3_REFCOUNT_FILE_NAME "boxbackup.refcount.db"
#define S3_FAKE_ACCOUNT_ID 0x53336964 // 'S3id'
#define S3_CACHE_LOCK_NAME "boxbackup.cache.lock"

#ifdef BOX_RELEASE_BUILD
	// Use a larger block size for efficiency
	#define S3_NOTIONAL_BLOCK_SIZE 1048576
#else
	// Use a smaller block size to make tests run faster
	#define S3_NOTIONAL_BLOCK_SIZE 16384
#endif

class S3BackupFileSystem : public BackupFileSystem
{
private:
	const Configuration& mrConfig;
	std::string mBasePath;
	S3Client& mrClient;
	int64_t GetRevisionID(const std::string& uri, HTTPResponse& response) const;
	int GetSizeInBlocks(int64_t bytes)
	{
		return (bytes + S3_NOTIONAL_BLOCK_SIZE - 1) / S3_NOTIONAL_BLOCK_SIZE;
	}

public:
	S3BackupFileSystem(const Configuration& config, const std::string& BasePath,
		S3Client& rClient)
	: mrConfig(config),
	  mBasePath(BasePath),
	  mrClient(rClient)
	{ }
	virtual bool TryGetLock() { return false; }
	virtual void ReleaseLock() { }
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

	// And these are public to help with writing tests ONLY:
	// The returned URI should start with mBasePath.
	std::string GetMetadataURI(const std::string& MetadataFilename) const
	{
		return mBasePath + MetadataFilename;
	}
	// The returned URI should start with mBasePath.
	std::string GetDirectoryURI(int64_t ObjectID)
	{
		return GetObjectURI(ObjectID, ObjectExists_Dir);
	}
	// The returned URI should start with mBasePath.
	std::string GetFileURI(int64_t ObjectID)
	{
		return GetObjectURI(ObjectID, ObjectExists_File);
	}

private:
	// S3BackupAccountControl wants to call some of these private APIs, but nobody else should:
	friend class S3BackupAccountControl;

	// GetObjectURL() returns the complete URL for an object at the given
	// path, by adding the hostname, port and the object's URI (which can
	// be retrieved from GetMetadataURI or GetObjectURI).
	std::string GetObjectURL(const std::string& ObjectURI) const;

	// GetObjectURI() is a private interface which converts an object ID
	// and type into a URI, which starts with mBasePath:
	std::string GetObjectURI(int64_t ObjectID, int Type) const;

	// GetObject() retrieves the object with the specified URI from the
	// configured S3 server. S3Client has no idea about path prefixes
	// (mBasePath), so it must already be added: the supplied ObjectURI
	// must start with it.
	HTTPResponse GetObject(const std::string& ObjectURI)
	{
		return mrClient.GetObject(ObjectURI);
	}

	// HeadObject() retrieves the headers (metadata) for the object with
	// the specified URI from the configured S3 server. S3Client has no
	// idea about path prefixes (mBasePath), so it must already be added:
	// the supplied ObjectURI must start with it.
	HTTPResponse HeadObject(const std::string& ObjectURI)
	{
		return mrClient.HeadObject(ObjectURI);
	}

	// PutObject() uploads the supplied stream to the configured S3 server,
	// saving it with the supplied URI. S3Client has no idea about path
	// prefixes (mBasePath), so it must already be added: the supplied
	// ObjectURI must start with it.
	HTTPResponse PutObject(const std::string& ObjectURI,
		IOStream& rStreamToSend, const char* pContentType = NULL)
	{
		return mrClient.PutObject(ObjectURI, rStreamToSend, pContentType);
	}
};

#endif // BACKUPFILESYSTEM__H
