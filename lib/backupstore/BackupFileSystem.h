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

#include "autogen_BackupStoreException.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "HTTPResponse.h"
#include "NamedLock.h"
#include "S3Client.h"
#include "SimpleDBClient.h"
#include "Utils.h" // for ObjectExists_*

class BackupStoreDirectory;
class Configuration;
class IOStream;

class FileSystemCategory : public Log::Category
{
	public:
		FileSystemCategory(const std::string& name)
		: Log::Category(std::string("FileSystem/") + name)
		{ }
};


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
		virtual bool IsOldFileIndependent() = 0;
		virtual bool IsNewFileIndependent() = 0;
		virtual int64_t GetChangeInBlocksUsedByOldFile() { return 0; }
	};

	BackupFileSystem() { }
	virtual ~BackupFileSystem() { }
	static const int KEEP_TRYING_FOREVER = -1;
	virtual void GetLock(int max_tries = 8);
	virtual void ReleaseLock();
	virtual bool HaveLock() = 0;
	virtual int GetBlockSize() = 0;
	virtual BackupStoreInfo& GetBackupStoreInfo(bool ReadOnly, bool Refresh = false);
	virtual void PutBackupStoreInfo(BackupStoreInfo& rInfo) = 0;

	// DiscardBackupStoreInfo() discards the active BackupStoreInfo, invalidating any
	// references to it! It is needed to allow a BackupStoreContext to be Finished,
	// changes made to the BackupStoreInfo by BackupStoreCheck and HousekeepStoreAccount,
	// and the Context to be reopened.
	virtual void DiscardBackupStoreInfo(BackupStoreInfo& rInfo)
	{
		ASSERT(mapBackupStoreInfo.get() == &rInfo);
		DiscardBackupStoreInfo();
	}
	virtual void DiscardBackupStoreInfo()
	{
		mapBackupStoreInfo.reset();
	}
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfoUncached()
	{
		// Return a BackupStoreInfo freshly retrieved from storage, read-only to
		// prevent accidentally making changes to this copy, which can't be saved
		// back to the BackupFileSystem.
		return GetBackupStoreInfoInternal(true); // ReadOnly
	}

	// GetPotentialRefCountDatabase() returns the current potential database if it
	// has been already obtained and not closed, otherwise creates a new one.
	// This same database will never be returned by both this function and
	// GetPermanentRefCountDatabase() at the same time; it must be committed to
	// convert it to a permanent DB before GetPermanentRefCountDatabase() will
	// return it, and GetPotentialRefCountDatabase() no longer will after that.
	virtual BackupStoreRefCountDatabase& GetPotentialRefCountDatabase() = 0;
	// GetPermanentRefCountDatabase returns the current permanent database, if already
	// open, otherwise refreshes the cached copy, opens it, and returns it.
	virtual BackupStoreRefCountDatabase&
		GetPermanentRefCountDatabase(bool ReadOnly) = 0;
	// SaveRefCountDatabase() uploads a modified database to permanent storage
	// (if necessary). It must be called with the permanent database, and it must already
	// have been Close()d. Calling Commit() on the temporary database calls this function
	// automatically.
	virtual void SaveRefCountDatabase(BackupStoreRefCountDatabase& refcount_db) = 0;

	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0) = 0;
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut) = 0;
	virtual void PutDirectory(BackupStoreDirectory& rDir) = 0;
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData, BackupStoreRefCountDatabase::refcount_t refcount,
		bool allow_overwrite = false) = 0;
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData,
		BackupStoreRefCountDatabase::refcount_t refcount) = 0;
	// GetObject() will retrieve either a file or directory, whichever exists.
	// GetFile() and GetDirectory() are only guaranteed to work on objects of the
	// correct type, but may be faster (depending on the implementation).
	virtual std::auto_ptr<IOStream> GetObject(int64_t ObjectID, bool required = true) = 0;
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID) = 0;
	virtual std::auto_ptr<IOStream> GetFilePatch(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain);
	virtual std::auto_ptr<IOStream> GetBlockIndexReconstructed(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain);
	virtual void DeleteFile(int64_t ObjectID) = 0;
	virtual void DeleteDirectory(int64_t ObjectID) = 0;
	virtual void DeleteObjectUnknown(int64_t ObjectID, bool missing_ok = false) = 0;
	virtual bool CanMergePatchesEasily() = 0;
	virtual std::auto_ptr<BackupFileSystem::Transaction>
		CombineFile(int64_t OlderPatchID, int64_t NewerFileID) = 0;
	virtual std::auto_ptr<BackupFileSystem::Transaction>
		CombineDiffs(int64_t OlderPatchID, int64_t NewerPatchID) = 0;
	virtual std::string GetAccountIdentifier() = 0;
	// Use of GetAccountID() is not recommended. It returns S3_FAKE_ACCOUNT_ID on
	// S3BackupFileSystem.
	virtual int GetAccountID() = 0;
	virtual int64_t GetFileSizeInBlocks(int64_t ObjectID) = 0;

	class CheckObjectsResult
	{
	public:
		CheckObjectsResult()
		: maxObjectIDFound(0),
		  numErrorsFound(0)
		{ }

		int64_t maxObjectIDFound;
		int numErrorsFound;
	};

	virtual CheckObjectsResult CheckObjects(bool fix_errors) = 0;
	virtual void EnsureObjectIsPermanent(int64_t ObjectID, bool fix_errors) = 0;

	// CloseRefCountDatabase() closes the active database, saving changes to permanent
	// storage if necessary. It invalidates any references to the current database!
	// It is needed to allow a BackupStoreContext to be Finished, changes made to the
	// BackupFileSystem's BackupStoreInfo by BackupStoreCheck and HousekeepStoreAccount,
	// and the Context to be reopened.
	void CloseRefCountDatabase(BackupStoreRefCountDatabase* p_refcount_db)
	{
		ASSERT(p_refcount_db);
		ASSERT(p_refcount_db == mapPermanentRefCountDatabase.get());
		// Ensure that the database is closed before trying to save it:
		p_refcount_db->Close();
		SaveRefCountDatabase(*p_refcount_db);
		mapPermanentRefCountDatabase.reset();
	}
	BackupStoreRefCountDatabase* GetCurrentRefCountDatabase()
	{
		return mapPermanentRefCountDatabase.get();
	}

	static const FileSystemCategory LOCKING;

protected:
	virtual void TryGetLock() = 0;
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfoInternal(bool ReadOnly) = 0;
	std::auto_ptr<BackupStoreInfo> mapBackupStoreInfo;
	// You can have one temporary and one permanent refcound DB open at any time,
	// obtained with GetPotentialRefCountDatabase() and
	// GetPermanentRefCountDatabase() respectively:
	std::auto_ptr<BackupStoreRefCountDatabase> mapPotentialRefCountDatabase;
	std::auto_ptr<BackupStoreRefCountDatabase> mapPermanentRefCountDatabase;

	friend class BackupStoreRefCountDatabaseWrapper;
	// These should only be called by BackupStoreRefCountDatabaseWrapper::Commit():
	virtual void RefCountDatabaseBeforeCommit(BackupStoreRefCountDatabase& refcount_db);
	virtual void RefCountDatabaseAfterCommit(BackupStoreRefCountDatabase& refcount_db);
	// AfterDiscard() destroys the temporary database object and therefore invalidates
	// any held references to it!
	virtual void RefCountDatabaseAfterDiscard(BackupStoreRefCountDatabase& refcount_db);
	virtual std::auto_ptr<IOStream> GetTemporaryFileStream(int64_t id) = 0;
};

class RaidBackupFileSystem : public BackupFileSystem
{
private:
	const int64_t mAccountID;
	const std::string mAccountRootDir;
	const int mStoreDiscSet;
	std::string GetObjectFileName(int64_t ObjectID, bool EnsureDirectoryExists);
	NamedLock mWriteLock;

public:
	RaidBackupFileSystem(int64_t AccountID, const std::string &AccountRootDir, int discSet)
	: BackupFileSystem(),
	  mAccountID(AccountID),
	  mAccountRootDir(AccountRootDir),
	  mStoreDiscSet(discSet)
	{
		if(AccountRootDir.size() == 0)
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException, BadConfiguration,
				"Account root directory is empty");
		}

		ASSERT(AccountRootDir[AccountRootDir.size() - 1] == '/' ||
			AccountRootDir[AccountRootDir.size() - 1] == DIRECTORY_SEPARATOR_ASCHAR);
	}
	virtual ~RaidBackupFileSystem();
	virtual void ReleaseLock()
	{
		BackupFileSystem::ReleaseLock();
		if(HaveLock())
		{
			mWriteLock.ReleaseLock();
		}
	}
	virtual bool HaveLock()
	{
		return mWriteLock.GotLock();
	}
	virtual int GetBlockSize();
	virtual void PutBackupStoreInfo(BackupStoreInfo& rInfo);

	virtual BackupStoreRefCountDatabase& GetPotentialRefCountDatabase();
	virtual BackupStoreRefCountDatabase& GetPermanentRefCountDatabase(bool ReadOnly);
	virtual void SaveRefCountDatabase(BackupStoreRefCountDatabase& refcount_db);

	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0);
	virtual std::auto_ptr<IOStream> GetObject(int64_t ObjectID, bool required = true);
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut);
	virtual void PutDirectory(BackupStoreDirectory& rDir);
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData, BackupStoreRefCountDatabase::refcount_t refcount,
		bool allow_overwrite = false);
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData,
		BackupStoreRefCountDatabase::refcount_t refcount);
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID)
	{
		// For RaidBackupFileSystem, GetObject() is equivalent to GetFile().
		return GetObject(ObjectID);
	}
	virtual void DeleteFile(int64_t ObjectID)
	{
		// RaidFile doesn't care what type of object it is
		DeleteObjectUnknown(ObjectID);
	}
	virtual void DeleteDirectory(int64_t ObjectID)
	{
		// RaidFile doesn't care what type of object it is
		DeleteObjectUnknown(ObjectID);
	}
	virtual void DeleteObjectUnknown(int64_t ObjectID, bool missing_ok = false);
	virtual bool CanMergePatchesEasily() { return true; }
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineFile(int64_t OlderPatchID, int64_t NewerFileID);
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineDiffs(int64_t OlderPatchID, int64_t NewerPatchID);
	virtual std::string GetAccountIdentifier();
	virtual int GetAccountID() { return mAccountID; }
	virtual int64_t GetFileSizeInBlocks(int64_t ObjectID);
	virtual CheckObjectsResult CheckObjects(bool fix_errors);
	virtual void EnsureObjectIsPermanent(int64_t ObjectID, bool fix_errors);

protected:
	virtual void TryGetLock();
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfoInternal(bool ReadOnly);
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineFileOrDiff(int64_t OlderPatchID, int64_t NewerObjectID, bool NewerIsPatch);
	virtual std::auto_ptr<IOStream> GetTemporaryFileStream(int64_t id);

private:
	void CheckObjectsScanDir(int64_t StartID, int Level, const std::string &rDirName,
		CheckObjectsResult& Result, bool fix_errors);
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
	const Configuration& mrS3Config;
	std::string mBasePath, mCacheDirectory;
	NamedLock mCacheLock;
	S3Client& mrClient;
	std::auto_ptr<SimpleDBClient> mapSimpleDBClient;
	bool mHaveLock;
	std::string mSimpleDBDomain, mLockName, mLockValue, mCurrentUserName,
		mCurrentHostName;
	SimpleDBClient::str_map_t mLockAttributes;
	void ReportLockMismatches(str_map_diff_t mismatches);

	S3BackupFileSystem(const S3BackupFileSystem& forbidden); // no copying
	S3BackupFileSystem& operator=(const S3BackupFileSystem& forbidden); // no assignment
	std::string GetRefCountDatabaseCachePath()
	{
		return mCacheDirectory + DIRECTORY_SEPARATOR + S3_REFCOUNT_FILE_NAME;
	}
	void GetCacheLock();

public:
	S3BackupFileSystem(const Configuration& s3_config, S3Client& rClient);
	virtual ~S3BackupFileSystem();

	virtual void ReleaseLock();
	virtual bool HaveLock()
	{
		return mHaveLock;
	}
	virtual int GetBlockSize();
	virtual void PutBackupStoreInfo(BackupStoreInfo& rInfo);
	virtual BackupStoreRefCountDatabase& GetPotentialRefCountDatabase();
	virtual BackupStoreRefCountDatabase& GetPermanentRefCountDatabase(bool ReadOnly);
	virtual void SaveRefCountDatabase(BackupStoreRefCountDatabase& refcount_db);
	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0);
	virtual std::auto_ptr<IOStream> GetObject(int64_t ObjectID, bool required = true);
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut);
	virtual void PutDirectory(BackupStoreDirectory& rDir);
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData, BackupStoreRefCountDatabase::refcount_t refcount,
		bool allow_overwrite = false);
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData,
		BackupStoreRefCountDatabase::refcount_t refcount)
	{
		return PutFileComplete(ObjectID, rPatchData, refcount);
	}
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID);
	virtual void DeleteFile(int64_t ObjectID);
	virtual void DeleteDirectory(int64_t ObjectID);
	virtual void DeleteObjectUnknown(int64_t ObjectID, bool missing_ok = false);

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
		return box_strtoui64(since.c_str(), NULL, 10);
	}

	// And these are public to help with writing tests ONLY:
	friend class S3BackupAccountControl;

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
	int GetSizeInBlocks(int64_t bytes)
	{
		return (bytes + S3_NOTIONAL_BLOCK_SIZE - 1) / S3_NOTIONAL_BLOCK_SIZE;
	}
	virtual bool CanMergePatchesEasily() { return true; }
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineFile(int64_t OlderPatchID, int64_t NewerFileID);
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineDiffs(int64_t OlderPatchID, int64_t NewerPatchID);
	virtual std::string GetAccountIdentifier();
	virtual int GetAccountID() { return S3_FAKE_ACCOUNT_ID; }
	virtual int64_t GetFileSizeInBlocks(int64_t ObjectID);
	virtual CheckObjectsResult CheckObjects(bool fix_errors);
	virtual void EnsureObjectIsPermanent(int64_t ObjectID, bool fix_errors)
	{
		// Filesystem is not transactional, so nothing to do here.
	}

protected:
	virtual void TryGetLock();
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineFileOrDiff(int64_t OlderPatchID, int64_t NewerObjectID, bool NewerIsPatch);
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfoInternal(bool ReadOnly);
	virtual std::auto_ptr<IOStream> GetTemporaryFileStream(int64_t id);

private:
	// GetObjectURL() returns the complete URL for an object at the given
	// path, by adding the hostname, port and the object's URI (which can
	// be retrieved from GetMetadataURI or GetObjectURI).
	std::string GetObjectURL(const std::string& ObjectURI) const;

	// GetObjectURI() is a private interface which converts an object ID
	// and type into a URI, which starts with mBasePath:
	std::string GetObjectURI(int64_t ObjectID, int Type) const;

	int64_t GetRevisionID(const std::string& uri, HTTPResponse& response) const;

	void CheckObjectsScanDir(int64_t start_id, int level, const std::string &dir_name,
		CheckObjectsResult& result, bool fix_errors);
	void CheckObjectsDir(int64_t start_id,
		BackupFileSystem::CheckObjectsResult& result, bool fix_errors,
		const std::string& prefix, std::vector<std::string> files);
};

#endif // BACKUPFILESYSTEM__H
