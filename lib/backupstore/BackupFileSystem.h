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
#include "BackupStoreRefCountDatabase.h"
#include "HTTPResponse.h"
#include "NamedLock.h"
#include "S3Client.h"
#include "SimpleDBClient.h"
#include "Utils.h" // for ObjectExists_*

class BackupStoreDirectory;
class BackupStoreInfo;
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
	virtual void TryGetLock() = 0;
	virtual void GetLock();
	virtual void ReleaseLock()
	{
		mapBackupStoreInfo.reset();
		mapPotentialRefCountDatabase.reset();
		mapPermanentRefCountDatabase.reset();
	}

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
	// (if necessary). It must be called with the permanent database. Calling Commit()
	// on the temporary database calls this function automatically.
	virtual void SaveRefCountDatabase(BackupStoreRefCountDatabase& refcount_db) = 0;

	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0) = 0;
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut) = 0;
	virtual void PutDirectory(BackupStoreDirectory& rDir) = 0;
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData, BackupStoreRefCountDatabase::refcount_t refcount) = 0;
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData,
		BackupStoreRefCountDatabase::refcount_t refcount) = 0;
	// GetObject() will retrieve either a file or directory, whichever exists.
	// GetFile() and GetDirectory() are only guaranteed to work on objects of the
	// correct type, but may be faster (depending on the implementation).
	virtual std::auto_ptr<IOStream> GetObject(int64_t ObjectID, bool required = true) = 0;
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID) = 0;
	virtual std::auto_ptr<IOStream> GetFilePatch(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain) = 0;
	virtual void DeleteFile(int64_t ObjectID) = 0;
	virtual void DeleteDirectory(int64_t ObjectID) = 0;
	virtual void DeleteObjectUnknown(int64_t ObjectID) = 0;
	virtual bool CanMergePatches() = 0;
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
		ASSERT(p_refcount_db == mapPermanentRefCountDatabase.get());
		SaveRefCountDatabase(*p_refcount_db);
		mapPermanentRefCountDatabase.reset();
	}
	BackupStoreRefCountDatabase* GetCurrentRefCountDatabase()
	{
		return mapPermanentRefCountDatabase.get();
	}

protected:
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfoInternal(bool ReadOnly) = 0;
	std::auto_ptr<BackupStoreInfo> mapBackupStoreInfo;
	// You can have one temporary and one permanent refcound DB open at any time,
	// obtained with GetPotentialRefCountDatabase() and
	// GetPermanentRefCountDatabase() respectively:
	std::auto_ptr<BackupStoreRefCountDatabase> mapPotentialRefCountDatabase;
	std::auto_ptr<BackupStoreRefCountDatabase> mapPermanentRefCountDatabase;

protected:
	friend class BackupStoreRefCountDatabaseWrapper;
	// These should only be called by BackupStoreRefCountDatabaseWrapper::Commit():
	virtual void RefCountDatabaseBeforeCommit(BackupStoreRefCountDatabase& refcount_db);
	virtual void RefCountDatabaseAfterCommit(BackupStoreRefCountDatabase& refcount_db);
	// AfterDiscard() destroys the temporary database object and therefore invalidates
	// any held references to it!
	virtual void RefCountDatabaseAfterDiscard(BackupStoreRefCountDatabase& refcount_db);
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
	~RaidBackupFileSystem()
	{
		// Close any open refcount DBs before partially destroying the
		// BackupFileSystem that they need to close down. Need to do this in the
		// subclass to avoid calling SaveRefCountDatabase() when the subclass
		// has already been partially destroyed.
		// http://stackoverflow.com/questions/10707286/how-to-resolve-pure-virtual-method-called
		if(mapPotentialRefCountDatabase.get())
		{
			mapPotentialRefCountDatabase->Discard();
			mapPotentialRefCountDatabase.reset();
		}

		mapPermanentRefCountDatabase.reset();

		if(mWriteLock.GotLock())
		{
			ReleaseLock();
		}
	}
	virtual void TryGetLock();
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
		IOStream& rFileData, BackupStoreRefCountDatabase::refcount_t refcount);
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData,
		BackupStoreRefCountDatabase::refcount_t refcount);
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID)
	{
		// For RaidBackupFileSystem, GetObject() is equivalent to GetFile().
		return GetObject(ObjectID);
	}
	virtual std::auto_ptr<IOStream> GetFilePatch(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain);
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
	virtual void DeleteObjectUnknown(int64_t ObjectID);
	virtual bool CanMergePatches() { return true; }
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
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfoInternal(bool ReadOnly);
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineFileOrDiff(int64_t OlderPatchID, int64_t NewerObjectID, bool NewerIsPatch);

private:
	void CheckObjectsScanDir(int64_t StartID, int Level, const std::string &rDirName,
		CheckObjectsResult& Result, bool fix_errors);
	void CheckObjectsDir(int64_t StartID,
		BackupFileSystem::CheckObjectsResult& Result, bool fix_errors);
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
	std::string mBasePath, mCacheDirectory;
	NamedLock mCacheLock;
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
	std::string GetRefCountDatabaseCachePath()
	{
		return mCacheDirectory + DIRECTORY_SEPARATOR + S3_REFCOUNT_FILE_NAME;
	}
	void GetCacheLock();
	std::string GetObjectRelativeURI(int64_t ObjectID, int Type);

public:
	S3BackupFileSystem(const Configuration& config, const std::string& BasePath,
		const std::string& CacheDirectory, S3Client& rClient);
	~S3BackupFileSystem();
	virtual void TryGetLock();
	virtual void ReleaseLock();
	virtual bool HaveLock()
	{
		return mHaveLock;
	}
	virtual int GetBlockSize();
	virtual void PutBackupStoreInfo(BackupStoreInfo& rInfo);
	virtual BackupStoreRefCountDatabase& GetPotentialRefCountDatabase();
	virtual BackupStoreRefCountDatabase& GetPermanentRefCountDatabase(bool ReadOnly);
	virtual bool ObjectExists(int64_t ObjectID, int64_t *pRevisionID = 0);
	virtual std::auto_ptr<IOStream> GetObject(int64_t ObjectID, bool required = true);
	virtual void GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut);
	virtual void PutDirectory(BackupStoreDirectory& rDir);
	virtual std::auto_ptr<Transaction> PutFileComplete(int64_t ObjectID,
		IOStream& rFileData, BackupStoreRefCountDatabase::refcount_t refcount);
	virtual std::auto_ptr<Transaction> PutFilePatch(int64_t ObjectID,
		int64_t DiffFromFileID, IOStream& rPatchData,
		BackupStoreRefCountDatabase::refcount_t refcount)
	{
		return PutFileComplete(ObjectID, rPatchData, refcount);
	}
	virtual std::auto_ptr<IOStream> GetFile(int64_t ObjectID);
	virtual std::auto_ptr<IOStream> GetFilePatch(int64_t ObjectID,
		std::vector<int64_t>& rPatchChain)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void DeleteFile(int64_t ObjectID);
	virtual void DeleteDirectory(int64_t ObjectID);
	virtual void DeleteObjectUnknown(int64_t ObjectID);
	std::string GetObjectURL(const std::string& ObjectPath) const;
	HTTPResponse GetObject(const std::string& ObjectPath)
	{
		return mrClient.GetObject(ObjectPath);
	}
	HTTPResponse HeadObject(const std::string& ObjectPath)
	{
		return mrClient.HeadObject(ObjectPath);
	}
	HTTPResponse PutObject(const std::string& ObjectPath,
		IOStream& rStreamToSend, const char* pContentType = NULL)
	{
		return mrClient.PutObject(ObjectPath, rStreamToSend, pContentType);
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
	virtual bool CanMergePatches() { return false; }
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineFile(int64_t OlderPatchID, int64_t NewerFileID)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	std::auto_ptr<BackupFileSystem::Transaction>
		CombineDiffs(int64_t OlderPatchID, int64_t NewerPatchID)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual std::string GetAccountIdentifier();
	virtual int GetAccountID() { return S3_FAKE_ACCOUNT_ID; }
	virtual int64_t GetFileSizeInBlocks(int64_t ObjectID);
	virtual CheckObjectsResult CheckObjects(bool fix_errors);
	virtual void EnsureObjectIsPermanent(int64_t ObjectID, bool fix_errors)
	{
		// Filesystem is not transactional, so nothing to do here.
	}

private:
	std::string GetObjectURI(int64_t ObjectID, int Type) const;
	typedef std::map<int64_t, std::vector<std::string> > start_id_to_files_t;
	void CheckObjectsScanDir(int64_t start_id, int level, const std::string &dir_name,
		CheckObjectsResult& result, bool fix_errors,
		start_id_to_files_t& start_id_to_files);
	void CheckObjectsDir(int64_t start_id,
		BackupFileSystem::CheckObjectsResult& result, bool fix_errors,
		const start_id_to_files_t& start_id_to_files);

protected:
	virtual std::auto_ptr<BackupStoreInfo> GetBackupStoreInfoInternal(bool ReadOnly);
	virtual void SaveRefCountDatabase(BackupStoreRefCountDatabase& refcount_db);
};

#endif // BACKUPFILESYSTEM__H
