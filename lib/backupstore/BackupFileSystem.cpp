// --------------------------------------------------------------------------
//
// File
//		Name:    BackupFileSystem.cpp
//		Purpose: Generic interface for reading and writing files and
//			 directories, abstracting over RaidFile, S3, FTP etc.
//		Created: 2015/08/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_PWD_H
#	include <pwd.h>
#endif

#ifdef HAVE_LMCONS_H
#	include <lmcons.h>
#endif

#ifdef HAVE_PROCESS_H
#	include <process.h> // for getpid() on Windows
#endif

#include <sys/types.h>

#include <sstream>

#include "autogen_BackupStoreException.h"
#include "BackupConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupFileSystem.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreFile.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "BufferedStream.h"
#include "BufferedWriteStream.h"
#include "ByteCountingStream.h"
#include "CollectInBufferStream.h"
#include "Configuration.h"
#include "BackupStoreObjectMagic.h"
#include "HTTPResponse.h"
#include "IOStream.h"
#include "InvisibleTempFileStream.h"
#include "RaidFileController.h"
#include "RaidFileRead.h"
#include "RaidFileUtil.h"
#include "RaidFileWrite.h"
#include "S3Client.h"
#include "StoreStructure.h"

#include "MemLeakFindOn.h"

const FileSystemCategory BackupFileSystem::LOCKING("Locking");

void BackupFileSystem::GetLock(int max_tries)
{
	if(HaveLock())
	{
		// Account already locked, nothing to do
		return;
	}

	int i = 0;
	for(; i < max_tries || max_tries == KEEP_TRYING_FOREVER; i++)
	{
		try
		{
			TryGetLock();

			// If that didn't throw an exception, then we should have successfully
			// got a lock!
			ASSERT(HaveLock());
			break;
		}
		catch(BackupStoreException &e)
		{
			if(EXCEPTION_IS_TYPE(e, BackupStoreException, CouldNotLockStoreAccount) &&
				((i < max_tries - 1) || max_tries == KEEP_TRYING_FOREVER))
			{
				// Sleep a bit, and try again, as long as we have retries left.

				if(i == 0)
				{
					BOX_LOG_CATEGORY(Log::INFO, LOCKING, "Failed to lock "
						"account " << GetAccountIdentifier() << ", "
						"still trying...");
				}
				else if(i == 30)
				{
					BOX_LOG_CATEGORY(Log::WARNING, LOCKING, "Failed to lock "
						"account " << GetAccountIdentifier() << " for "
						"30 seconds, still trying...");
				}

				ShortSleep(MilliSecondsToBoxTime(1000), true);
				// Will try again
			}
			else if(EXCEPTION_IS_TYPE(e, BackupStoreException,
				CouldNotLockStoreAccount))
			{
				BOX_LOG_CATEGORY(Log::INFO, LOCKING, "Failed to lock account " <<
					GetAccountIdentifier() << " after " << (i + 1) << " "
					"attempts, giving up");
				throw;
			}
			else
			{
				BOX_LOG_CATEGORY(Log::INFO, LOCKING, "Failed to lock account " <<
					GetAccountIdentifier() << " " "with unexpected error: " <<
					e.what());
				throw;
			}
		}
	}

	// If that didn't throw an exception, then we should have successfully got a lock!
	ASSERT(HaveLock());
	BOX_LOG_CATEGORY(Log::TRACE, LOCKING, "Successfully locked account " <<
		GetAccountIdentifier() << " after " << (i + 1) << " attempts");
}

void BackupFileSystem::ReleaseLock()
{
	if(HaveLock())
	{
		BOX_LOG_CATEGORY(Log::TRACE, LOCKING, "Releasing lock on account " <<
			GetAccountIdentifier());
	}

	mapBackupStoreInfo.reset();

	if(mapPotentialRefCountDatabase.get())
	{
		mapPotentialRefCountDatabase->Discard();
		mapPotentialRefCountDatabase.reset();
	}

	mapPermanentRefCountDatabase.reset();
}

// Refresh forces the any current BackupStoreInfo to be discarded and reloaded from the
// store. This would be dangerous if anyone was holding onto a reference to it!
BackupStoreInfo& BackupFileSystem::GetBackupStoreInfo(bool ReadOnly, bool Refresh)
{
	if(mapBackupStoreInfo.get())
	{
		if(!Refresh && (ReadOnly || !mapBackupStoreInfo->IsReadOnly()))
		{
			// Return the current BackupStoreInfo
			return *mapBackupStoreInfo;
		}
		else
		{
			// Need to reopen to change from read-only to read-write.
			mapBackupStoreInfo.reset();
		}
	}

	mapBackupStoreInfo = GetBackupStoreInfoInternal(ReadOnly);
	return *mapBackupStoreInfo;
}


void BackupFileSystem::RefCountDatabaseBeforeCommit(BackupStoreRefCountDatabase& refcount_db)
{
	ASSERT(&refcount_db == mapPotentialRefCountDatabase.get());
	// This is the temporary database, so it is about to be committed and become the permanent
	// database, so we need to close the current permanent database (if any) first.
	mapPermanentRefCountDatabase.reset();
}


void BackupFileSystem::RefCountDatabaseAfterCommit(BackupStoreRefCountDatabase& refcount_db)
{
	// Can only commit a temporary database:
	ASSERT(&refcount_db == mapPotentialRefCountDatabase.get());

	// This was the temporary database, but it is now permanent, and has replaced the
	// permanent file, so we need to change the databases returned by the temporary
	// and permanent getter functions:
	mapPermanentRefCountDatabase = mapPotentialRefCountDatabase;

	// And save it to permanent storage (TODO: avoid double Save() by AfterCommit
	// handler and refcount DB destructor):
	SaveRefCountDatabase(refcount_db);

	// Reopen the database that was closed by Commit().
	mapPermanentRefCountDatabase->Reopen();
}


void BackupFileSystem::RefCountDatabaseAfterDiscard(BackupStoreRefCountDatabase& refcount_db)
{
	ASSERT(&refcount_db == mapPotentialRefCountDatabase.get());
	mapPotentialRefCountDatabase.reset();
}


// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreRefCountDatabaseWrapper
//		Purpose: Wrapper around BackupStoreRefCountDatabase that
//			 automatically notifies the BackupFileSystem when
//			 Commit() or Discard() is called.
//		Created: 2016/04/16
//
// --------------------------------------------------------------------------

typedef BackupStoreRefCountDatabase::refcount_t refcount_t;

class BackupStoreRefCountDatabaseWrapper : public BackupStoreRefCountDatabase
{
private:
	// No copying allowed
	BackupStoreRefCountDatabaseWrapper(const BackupStoreRefCountDatabase &);
	std::auto_ptr<BackupStoreRefCountDatabase> mapUnderlying;
	BackupFileSystem& mrFileSystem;

public:
	BackupStoreRefCountDatabaseWrapper(
		std::auto_ptr<BackupStoreRefCountDatabase> apUnderlying,
		BackupFileSystem& filesystem)
	: mapUnderlying(apUnderlying),
	  mrFileSystem(filesystem)
	{ }
	virtual ~BackupStoreRefCountDatabaseWrapper()
	{
		// If this is the permanent database, and not read-only, it to permanent
		// storage on destruction. (TODO: avoid double Save() by AfterCommit
		// handler and this destructor)
		if(this == mrFileSystem.mapPermanentRefCountDatabase.get() &&
			// ReadOnly: don't make the database read-write if it isn't already
			!IsReadOnly())
		{
			mrFileSystem.SaveRefCountDatabase(*this);
		}
	}

	virtual void Commit()
	{
		mrFileSystem.RefCountDatabaseBeforeCommit(*this);
		mapUnderlying->Commit();
		// Upload the changed file to the server.
		mrFileSystem.RefCountDatabaseAfterCommit(*this);
	}
	virtual void Discard()
	{
		mapUnderlying->Discard();
		mrFileSystem.RefCountDatabaseAfterDiscard(*this);
	}
	virtual void Close() { mapUnderlying->Close(); }
	virtual void Reopen() { mapUnderlying->Reopen(); }
	virtual bool IsReadOnly() { return mapUnderlying->IsReadOnly(); }

	// Data access functions
	virtual refcount_t GetRefCount(int64_t ObjectID) const
	{
		return mapUnderlying->GetRefCount(ObjectID);
	}
	virtual int64_t GetLastObjectIDUsed() const
	{
		return mapUnderlying->GetLastObjectIDUsed();
	}

	// Data modification functions
	virtual void AddReference(int64_t ObjectID)
	{
		mapUnderlying->AddReference(ObjectID);
	}
	// RemoveReference returns false if refcount drops to zero
	virtual bool RemoveReference(int64_t ObjectID)
	{
		return mapUnderlying->RemoveReference(ObjectID);
	}
	virtual int ReportChangesTo(BackupStoreRefCountDatabase& rOldRefs)
	{
		return mapUnderlying->ReportChangesTo(rOldRefs);
	}
};


void RaidBackupFileSystem::TryGetLock()
{
	if(mWriteLock.GotLock())
	{
		return;
	}

	// Make the filename of the write lock file
	std::string writeLockFile;
	StoreStructure::MakeWriteLockFilename(mAccountRootDir, mStoreDiscSet,
		writeLockFile);

	// Request the lock
	if(!mWriteLock.TryAndGetLock(writeLockFile.c_str(),
		0600 /* restrictive file permissions */))
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException,
			CouldNotLockStoreAccount, "Failed to get exclusive "
			"lock on account");
	}
}


std::string RaidBackupFileSystem::GetAccountIdentifier()
{
	std::string account_name;
	try
	{
		account_name = GetBackupStoreInfo(true).GetAccountName();
	}
	catch(RaidFileException &e)
	{
		account_name = "unknown";
	}

	std::ostringstream oss;
	oss << BOX_FORMAT_ACCOUNT(mAccountID) << " (" << account_name << ")";
	return oss.str();
}


std::string RaidBackupFileSystem::GetObjectFileName(int64_t ObjectID,
	bool EnsureDirectoryExists)
{
	std::string filename;
	StoreStructure::MakeObjectFilename(ObjectID, mAccountRootDir, mStoreDiscSet,
		filename, EnsureDirectoryExists);
	return filename;
}


int RaidBackupFileSystem::GetBlockSize()
{
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(mStoreDiscSet));
	return rdiscSet.GetBlockSize();
}


std::auto_ptr<BackupStoreInfo> RaidBackupFileSystem::GetBackupStoreInfoInternal(bool ReadOnly)
{
	// Generate the filename
	std::string fn(mAccountRootDir + INFO_FILENAME);

	// Open the file for reading (passing on optional request for revision ID)
	std::auto_ptr<RaidFileRead> rf(RaidFileRead::Open(mStoreDiscSet, fn));
	std::auto_ptr<BackupStoreInfo> info = BackupStoreInfo::Load(*rf, fn, ReadOnly);

	// Check it
	if(info->GetAccountID() != mAccountID)
	{
		THROW_FILE_ERROR("Found wrong account ID in store info",
			fn, BackupStoreException, BadStoreInfoOnLoad);
	}

	return info;
}


void RaidBackupFileSystem::PutBackupStoreInfo(BackupStoreInfo& rInfo)
{
	if(rInfo.IsReadOnly())
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, StoreInfoIsReadOnly,
			"Tried to save BackupStoreInfo when configured as read-only");
	}

	std::string filename(mAccountRootDir + INFO_FILENAME);
	RaidFileWrite rf(mStoreDiscSet, filename);
	rf.Open(true); // AllowOverwrite
	rInfo.Save(rf);

	// Commit it to disc, converting it to RAID now
	rf.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
}


BackupStoreRefCountDatabase& RaidBackupFileSystem::GetPermanentRefCountDatabase(
	bool ReadOnly)
{
	if(mapPermanentRefCountDatabase.get())
	{
		return *mapPermanentRefCountDatabase;
	}

	// It's dangerous to have two read-write databases open at the same time (it would
	// be too easy to update the refcounts in the wrong one by mistake), and temporary
	// databases are always read-write, so if a temporary database is already open
	// then we should only allow a read-only permanent database to be opened.
	ASSERT(!mapPotentialRefCountDatabase.get() || ReadOnly);

	BackupStoreAccountDatabase::Entry account(mAccountID, mStoreDiscSet);
	mapPermanentRefCountDatabase = BackupStoreRefCountDatabase::Load(account,
		ReadOnly);
	return *mapPermanentRefCountDatabase;
}


BackupStoreRefCountDatabase& RaidBackupFileSystem::GetPotentialRefCountDatabase()
{
	// Creating the "official" potential refcount DB is actually a change
	// to the store, even if you don't commit it, because it's in the same
	// directory and would conflict with another process trying to do the
	// same thing, so it requires that you hold the write lock.
	ASSERT(mWriteLock.GotLock());

	if(mapPotentialRefCountDatabase.get())
	{
		return *mapPotentialRefCountDatabase;
	}

	// It's dangerous to have two read-write databases open at the same
	// time (it would be too easy to update the refcounts in the wrong one
	// by mistake), and temporary databases are always read-write, so if a
	// permanent database is already open then it must be a read-only one.
	ASSERT(!mapPermanentRefCountDatabase.get() ||
		mapPermanentRefCountDatabase->IsReadOnly());

	// We deliberately do not give the caller control of the
	// reuse_existing_file parameter to Create(), because that would make
	// it easy to bypass the restriction of only one (committable)
	// temporary database at a time, and to accidentally overwrite the main
	// refcount DB.
	BackupStoreAccountDatabase::Entry account(mAccountID, mStoreDiscSet);
	std::auto_ptr<BackupStoreRefCountDatabase> ap_new_db =
		BackupStoreRefCountDatabase::Create(account);
	mapPotentialRefCountDatabase.reset(
		new BackupStoreRefCountDatabaseWrapper(ap_new_db, *this));

	return *mapPotentialRefCountDatabase;
}


void RaidBackupFileSystem::SaveRefCountDatabase(BackupStoreRefCountDatabase& refcount_db)
{
	// You can only save the permanent database.
	ASSERT(&refcount_db == mapPermanentRefCountDatabase.get());

	// The database is already saved in the store, so we don't need to do anything.
}


//! Returns whether an object (a file or directory) exists with this object ID, and its
//! revision ID, which for a RaidFile is based on its timestamp and file size.
bool RaidBackupFileSystem::ObjectExists(int64_t ObjectID, int64_t *pRevisionID)
{
	// Don't bother creating the containing directory if it doesn't exist.
	std::string filename = GetObjectFileName(ObjectID, false);
	return RaidFileRead::FileExists(mStoreDiscSet, filename, pRevisionID);
}


//! Reads a directory with the specified ID into the supplied BackupStoreDirectory
//! object, also initialising its revision ID and SizeInBlocks.
void RaidBackupFileSystem::GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut)
{
	int64_t revID = 0;
	// Don't bother creating the containing directory if it doesn't exist.
	std::string filename = GetObjectFileName(ObjectID, false);
	std::auto_ptr<RaidFileRead> objectFile(RaidFileRead::Open(mStoreDiscSet,
		filename, &revID));

	// Read it from the stream, then set it's revision ID
	BufferedStream buf(*objectFile);
	rDirOut.ReadFromStream(buf, IOStream::TimeOutInfinite);
	rDirOut.SetRevisionID(revID);

	// Make sure the size of the directory is available for writing the dir back
	int64_t dirSize = objectFile->GetDiscUsageInBlocks();
	ASSERT(dirSize > 0);
	rDirOut.SetUserInfo1_SizeInBlocks(dirSize);
}


void RaidBackupFileSystem::PutDirectory(BackupStoreDirectory& rDir)
{
	// Create the containing directory if it doesn't exist.
	std::string filename = GetObjectFileName(rDir.GetObjectID(), true);
	RaidFileWrite writeDir(mStoreDiscSet, filename);
	writeDir.Open(true); // allow overwriting

	BufferedWriteStream buffer(writeDir);
	rDir.WriteToStream(buffer);
	buffer.Flush();

	// get the disc usage (must do this before commiting it)
	int64_t dirSize = writeDir.GetDiscUsageInBlocks();

	// Commit directory
	writeDir.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);

	int64_t revid = 0;
	if(!RaidFileRead::FileExists(mStoreDiscSet, filename, &revid))
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}

	rDir.SetUserInfo1_SizeInBlocks(dirSize);
	rDir.SetRevisionID(revid);
}


class RaidPutFileCompleteTransaction : public BackupFileSystem::Transaction
{
private:
	RaidFileWrite mStoreFile;
	std::string mFileName;
#ifndef BOX_RELEASE_BUILD
	int mDiscSet;
#endif
	bool mCommitted;

public:
	RaidPutFileCompleteTransaction(int StoreDiscSet, const std::string& filename,
		BackupStoreRefCountDatabase::refcount_t refcount)
	: mStoreFile(StoreDiscSet, filename, refcount),
	  mFileName(filename),
#ifndef BOX_RELEASE_BUILD
	  mDiscSet(StoreDiscSet),
#endif
	  mCommitted(false),
	  mNumBlocks(-1)
	{ }
	~RaidPutFileCompleteTransaction();
	virtual void Commit();
	virtual int64_t GetNumBlocks()
	{
		ASSERT(mNumBlocks != -1);
		return mNumBlocks;
	}
	RaidFileWrite& GetRaidFile() { return mStoreFile; }

	// It doesn't matter what we return here, because this should never be called
	// for a PutFileCompleteTransaction (the API is intended for
	// PutFilePatchTransaction instead):
	virtual bool IsNewFileIndependent() { return false; }

	int64_t mNumBlocks;
};


void RaidPutFileCompleteTransaction::Commit()
{
	ASSERT(!mCommitted);
	mStoreFile.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);

#ifndef BOX_RELEASE_BUILD
	// Verify the file -- only necessary for non-diffed versions.
	//
	// We cannot use VerifyEncodedFileFormat() until the file is committed. We already
	// verified it as we were saving it, so this is a double check that should not be
	// necessary, and thus is only done in debug builds.
	std::auto_ptr<RaidFileRead> checkFile(RaidFileRead::Open(mDiscSet, mFileName));
	if(!BackupStoreFile::VerifyEncodedFileFormat(*checkFile))
	{
		mStoreFile.Delete();
		THROW_EXCEPTION_MESSAGE(BackupStoreException, AddedFileDoesNotVerify,
			"Newly saved file does not verify after write: this should not "
			"happen: " << mFileName);
	}
#endif // !BOX_RELEASE_BUILD

	mCommitted = true;
}


RaidPutFileCompleteTransaction::~RaidPutFileCompleteTransaction()
{
	if(!mCommitted)
	{
		mStoreFile.Discard();
	}
}


std::auto_ptr<BackupFileSystem::Transaction>
RaidBackupFileSystem::PutFileComplete(int64_t ObjectID, IOStream& rFileData,
	BackupStoreRefCountDatabase::refcount_t refcount)
{
	// Create the containing directory if it doesn't exist.
	std::string filename = GetObjectFileName(ObjectID, true);

	// We can only do this when the file (ObjectID) doesn't already exist.
	ASSERT(refcount == 0);

	// But RaidFileWrite won't allow us to write to a file that doesn't have exactly one
	// reference, so we pretend for now that there is one. If the file already exists,
	// then Open(false) below will raise an exception for us.
	RaidPutFileCompleteTransaction* pTrans = new RaidPutFileCompleteTransaction(
		mStoreDiscSet, filename, 1);
	std::auto_ptr<BackupFileSystem::Transaction> apTrans(pTrans);

	RaidFileWrite& rStoreFile(pTrans->GetRaidFile());
	rStoreFile.Open(false); // no overwriting

	BackupStoreFile::VerifyStream validator(rFileData);

	// A full file, just store to disc
	if(!validator.CopyStreamTo(rStoreFile, BACKUP_STORE_TIMEOUT))
	{
		THROW_EXCEPTION(BackupStoreException, ReadFileFromStreamTimedOut);
	}

	// Close() is necessary to perform final validation on the block index.
	validator.Close(false); // Don't CloseCopyStream, RaidFile doesn't like it.

	// Need to do this before committing the RaidFile, can't do it after.
	pTrans->mNumBlocks = rStoreFile.GetDiscUsageInBlocks();

	return apTrans;
}


class RaidPutFilePatchTransaction : public BackupFileSystem::Transaction
{
private:
	RaidFileWrite mNewCompleteFile;
	RaidFileWrite mReversedPatchFile;
	bool mReversedDiffIsCompletelyDifferent;
	int64_t mBlocksUsedByNewFile;
	int64_t mChangeInBlocksUsedByOldFile;

public:
	RaidPutFilePatchTransaction(int StoreDiscSet,
		const std::string& newCompleteFilename,
		const std::string& reversedPatchFilename,
		BackupStoreRefCountDatabase::refcount_t refcount)
	// It's not quite true that mNewCompleteFile has 1 reference: it doesn't exist
	// yet, so it has 0 right now. However when the transaction is committed it will
	// have 1, and RaidFileWrite gets upset if we try to modify a file with != 1
	// references, so we need to pretend now that we already have the reference.
	: mNewCompleteFile(StoreDiscSet, newCompleteFilename, 1),
	  mReversedPatchFile(StoreDiscSet, reversedPatchFilename, refcount),
	  mReversedDiffIsCompletelyDifferent(false),
	  mBlocksUsedByNewFile(0),
	  mChangeInBlocksUsedByOldFile(0)
	{ }
	virtual void Commit();
	RaidFileWrite& GetNewCompleteFile()   { return mNewCompleteFile; }
	RaidFileWrite& GetReversedPatchFile() { return mReversedPatchFile; }
	void SetReversedDiffIsCompletelyDifferent(bool IsCompletelyDifferent)
	{
		mReversedDiffIsCompletelyDifferent = IsCompletelyDifferent;
	}
	virtual bool IsNewFileIndependent()
	{
		return mReversedDiffIsCompletelyDifferent;
	}
	void SetBlocksUsedByNewFile(int64_t BlocksUsedByNewFile)
	{
		mBlocksUsedByNewFile = BlocksUsedByNewFile;
	}
	virtual int64_t GetNumBlocks()
	{
		return mBlocksUsedByNewFile;
	}
	void SetChangeInBlocksUsedByOldFile(int64_t ChangeInBlocksUsedByOldFile)
	{
		mChangeInBlocksUsedByOldFile = ChangeInBlocksUsedByOldFile;
	}
	virtual int64_t GetChangeInBlocksUsedByOldFile()
	{
		return mChangeInBlocksUsedByOldFile;
	}
};


void RaidPutFilePatchTransaction::Commit()
{
	mNewCompleteFile.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
	mReversedPatchFile.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
}


std::auto_ptr<BackupFileSystem::Transaction>
RaidBackupFileSystem::PutFilePatch(int64_t ObjectID, int64_t DiffFromFileID,
	IOStream& rPatchData, BackupStoreRefCountDatabase::refcount_t refcount)
{
	// Create the containing directory if it doesn't exist.
	std::string newVersionFilename = GetObjectFileName(ObjectID, true);

	// Filename of the old version
	std::string oldVersionFilename = GetObjectFileName(DiffFromFileID,
		false); // no need to make sure the directory it's in exists

	RaidPutFilePatchTransaction* pTrans = new RaidPutFilePatchTransaction(
		mStoreDiscSet, newVersionFilename, oldVersionFilename, refcount);
	std::auto_ptr<BackupFileSystem::Transaction> apTrans(pTrans);

	RaidFileWrite& rNewCompleteFile(pTrans->GetNewCompleteFile());
	RaidFileWrite& rReversedPatchFile(pTrans->GetReversedPatchFile());

	rNewCompleteFile.Open(false); // no overwriting

	// Diff file, needs to be recreated.
	// Choose a temporary filename.
	std::string tempFn(RaidFileController::DiscSetPathToFileSystemPath(mStoreDiscSet,
		newVersionFilename + ".difftemp",
		1)); // NOT the same disc as the write file, to avoid using lots of space on the same disc unnecessarily

	try
	{
		// Open it twice
#ifdef WIN32
		InvisibleTempFileStream diff(tempFn.c_str(), O_RDWR | O_CREAT | O_BINARY);
		InvisibleTempFileStream diff2(tempFn.c_str(), O_RDWR | O_BINARY);
#else
		FileStream diff(tempFn.c_str(), O_RDWR | O_CREAT | O_EXCL);
		FileStream diff2(tempFn.c_str(), O_RDONLY);

		// Unlink it immediately, so it definitely goes away
		if(EMU_UNLINK(tempFn.c_str()) != 0)
		{
			THROW_EXCEPTION(CommonException, OSFileError);
		}
#endif

		// Stream the incoming diff to this temporary file
		if(!rPatchData.CopyStreamTo(diff, BACKUP_STORE_TIMEOUT))
		{
			THROW_EXCEPTION(BackupStoreException, ReadFileFromStreamTimedOut);
		}

		// Verify the diff
		diff.Seek(0, IOStream::SeekType_Absolute);
		if(!BackupStoreFile::VerifyEncodedFileFormat(diff))
		{
			THROW_EXCEPTION(BackupStoreException, AddedFileDoesNotVerify);
		}

		// Seek to beginning of diff file
		diff.Seek(0, IOStream::SeekType_Absolute);

		// Reassemble that diff -- open previous file, and combine the patch and file
		std::auto_ptr<RaidFileRead> from(RaidFileRead::Open(mStoreDiscSet, oldVersionFilename));
		BackupStoreFile::CombineFile(diff, diff2, *from, rNewCompleteFile);

		// Then... reverse the patch back (open the from file again, and create a write file to overwrite it)
		std::auto_ptr<RaidFileRead> from2(RaidFileRead::Open(mStoreDiscSet, oldVersionFilename));
		rReversedPatchFile.Open(true); // allow overwriting
		from->Seek(0, IOStream::SeekType_Absolute);
		diff.Seek(0, IOStream::SeekType_Absolute);

		bool isCompletelyDifferent;
		BackupStoreFile::ReverseDiffFile(diff, *from, *from2, rReversedPatchFile,
				DiffFromFileID, &isCompletelyDifferent);
		pTrans->SetReversedDiffIsCompletelyDifferent(isCompletelyDifferent);

		// Store disc space used
		int64_t oldVersionNewBlocksUsed =
			rReversedPatchFile.GetDiscUsageInBlocks();

		// And make a space adjustment for the size calculation
		int64_t spaceSavedByConversionToPatch = from->GetDiscUsageInBlocks() -
			oldVersionNewBlocksUsed;
		pTrans->SetChangeInBlocksUsedByOldFile(-spaceSavedByConversionToPatch);
		pTrans->SetBlocksUsedByNewFile(rNewCompleteFile.GetDiscUsageInBlocks());
		return apTrans;

		// Everything cleans up here...
	}
	catch(...)
	{
		// Be very paranoid about deleting this temp file -- we could only leave a zero byte file anyway
		EMU_UNLINK(tempFn.c_str());
		throw;
	}
}


std::auto_ptr<IOStream> RaidBackupFileSystem::GetObject(int64_t ObjectID, bool required)
{
	std::string filename = GetObjectFileName(ObjectID,
		false); // no need to make sure the directory it's in exists.

	if(!required && !RaidFileRead::FileExists(mStoreDiscSet, filename))
	{
		return std::auto_ptr<IOStream>();
	}

	std::auto_ptr<RaidFileRead> objectFile(RaidFileRead::Open(mStoreDiscSet,
		filename));
	return static_cast<std::auto_ptr<IOStream> >(objectFile);
}


std::auto_ptr<IOStream> RaidBackupFileSystem::GetFilePatch(int64_t ObjectID,
	std::vector<int64_t>& rPatchChain)
{
	// File exists, but is a patch from a new version. Generate the older version.
	// The last entry in the chain is the full file, the others are patches back from it.
	// Open the last one, which is the current full file.
	std::auto_ptr<IOStream> from(GetFile(rPatchChain[rPatchChain.size() - 1]));

	// Then, for each patch in the chain, do a combine
	for(int p = ((int)rPatchChain.size()) - 2; p >= 0; --p)
	{
		// ID of patch
		int64_t patchID = rPatchChain[p];

		// Open it a couple of times. TODO FIXME: this could be very inefficient.
		std::auto_ptr<IOStream> diff(GetFile(patchID));
		std::auto_ptr<IOStream> diff2(GetFile(patchID));

		// Choose a temporary filename for the result of the combination
		std::ostringstream fs;
		fs << mAccountRootDir << ".recombinetemp." << p;
		std::string tempFn =
			RaidFileController::DiscSetPathToFileSystemPath(mStoreDiscSet,
				fs.str(), p + 16);

		// Open the temporary file
		std::auto_ptr<IOStream> combined(
			new InvisibleTempFileStream(
				tempFn, O_RDWR | O_CREAT | O_EXCL |
				O_BINARY | O_TRUNC));

		// Do the combining
		BackupStoreFile::CombineFile(*diff, *diff2, *from, *combined);

		// Move to the beginning of the combined file
		combined->Seek(0, IOStream::SeekType_Absolute);

		// Then shuffle round for the next go
		if(from.get())
		{
			from->Close();
		}

		from = combined;
	}

	std::auto_ptr<IOStream> stream(
		BackupStoreFile::ReorderFileToStreamOrder(from.get(),
			true)); // take ownership

	// Release from file to avoid double deletion
	from.release();

	return stream;
}


// Delete an object whose type is unknown. For RaidFile, we don't need to know what type
// it is to use RaidFileWrite::Delete() on it.
void RaidBackupFileSystem::DeleteObjectUnknown(int64_t ObjectID)
{
	std::string filename = GetObjectFileName(ObjectID, false);
	RaidFileWrite deleteFile(mStoreDiscSet, filename);
	deleteFile.Delete();
}


std::auto_ptr<BackupFileSystem::Transaction>
RaidBackupFileSystem::CombineFileOrDiff(int64_t OlderPatchID, int64_t NewerObjectID, bool NewerIsPatch)
{
	// This normally only happens during housekeeping, which is using a temporary
	// refcount database, so insist on that for now.
	BackupStoreRefCountDatabase* pRefCount = mapPotentialRefCountDatabase.get();
	ASSERT(pRefCount != NULL);
	ASSERT(mapPermanentRefCountDatabase.get() == NULL ||
		mapPermanentRefCountDatabase->IsReadOnly());

	// Open the older object twice (the patch)
	std::auto_ptr<IOStream> pdiff = GetFile(OlderPatchID);
	std::auto_ptr<IOStream> pdiff2 = GetFile(OlderPatchID);

	// Open the newer object (the file to be deleted)
	std::auto_ptr<IOStream> pobjectBeingDeleted = GetFile(NewerObjectID);

	// And open a write file to overwrite the older object (the patch)
	std::string older_filename = GetObjectFileName(OlderPatchID,
		false); // no need to make sure the directory it's in exists.

	std::auto_ptr<RaidPutFileCompleteTransaction>
		ap_overwrite_older(new RaidPutFileCompleteTransaction(
			mStoreDiscSet, older_filename,
			pRefCount->GetRefCount(OlderPatchID)));
	RaidFileWrite& overwrite_older(ap_overwrite_older->GetRaidFile());
	overwrite_older.Open(true /* allow overwriting */);

	if(NewerIsPatch)
	{
		// Combine two adjacent patches (reverse diffs) into a single one object.
		BackupStoreFile::CombineDiffs(*pobjectBeingDeleted, *pdiff, *pdiff2, overwrite_older);
	}
	else
	{
		// Combine an older patch (reverse diff) with the subsequent complete file.
		BackupStoreFile::CombineFile(*pdiff, *pdiff2, *pobjectBeingDeleted, overwrite_older);
	}

	// Need to do this before committing the RaidFile, can't do it after.
	ap_overwrite_older->mNumBlocks = overwrite_older.GetDiscUsageInBlocks();

	// The file will be committed later when the directory is safely commited.
	return static_cast<std::auto_ptr<BackupFileSystem::Transaction> >(ap_overwrite_older);
}


std::auto_ptr<BackupFileSystem::Transaction>
RaidBackupFileSystem::CombineFile(int64_t OlderPatchID, int64_t NewerFileID)
{
	return CombineFileOrDiff(OlderPatchID, NewerFileID, false); // !NewerIsPatch
}


std::auto_ptr<BackupFileSystem::Transaction>
RaidBackupFileSystem::CombineDiffs(int64_t OlderPatchID, int64_t NewerPatchID)
{
	return CombineFileOrDiff(OlderPatchID, NewerPatchID, true); // NewerIsPatch
}


int64_t RaidBackupFileSystem::GetFileSizeInBlocks(int64_t ObjectID)
{
	std::string filename = GetObjectFileName(ObjectID, false);
	std::auto_ptr<RaidFileRead> apRead = RaidFileRead::Open(mStoreDiscSet, filename);
	return apRead->GetDiscUsageInBlocks();
}


BackupFileSystem::CheckObjectsResult
RaidBackupFileSystem::CheckObjects(bool fix_errors)
{
	// Find the maximum start ID of directories -- worked out by looking at disc
	// contents, not trusting anything.
	CheckObjectsResult result;
	CheckObjectsScanDir(0, 1, mAccountRootDir, result, fix_errors);

	// Then go through and scan all the objects within those directories
	for(int64_t start_id = 0; start_id <= result.maxObjectIDFound;
		start_id += (1<<STORE_ID_SEGMENT_LENGTH))
	{
		CheckObjectsDir(start_id, result, fix_errors);
	}

	return result;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    static TwoDigitHexToInt(const char *, int &)
//		Purpose: Convert a two digit hex string to an int, returning whether it's valid or not
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
static inline bool TwoDigitHexToInt(const char *String, int &rNumberOut)
{
	int n = 0;
	// Char 0
	if(String[0] >= '0' && String[0] <= '9')
	{
		n = (String[0] - '0') << 4;
	}
	else if(String[0] >= 'a' && String[0] <= 'f')
	{
		n = ((String[0] - 'a') + 0xa) << 4;
	}
	else
	{
		return false;
	}
	// Char 1
	if(String[1] >= '0' && String[1] <= '9')
	{
		n |= String[1] - '0';
	}
	else if(String[1] >= 'a' && String[1] <= 'f')
	{
		n |= (String[1] - 'a') + 0xa;
	}
	else
	{
		return false;
	}

	// Return a valid number
	rNumberOut = n;
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidBackupFileSystem::CheckObjectsScanDir(
//			 int64_t StartID, int Level,
//			 const std::string &rDirName,
//			 BackupFileSystem::CheckObjectsResult& Result,
//			 bool fix_errors)
//		Purpose: Read in the contents of the directory, recurse to
//			 lower levels, return the maximum starting ID of any
//			 directory found. Internal method.
//		Created: 21/4/04
//
// --------------------------------------------------------------------------
void RaidBackupFileSystem::CheckObjectsScanDir(int64_t StartID, int Level,
	const std::string &rDirName, BackupFileSystem::CheckObjectsResult& Result,
	bool fix_errors)
{
	//TRACE2("Scan directory for max dir starting ID %s, StartID %lld\n", rDirName.c_str(), StartID);

	if(Result.maxObjectIDFound < StartID)
	{
		Result.maxObjectIDFound = StartID;
	}

	// Read in all the directories, and recurse downwards.
	// If any of the directories is missing, create it.
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mStoreDiscSet));

	if(!rdiscSet.IsNonRaidSet())
	{
		unsigned int numDiscs = rdiscSet.size();

		for(unsigned int l = 0; l < numDiscs; ++l)
		{
			// build name
			std::string dn(rdiscSet[l] + DIRECTORY_SEPARATOR + rDirName);
			EMU_STRUCT_STAT st;

			if(EMU_STAT(dn.c_str(), &st) != 0 &&
				errno == ENOENT)
			{
				if(mkdir(dn.c_str(), 0755) != 0)
				{
					THROW_SYS_FILE_ERROR("Failed to create missing "
						"RaidFile directory", dn,
						RaidFileException, OSError);
				}
			}
		}
	}

	std::vector<std::string> dirs;
	RaidFileRead::ReadDirectoryContents(mStoreDiscSet, rDirName,
		RaidFileRead::DirReadType_DirsOnly, dirs);

	for(std::vector<std::string>::const_iterator i(dirs.begin());
		i != dirs.end(); ++i)
	{
		// Check to see if it's the right name
		int n = 0;
		if((*i).size() == 2 && TwoDigitHexToInt((*i).c_str(), n)
			&& n < (1<<STORE_ID_SEGMENT_LENGTH))
		{
			// Next level down
			BackupFileSystem::CheckObjectsResult sub_result;

			CheckObjectsScanDir(
				StartID | (n << (Level * STORE_ID_SEGMENT_LENGTH)),
				Level + 1, rDirName + DIRECTORY_SEPARATOR + *i,
				sub_result, fix_errors);
			Result.numErrorsFound += sub_result.numErrorsFound;

			// Found a greater starting ID?
			if(Result.maxObjectIDFound < sub_result.maxObjectIDFound)
			{
				Result.maxObjectIDFound = sub_result.maxObjectIDFound;
			}
		}
		else
		{
			BOX_ERROR("Spurious or invalid directory '" << rDirName <<
				DIRECTORY_SEPARATOR << (*i) << "' found, " <<
				(fix_errors?"deleting":"delete manually"));
			++Result.numErrorsFound;
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidBackupFileSystem::CheckObjectsDir(int64_t StartID,
//			 BackupFileSystem::CheckObjectsResult& Result,
//			 bool fix_errors)
//		Purpose: Check all the files within this directory which has
//			 the given starting ID.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void RaidBackupFileSystem::CheckObjectsDir(int64_t StartID,
	BackupFileSystem::CheckObjectsResult& Result, bool fix_errors)
{
	// Make directory name -- first generate the filename of an entry in it
	std::string dirName;
	StoreStructure::MakeObjectFilename(StartID, mAccountRootDir, mStoreDiscSet,
		dirName, false /* don't make sure the dir exists */);

	// Check expectations
	ASSERT(dirName.size() > 4 &&
		(dirName[dirName.size() - 4] == DIRECTORY_SEPARATOR_ASCHAR ||
		 dirName[dirName.size() - 4] == '/'));
	// Remove the filename from it
	dirName.resize(dirName.size() - 4); // four chars for "/o00"

	// Check directory exists
	if(!RaidFileRead::DirectoryExists(mStoreDiscSet, dirName))
	{
		BOX_ERROR("RaidFile dir " << dirName << " does not exist");
		Result.numErrorsFound++;
		return;
	}

	// Read directory contents
	std::vector<std::string> files;
	RaidFileRead::ReadDirectoryContents(mStoreDiscSet, dirName,
		RaidFileRead::DirReadType_FilesOnly, files);

	// Parse each entry, building up a list of object IDs which are present in the dir.
	// This is done so that whatever order is retured from the directory, objects are scanned
	// in order.
	// Filename must begin with a 'o' and be three characters long, otherwise it gets deleted.
	for(std::vector<std::string>::const_iterator i(files.begin()); i != files.end(); ++i)
	{
		bool fileOK = true;
		int n = 0;
		if((*i).size() == 3 && (*i)[0] == 'o' && TwoDigitHexToInt((*i).c_str() + 1, n)
			&& n < (1<<STORE_ID_SEGMENT_LENGTH))
		{
			// Filename is valid, mark as existing
			if(Result.maxObjectIDFound < (StartID | n))
			{
				Result.maxObjectIDFound = (StartID | n);
			}
		}
		// No other files should be present in subdirectories
		else if(StartID != 0)
		{
			fileOK = false;
		}
		// info and refcount databases are OK in the root directory
		else if(*i == "info" || *i == "refcount.db" ||
			*i == "refcount.rdb" || *i == "refcount.rdbX")
		{
			fileOK = true;
		}
		else
		{
			fileOK = false;
		}

		if(!fileOK)
		{
			// Unexpected or bad file, delete it
			BOX_ERROR("Spurious file " << dirName <<
				DIRECTORY_SEPARATOR << (*i) << " found" <<
				(fix_errors?", deleting":""));
			++Result.numErrorsFound;
			if(fix_errors)
			{
				RaidFileWrite del(mStoreDiscSet,
					dirName + DIRECTORY_SEPARATOR + *i);
				del.Delete();
			}
		}
	}
}


void RaidBackupFileSystem::EnsureObjectIsPermanent(int64_t ObjectID, bool fix_errors)
{
	// If it looks like a good object, and it's non-RAID, and
	// this is a RAID set, then convert it to RAID.

	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mStoreDiscSet));
	if(!rdiscSet.IsNonRaidSet())
	{
		// See if the file exists
		std::string filename;
		StoreStructure::MakeObjectFilename(ObjectID, mAccountRootDir, mStoreDiscSet,
			filename, false /* don't make sure the dir exists */);

		RaidFileUtil::ExistType existance =
			RaidFileUtil::RaidFileExists(rdiscSet, filename);
		if(existance == RaidFileUtil::NonRaid)
		{
			BOX_WARNING("Found non-RAID write file in RAID set" <<
				(fix_errors?", transforming to RAID: ":"") <<
				(fix_errors?filename:""));
			if(fix_errors)
			{
				RaidFileWrite write(mStoreDiscSet, filename);
				write.TransformToRaidStorage();
			}
		}
		else if(existance == RaidFileUtil::AsRaidWithMissingReadable)
		{
			BOX_WARNING("Found damaged but repairable RAID file" <<
				(fix_errors?", repairing: ":"") <<
				(fix_errors?filename:""));
			if(fix_errors)
			{
				std::auto_ptr<RaidFileRead> read(
					RaidFileRead::Open(mStoreDiscSet,
						filename));
				RaidFileWrite write(mStoreDiscSet, filename);
				write.Open(true /* overwrite */);
				read->CopyStreamTo(write);
				read.reset();
				write.Commit(BACKUP_STORE_CONVERT_TO_RAID_IMMEDIATELY);
			}
		}
	}
}


S3BackupFileSystem::S3BackupFileSystem(const Configuration& config,
	const std::string& BasePath, const std::string& CacheDirectory, S3Client& rClient)
: mrConfig(config),
  mBasePath(BasePath),
  mCacheDirectory(CacheDirectory),
  mrClient(rClient),
  mHaveLock(false)
{
	if(mBasePath.size() == 0 || mBasePath[0] != '/' || mBasePath[mBasePath.size() - 1] != '/')
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, BadConfiguration,
			"mBasePath is invalid: must start and end with a slash (/)");
	}

	const Configuration s3config = config.GetSubConfiguration("S3Store");
	const std::string& s3_hostname(s3config.GetKeyValue("HostName"));
	const std::string& s3_base_path(s3config.GetKeyValue("BasePath"));
	mSimpleDBDomain = s3config.GetKeyValue("SimpleDBDomain");

	// The lock name should be the same for all hosts/files/daemons potentially
	// writing to the same region of the S3 store. The default is the Amazon S3 bucket
	// name and path, concatenated.
	mLockName = s3config.GetKeyValueDefault("SimpleDBLockName",
		s3_hostname + s3_base_path);

	// The lock value should be unique for each host potentially accessing the same
	// region of the store, and should help you to identify which one is currently
	// holding the lock. The default is username@hostname(pid).
#if HAVE_DECL_GETUSERNAMEA
	char username_buffer[UNLEN + 1];
	DWORD buffer_size = sizeof(username_buffer);
	if(!GetUserNameA(username_buffer, &buffer_size))
	{
		THROW_WIN_ERROR("Failed to GetUserName()", CommonException, Internal);
	}
	mCurrentUserName = username_buffer;
#elif defined HAVE_GETPWUID
	mCurrentUserName = getpwuid(getuid())->pw_name;
#else
#	error "Don't know how to get current user name"
#endif

	char hostname_buf[1024];
	if(gethostname(hostname_buf, sizeof(hostname_buf)) != 0)
	{
		THROW_SOCKET_ERROR("Failed to get local hostname", CommonException, Internal);
	}
	mCurrentHostName = hostname_buf;

	std::ostringstream lock_value_buf;
	lock_value_buf << mCurrentUserName << "@" << hostname_buf << "(" << getpid() <<
		")";
	mLockValue = s3config.GetKeyValueDefault("SimpleDBLockValue",
		lock_value_buf.str());
}


int S3BackupFileSystem::GetBlockSize()
{
	return S3_NOTIONAL_BLOCK_SIZE;
}


std::string S3BackupFileSystem::GetAccountIdentifier()
{
	std::string name;

	try
	{
		name = GetBackupStoreInfo(true).GetAccountName();
	}
	catch(HTTPException &e)
	{
		if(EXCEPTION_IS_TYPE(e, HTTPException, FileNotFound))
		{
			std::string info_uri = GetMetadataURI(S3_INFO_FILE_NAME);
			std::string info_url = GetObjectURL(info_uri);
			return "unknown (BackupStoreInfo file not found) at " + info_url;
		}
		else
		{
			throw;
		}
	}

	std::ostringstream oss;
	oss << "'" << name << "'";
	return oss.str();
}


std::string S3BackupFileSystem::GetObjectURL(const std::string& ObjectPath) const
{
	const Configuration s3config = mrConfig.GetSubConfiguration("S3Store");
	return std::string("http://") + s3config.GetKeyValue("HostName") + ":" +
		s3config.GetKeyValue("Port") + ObjectPath;
}


int64_t S3BackupFileSystem::GetRevisionID(const std::string& uri,
	HTTPResponse& response) const
{
	std::string etag;

	if(!response.GetHeader("etag", &etag))
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, MissingEtagHeader,
			"Failed to get the MD5 checksum of the file or directory "
			"at this URL: " << GetObjectURL(uri));
	}

	if(etag[0] != '"')
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, InvalidEtagHeader,
			"Failed to get the MD5 checksum of the file or directory "
			"at this URL: " << GetObjectURL(uri));
	}

	const char * pEnd = NULL;
	std::string checksum = etag.substr(1, 16);
	int64_t revID = box_strtoui64(checksum.c_str(), &pEnd, 16);
	if(*pEnd != '\0')
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, InvalidEtagHeader,
			"Failed to get the MD5 checksum of the file or "
			"directory at this URL: " << uri << ": invalid character '" <<
			*pEnd << "' in '" << etag << "'");
	}

	return revID;
}


// TODO FIXME ADD CACHING!
std::auto_ptr<BackupStoreInfo> S3BackupFileSystem::GetBackupStoreInfoInternal(bool ReadOnly)
{
	std::string info_uri = GetMetadataURI(S3_INFO_FILE_NAME);
	std::string info_url = GetObjectURL(info_uri);
	HTTPResponse response = mrClient.GetObject(info_uri);
	mrClient.CheckResponse(response, std::string("No BackupStoreInfo file exists "
		"at this URL: ") + info_url);

	std::auto_ptr<BackupStoreInfo> info = BackupStoreInfo::Load(response, info_url,
		ReadOnly);

	// We don't actually use AccountID to distinguish accounts on S3 stores.
	if(info->GetAccountID() != S3_FAKE_ACCOUNT_ID)
	{
		THROW_FILE_ERROR("Found wrong account ID in store info",
			info_url, BackupStoreException, BadStoreInfoOnLoad);
	}

	return info;
}


void S3BackupFileSystem::PutBackupStoreInfo(BackupStoreInfo& rInfo)
{
	if(rInfo.IsReadOnly())
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, StoreInfoIsReadOnly,
			"Tried to save BackupStoreInfo when configured as read-only");
	}

	CollectInBufferStream out;
	rInfo.Save(out);
	out.SetForReading();

	std::string info_uri = GetMetadataURI(S3_INFO_FILE_NAME);
	HTTPResponse response = mrClient.PutObject(info_uri, out);

	std::string info_url = GetObjectURL(info_uri);
	mrClient.CheckResponse(response, std::string("Failed to upload the new "
		"BackupStoreInfo file to this URL: ") + info_url);
}


void S3BackupFileSystem::GetCacheLock()
{
	if(!mCacheLock.GotLock())
	{
		std::string cache_lockfile_name = mCacheDirectory + DIRECTORY_SEPARATOR +
			S3_CACHE_LOCK_NAME;
		if(!mCacheLock.TryAndGetLock(cache_lockfile_name))
		{
			THROW_FILE_ERROR("Cache directory is locked by another process",
				mCacheDirectory, BackupStoreException, CacheDirectoryLocked);
		}
	}
}


BackupStoreRefCountDatabase& S3BackupFileSystem::GetPermanentRefCountDatabase(
	bool ReadOnly)
{
	if(mapPermanentRefCountDatabase.get())
	{
		return *mapPermanentRefCountDatabase;
	}

	// It's dangerous to have two read-write databases open at the same time (it would
	// be too easy to update the refcounts in the wrong one by mistake), and potential
	// databases are always read-write, so if a potential database is already open
	// then we should only allow a read-only permanent database to be opened.
	ASSERT(!mapPotentialRefCountDatabase.get() || ReadOnly);

	GetCacheLock();

	// If we have a cached copy, check that it's up to date.
	std::string local_path = GetRefCountDatabaseCachePath();
	std::string digest;

	if(FileExists(local_path))
	{
		FileStream fs(local_path);
		MD5DigestStream digester;
		fs.CopyStreamTo(digester);
		digester.Close();
		digest = digester.DigestAsString();
	}

	// Try to fetch it from the remote server. If we pass a digest, and if it matches,
	// then the server won't send us the same file data again.
	std::string uri = GetMetadataURI(S3_REFCOUNT_FILE_NAME);
	HTTPResponse response = mrClient.GetObject(uri, digest);
	if(response.GetResponseCode() == HTTPResponse::Code_OK)
	{
		if(digest.size() > 0)
		{
			BOX_WARNING("We had a cached copy of the refcount DB, but it "
				"didn't match the one in S3, so the server sent us a new "
				"copy and the cache will be updated");
		}

		FileStream fs(local_path, O_CREAT | O_RDWR);
		response.CopyStreamTo(fs);

		// Check that we got the full and correct file. TODO: calculate the MD5
		// digest while writing the file, instead of afterwards.
		fs.Seek(0, IOStream::SeekType_Absolute);
		MD5DigestStream digester;
		fs.CopyStreamTo(digester);
		digester.Close();
		digest = digester.DigestAsString();

		if(response.GetHeaders().GetHeaderValue("etag") !=
			"\"" + digest + "\"")
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException,
				FileDownloadedIncorrectly, "Downloaded invalid file from "
				"S3: expected MD5 digest to be " <<
				response.GetHeaders().GetHeaderValue("etag") << " but "
				"it was actually " << digest);
		}
	}
	else if(response.GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		// Do not create a new refcount DB here, it is not safe! Users may wonder
		// why they have lost all their files, and/or unwittingly overwrite their
		// backup data.
		THROW_EXCEPTION_MESSAGE(BackupStoreException,
			CorruptReferenceCountDatabase, "Reference count database is "
			"missing, cannot safely open account. Please run bbstoreaccounts "
			"check on the account to recreate it. 404 Not Found: " << uri);
	}
	else if(response.GetResponseCode() == HTTPResponse::Code_NotModified)
	{
		// The copy on the server is the same as the one in our cache, so we don't
		// need to download it again, nothing to do!
	}
	else
	{
		mrClient.CheckResponse(response, "Failed to download reference "
			"count database");
	}

	mapPermanentRefCountDatabase = BackupStoreRefCountDatabase::Load(local_path,
		S3_FAKE_ACCOUNT_ID, ReadOnly);
	return *mapPermanentRefCountDatabase;
}


BackupStoreRefCountDatabase& S3BackupFileSystem::GetPotentialRefCountDatabase()
{
	// Creating the "official" temporary refcount DB is actually a change
	// to the cache, even if you don't commit it, because it's in the same
	// directory and would conflict with another process trying to do the
	// same thing, so it requires that you hold the write and cache locks.
	ASSERT(mHaveLock);

	if(mapPotentialRefCountDatabase.get())
	{
		return *mapPotentialRefCountDatabase;
	}

	// It's dangerous to have two read-write databases open at the same time (it would
	// be too easy to update the refcounts in the wrong one by mistake), and temporary
	// databases are always read-write, so if a permanent database is already open
	// then it must be a read-only one.
	ASSERT(!mapPermanentRefCountDatabase.get() ||
		mapPermanentRefCountDatabase->IsReadOnly());

	GetCacheLock();

	// The temporary database cannot be on the remote server, so there is no need to
	// download it into the cache. Just create one and return it.
	std::string local_path = GetRefCountDatabaseCachePath();

	// We deliberately do not give the caller control of the
	// reuse_existing_file parameter to Create(), because that would make
	// it easy to bypass the restriction of only one (committable)
	// temporary database at a time, and to accidentally overwrite the main
	// refcount DB.
	std::auto_ptr<BackupStoreRefCountDatabase> ap_new_db =
		BackupStoreRefCountDatabase::Create(local_path, S3_FAKE_ACCOUNT_ID);
	mapPotentialRefCountDatabase.reset(
		new BackupStoreRefCountDatabaseWrapper(ap_new_db, *this));

	return *mapPotentialRefCountDatabase;
}


void S3BackupFileSystem::SaveRefCountDatabase(BackupStoreRefCountDatabase& refcount_db)
{
	// You can only save the permanent database.
	ASSERT(&refcount_db == mapPermanentRefCountDatabase.get());

	std::string local_path = GetRefCountDatabaseCachePath();
	FileStream fs(local_path, O_RDONLY);

	// Try to upload it to the remote server.
	HTTPResponse response = mrClient.PutObject(GetMetadataURI(S3_REFCOUNT_FILE_NAME),
		fs);
	mrClient.CheckResponse(response, "Failed to upload refcount db to S3");
}


//! Returns whether an object (a file or directory) exists with this object ID, and its
//! revision ID, which for a RaidFile is based on its timestamp and file size.
//!
//! TODO FIXME: we should probably return a hint of whether the object is a file or a
//! directory (because we know), as BackupStoreCheck could use this to avoid repeated
//! wasted requests for the object as a file or a directory, when it's already known that
//! it has a different type. We should also use a cache for directory listings!
bool S3BackupFileSystem::ObjectExists(int64_t ObjectID, int64_t *pRevisionID)
{
	std::string uri = GetDirectoryURI(ObjectID);
	HTTPResponse response = mrClient.HeadObject(uri);

	if(response.GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		// A file might exist, check that too.
		uri = GetFileURI(ObjectID);
		response = mrClient.HeadObject(uri);
	}

	if(response.GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		return false;
	}

	if(response.GetResponseCode() != HTTPResponse::Code_OK)
	{
		// Throw an appropriate exception.
		mrClient.CheckResponse(response, std::string("Failed to check whether "
			"a file or directory exists at this URL: ") +
			GetObjectURL(uri));
	}

	if(pRevisionID)
	{
		*pRevisionID = GetRevisionID(uri, response);
	}

	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    S3BackupFileSystem::GetObjectURI(int64_t ObjectID,
//			 int Type)
//		Purpose: Builds the object filename for a given object,
//			 including mBasePath. Very similar to
//			 StoreStructure::MakeObjectFilename(), but files and
//			 directories have different extensions, and the
//			 filename is the full object ID, not just the lower
//			 STORE_ID_SEGMENT_LENGTH bits.
//		Created: 2016/03/21
//
// --------------------------------------------------------------------------
std::string S3BackupFileSystem::GetObjectURI(int64_t ObjectID, int Type) const
{
	const static char *hex = "0123456789abcdef";
	ASSERT(mBasePath.size() > 0 && mBasePath[0] == '/' &&
		mBasePath[mBasePath.size() - 1] == '/');
	std::ostringstream out;
	out << mBasePath;

	// Get the id value from the stored object ID into an unsigned int64_t, so that
	// we can do bitwise operations on it safely.
	uint64_t id = (uint64_t)ObjectID;

	// Shift off the bits which make up the leafname
	id >>= STORE_ID_SEGMENT_LENGTH;

	// build pathname
	while(id != 0)
	{
		// assumes that the segments are no bigger than 8 bits
		int v = id & STORE_ID_SEGMENT_MASK;
		out << hex[(v & 0xf0) >> 4];
		out << hex[v & 0xf];
		out << "/";

		// shift the bits we used off the pathname
		id >>= STORE_ID_SEGMENT_LENGTH;
	}

	// append the filename
	out << BOX_FORMAT_OBJECTID(ObjectID);
	if(Type == ObjectExists_File)
	{
		out << ".file";
	}
	else if(Type == ObjectExists_Dir)
	{
		out << ".dir";
	}
	else
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			"Unknown file type for object " << BOX_FORMAT_OBJECTID(ObjectID) <<
			": " << Type);
	}

	return out.str();
}


//! Reads a directory with the specified ID into the supplied BackupStoreDirectory
//! object, also initialising its revision ID and SizeInBlocks.
void S3BackupFileSystem::GetDirectory(int64_t ObjectID, BackupStoreDirectory& rDirOut)
{
	std::string uri = GetDirectoryURI(ObjectID);
	HTTPResponse response = mrClient.GetObject(uri);
	mrClient.CheckResponse(response,
		std::string("Failed to download directory: ") + uri);
	rDirOut.ReadFromStream(response, mrClient.GetNetworkTimeout());

	rDirOut.SetRevisionID(GetRevisionID(uri, response));
	rDirOut.SetUserInfo1_SizeInBlocks(GetSizeInBlocks(response.GetContentLength()));
}


//! Writes the supplied BackupStoreDirectory object to the store, and updates its revision
//! ID and SizeInBlocks.
void S3BackupFileSystem::PutDirectory(BackupStoreDirectory& rDir)
{
	CollectInBufferStream out;
	rDir.WriteToStream(out);
	out.SetForReading();

	std::string uri = GetDirectoryURI(rDir.GetObjectID());
	HTTPResponse response = mrClient.PutObject(uri, out);
	mrClient.CheckResponse(response,
		std::string("Failed to upload directory: ") + uri);

	rDir.SetRevisionID(GetRevisionID(uri, response));
	rDir.SetUserInfo1_SizeInBlocks(GetSizeInBlocks(out.GetSize()));
}


void S3BackupFileSystem::DeleteDirectory(int64_t ObjectID)
{
	std::string uri = GetDirectoryURI(ObjectID);
	HTTPResponse response = mrClient.DeleteObject(uri);
	mrClient.CheckResponse(response,
		std::string("Failed to delete directory: ") + uri,
		true); // ExpectNoContent
}


void S3BackupFileSystem::DeleteFile(int64_t ObjectID)
{
	std::string uri = GetFileURI(ObjectID);
	HTTPResponse response = mrClient.DeleteObject(uri);
	mrClient.CheckResponse(response,
		std::string("Failed to delete file: ") + uri,
		true); // ExpectNoContent
}

void S3BackupFileSystem::DeleteObjectUnknown(int64_t ObjectID)
{
	std::string uri = GetFileURI(ObjectID);
	HTTPResponse response = mrClient.DeleteObject(uri);
	// It might be a directory instead, try that before returning an error.
	if(response.GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		std::string uri = GetDirectoryURI(ObjectID);
		response = mrClient.DeleteObject(uri);
	}
	mrClient.CheckResponse(response,
		std::string("Failed to delete file or directory: ") + uri,
		true); // ExpectNoContent
}



class S3PutFileCompleteTransaction : public BackupFileSystem::Transaction
{
private:
	S3Client& mrClient;
	std::string mFileURI;
	bool mCommitted;
	int64_t mNumBlocks;

public:
	S3PutFileCompleteTransaction(S3BackupFileSystem& fs, S3Client& client,
		const std::string& file_uri, IOStream& file_data);
	~S3PutFileCompleteTransaction();
	virtual void Commit();
	virtual int64_t GetNumBlocks() { return mNumBlocks; }

	// It doesn't matter what we return here, because this should never be called
	// for a PutFileCompleteTransaction (the API is intended for
	// PutFilePatchTransaction instead):
	virtual bool IsNewFileIndependent() { return false; }
};


S3PutFileCompleteTransaction::S3PutFileCompleteTransaction(S3BackupFileSystem& fs,
	S3Client& client, const std::string& file_uri, IOStream& file_data)
: mrClient(client),
  mFileURI(file_uri),
  mCommitted(false),
  mNumBlocks(0)
{
	ByteCountingStream counter(file_data);
	HTTPResponse response = mrClient.PutObject(file_uri, counter);
	if(response.GetResponseCode() != HTTPResponse::Code_OK)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, FileUploadFailed,
			"Failed to upload file to Amazon S3: " <<
			response.ResponseCodeString());
	}
	mNumBlocks = fs.GetSizeInBlocks(counter.GetNumBytesRead());
}


void S3PutFileCompleteTransaction::Commit()
{
	mCommitted = true;
}


S3PutFileCompleteTransaction::~S3PutFileCompleteTransaction()
{
	if(!mCommitted)
	{
		HTTPResponse response = mrClient.DeleteObject(mFileURI);
		mrClient.CheckResponse(response, "Failed to delete uploaded file from Amazon S3",
			true); // ExpectNoContent
	}
}


std::auto_ptr<BackupFileSystem::Transaction>
S3BackupFileSystem::PutFileComplete(int64_t ObjectID, IOStream& rFileData,
	BackupStoreRefCountDatabase::refcount_t refcount)
{
	ASSERT(refcount == 0 || refcount == 1);
	BackupStoreFile::VerifyStream validator(rFileData);
	S3PutFileCompleteTransaction* pTrans = new S3PutFileCompleteTransaction(*this,
		mrClient, GetFileURI(ObjectID), validator);
	return std::auto_ptr<BackupFileSystem::Transaction>(pTrans);
}


//! GetObject() can be for either a file or a directory, so we need to try both.
// TODO FIXME use a cached directory listing to determine which it is
std::auto_ptr<IOStream> S3BackupFileSystem::GetObject(int64_t ObjectID, bool required)
{
	std::string uri = GetFileURI(ObjectID);
	std::auto_ptr<HTTPResponse> ap_response(
		new HTTPResponse(mrClient.GetObject(uri))
	);

	if(ap_response->GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		// It's not a file, try the directory URI instead.
		uri = GetDirectoryURI(ObjectID);
		ap_response.reset(new HTTPResponse(mrClient.GetObject(uri)));
	}

	if(ap_response->GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		if(required)
		{
			THROW_EXCEPTION_MESSAGE(BackupStoreException, ObjectDoesNotExist,
				"Requested object " << BOX_FORMAT_OBJECTID(ObjectID) << " "
				"does not exist, or is not a file or a directory");
		}
		else
		{
			return std::auto_ptr<IOStream>();
		}
	}

	mrClient.CheckResponse(*ap_response,
		std::string("Failed to download requested file: " + uri));
	return static_cast<std::auto_ptr<IOStream> >(ap_response);
}


std::auto_ptr<IOStream> S3BackupFileSystem::GetFile(int64_t ObjectID)
{
	std::string uri = GetFileURI(ObjectID);
	std::auto_ptr<HTTPResponse> ap_response(
		new HTTPResponse(mrClient.GetObject(uri))
	);

	if(ap_response->GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, ObjectDoesNotExist,
			"Requested object " << BOX_FORMAT_OBJECTID(ObjectID) << " "
			"does not exist, or is not a file.");
	}

	mrClient.CheckResponse(*ap_response,
		std::string("Failed to download requested file: " + uri));
	return static_cast<std::auto_ptr<IOStream> >(ap_response);
}


void S3BackupFileSystem::ReportLockMismatches(str_map_diff_t mismatches)
{
	if(!mismatches.empty())
	{
		std::ostringstream error_buf;
		bool first_item = true;
		for(str_map_diff_t::iterator i = mismatches.begin();
			i != mismatches.end(); i++)
		{
			if(!first_item)
			{
				error_buf << ", ";
			}
			first_item = false;
			const std::string& name(i->first);
			const std::string& expected(i->second.first);
			const std::string& actual(i->second.second);

			error_buf << name << " was not '" << expected << "' but '" <<
				actual << "'";
		}
		THROW_EXCEPTION_MESSAGE(BackupStoreException,
			CouldNotLockStoreAccount, "Lock on '" << mLockName <<
			"' was concurrently modified: " << error_buf.str());
	}
}


void S3BackupFileSystem::TryGetLock()
{
	if(mHaveLock)
	{
		return;
	}

	const Configuration s3config = mrConfig.GetSubConfiguration("S3Store");

	if(!mapSimpleDBClient.get())
	{
		mapSimpleDBClient.reset(new SimpleDBClient(s3config));
		// timeout left at the default 300 seconds.
	}

	// Create the domain, to ensure that it exists. This is idempotent.
	mapSimpleDBClient->CreateDomain(mSimpleDBDomain);
	SimpleDBClient::str_map_t conditional;

	// Check to see whether someone already holds the lock
	try
	{
		SimpleDBClient::str_map_t attributes;
		{
			HideSpecificExceptionGuard hex(HTTPException::ExceptionType,
				HTTPException::SimpleDBItemNotFound);
			attributes = mapSimpleDBClient->GetAttributes(mSimpleDBDomain,
				mLockName);
		}

		// This succeeded, which means that someone once held the lock. If the
		// locked attribute is empty, then they released it cleanly, and we can
		// access the account safely.
		box_time_t since_time = box_strtoui64(attributes["since"].c_str(), NULL, 10);

		if(attributes["locked"] == "")
		{
			// The account was locked, but no longer. Make sure it stays that
			// way, to avoid a race condition.
			conditional = attributes;
		}
		// Otherwise, someone holds the lock right now. If the lock is held by
		// this computer (same hostname) and the PID is no longer running, then
		// it's reasonable to assume that we can override it because the original
		// process is dead.
		else if(attributes["hostname"] == mCurrentHostName)
		{
			char* end_ptr;
			int locking_pid = strtol(attributes["pid"].c_str(), &end_ptr, 10);
			if(*end_ptr != 0)
			{
				THROW_EXCEPTION_MESSAGE(BackupStoreException,
					CouldNotLockStoreAccount, "Failed to parse PID "
					"from existing lock: " << attributes["pid"]);
			}

			if(process_is_running(locking_pid))
			{
				THROW_EXCEPTION_MESSAGE(BackupStoreException,
					CouldNotLockStoreAccount, "Lock on '" <<
					mLockName << "' is held by '" <<
					attributes["locker"] << "' (process " <<
					locking_pid << " on this host, " <<
					mCurrentHostName << ", which is still running), "
					"since " << FormatTime(since_time,
						true)); // includeDate
			}
			else
			{
				BOX_WARNING(
					"Lock on '" << mLockName << "' was held by '" <<
					attributes["locker"] << "' (process " <<
					locking_pid << " on this host, " <<
					mCurrentHostName << ", which appears to have ended) "
					"since " << FormatTime(since_time,
						true) // includeDate
					<< ", overriding it");
				conditional = attributes;
			}
		}
		else
		{
			// If the account is locked by a process on a different host, then
			// we have no way to check whether it is still running, so we can
			// only give up.
			THROW_EXCEPTION_MESSAGE(BackupStoreException,
				CouldNotLockStoreAccount, "Lock on '" << mLockName <<
				"' is held by '" << attributes["locker"] << " since " <<
				FormatTime(since_time, true)); // includeDate
		}
	}
	catch(HTTPException &e)
	{
		if(EXCEPTION_IS_TYPE(e, HTTPException, SimpleDBItemNotFound))
		{
			// The lock doesn't exist, so it's safe to create it. We can't
			// make this request conditional, so there is a race condition
			// here! We deal with that by reading back the attributes with
			// a ConsistentRead after writing them.
		}
		else
		{
			// Something else went wrong.
			throw;
		}
	}

	mLockAttributes["locked"] = "true";
	mLockAttributes["locker"] = mLockValue;
	mLockAttributes["hostname"] = mCurrentHostName;
	{
		std::ostringstream pid_buf;
		pid_buf << getpid();
		mLockAttributes["pid"] = pid_buf.str();
	}
	{
		std::ostringstream since_buf;
		since_buf << GetCurrentBoxTime();
		mLockAttributes["since"] = since_buf.str();
	}

	// This will throw an exception if the conditional PUT fails:
	mapSimpleDBClient->PutAttributes(mSimpleDBDomain, mLockName, mLockAttributes,
		conditional);

	// To avoid the race condition, read back the attribute values with a consistent
	// read, to check that nobody else sneaked in at the same time:
	SimpleDBClient::str_map_t attributes_read = mapSimpleDBClient->GetAttributes(
		mSimpleDBDomain, mLockName, true); // consistent_read

	str_map_diff_t mismatches = compare_str_maps(mLockAttributes, attributes_read);

	// This should throw an exception if there are any mismatches:
	ReportLockMismatches(mismatches);
	ASSERT(mismatches.empty());

	// Now we have the lock!
	mHaveLock = true;
}


void S3BackupFileSystem::ReleaseLock()
{
	BackupFileSystem::ReleaseLock();

	// It's possible that neither the temporary nor the permanent refcount DB was
	// requested while we had the write lock, so the cache lock may not have been
	// acquired.
	if(mCacheLock.GotLock())
	{
		mCacheLock.ReleaseLock();
	}

	// Releasing is so much easier!
	if(!mHaveLock)
	{
		return;
	}

	// If we have a lock, we should also have the SimpleDBClient that we used to
	// acquire it!
	ASSERT(mapSimpleDBClient.get());

	// Read the current values, and check that they match what we expected, i.e. that
	// nobody stole the lock from under us
	SimpleDBClient::str_map_t attributes_read = mapSimpleDBClient->GetAttributes(
		mSimpleDBDomain, mLockName, true); // consistent_read
	str_map_diff_t mismatches = compare_str_maps(mLockAttributes, attributes_read);

	// This should throw an exception if there are any mismatches:
	ReportLockMismatches(mismatches);
	ASSERT(mismatches.empty());

	// Now write the same values back, except with "locked" = ""
	mLockAttributes["locked"] = "";

	// Conditional PUT, using the values that we just read, to ensure that nobody
	// changes it under our feet right now. This will throw an exception if the
	// conditional PUT fails:
	mapSimpleDBClient->PutAttributes(mSimpleDBDomain, mLockName, mLockAttributes,
		attributes_read);

	// Read back, to check that we unlocked successfully:
	attributes_read = mapSimpleDBClient->GetAttributes(mSimpleDBDomain, mLockName,
		true); // consistent_read
	mismatches = compare_str_maps(mLockAttributes, attributes_read);

	// This should throw an exception if there are any mismatches:
	ReportLockMismatches(mismatches);
	ASSERT(mismatches.empty());

	// Now we no longer have the lock!
	mHaveLock = false;
}


S3BackupFileSystem::~S3BackupFileSystem()
{
	// Close any open refcount DBs before partially destroying the
	// BackupFileSystem that they need to close down. Need to do this in
	// the subclass to avoid calling SaveRefCountDatabase() when the
	// subclass has already been partially destroyed.
	// http://stackoverflow.com/questions/10707286/how-to-resolve-pure-virtual-method-called
	mapPotentialRefCountDatabase.reset();
	mapPermanentRefCountDatabase.reset();

	// This needs to be in the source file, not inline, as long as we don't include
	// the whole of SimpleDBClient.h in BackupFileSystem.h.
	if(mHaveLock)
	{
		try
		{
			ReleaseLock();
		}
		catch(BoxException& e)
		{
			// Destructors aren't supposed to throw exceptions, so it's too late
			// for us to do much except log a warning
			BOX_WARNING("Failed to release a lock while destroying "
				"S3BackupFileSystem: " << e.what());
		}
	}
}


// TODO FIXME check if file is in cache and return size from there
int64_t S3BackupFileSystem::GetFileSizeInBlocks(int64_t ObjectID)
{
	std::string uri = GetFileURI(ObjectID);
	HTTPResponse response = mrClient.HeadObject(uri);
	return GetSizeInBlocks(response.GetContentLength());
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    S3BackupFileSystem::CheckObjects(bool fix_errors)
//		Purpose: Scan directories on store recursively, counting
//			 files and identifying ones that should not exist,
//			 identify the highest object ID that exists, and
//			 check every object up to that ID.
//		Created: 2016/03/21
//
// --------------------------------------------------------------------------


BackupFileSystem::CheckObjectsResult
S3BackupFileSystem::CheckObjects(bool fix_errors)
{
	// Find the maximum start ID of directories -- worked out by looking at list of
	// objects on store, not trusting anything.
	CheckObjectsResult result;
	start_id_to_files_t start_id_to_files;
	CheckObjectsScanDir(0, 1, "", result, fix_errors, start_id_to_files);

	// Then go through and scan all the objects within those directories
	for(int64_t StartID = 0; StartID <= result.maxObjectIDFound;
		StartID += (1<<STORE_ID_SEGMENT_LENGTH))
	{
		CheckObjectsDir(StartID, result, fix_errors, start_id_to_files);
	}

	return result;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    S3BackupFileSystem::CheckObjectsScanDir(
//			 int64_t start_id, int level,
//			 const std::string &dir_name,
//			 BackupFileSystem::CheckObjectsResult& result,
//			 bool fix_errors)
//		Purpose: Read in the contents of the directory, recurse to
//			 lower levels, return the maximum starting ID of any
//			 directory found. Internal method.
//		Created: 2016/03/21
//
// --------------------------------------------------------------------------

void S3BackupFileSystem::CheckObjectsScanDir(int64_t start_id, int level,
	const std::string &dir_name, BackupFileSystem::CheckObjectsResult& result,
	bool fix_errors, start_id_to_files_t& start_id_to_files)
{
	//TRACE2("Scan directory for max dir starting ID %s, StartID %lld\n", rDirName.c_str(), StartID);

	if(result.maxObjectIDFound < start_id)
	{
		result.maxObjectIDFound = start_id;
	}

	std::vector<S3Client::BucketEntry> files;
	std::vector<std::string> dirs;
	bool is_truncated;
	int max_keys = 1000;
	start_id_to_files[start_id] = std::vector<std::string>();

	// Remove the initial / from BasePath. It must also end with /, and dir_name
	// must not start with / but must end with / (or be empty), so that when
	// concatenated they make a valid URI with a trailing slash ("prefix").
	if(!StartsWith("/", mBasePath))
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, BadConfiguration,
			"BasePath must start and end with '/', but was: " << mBasePath);
	}
	std::string prefix = RemovePrefix("/", mBasePath);

	if(!EndsWith("/", mBasePath))
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, BadConfiguration,
			"BasePath must start and end with '/', but was: " << mBasePath);
	}

	if(StartsWith("/", dir_name))
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, BadConfiguration,
			"dir_name must not start with '/': '" << dir_name << "'");
	}

	if(!dir_name.empty() && !EndsWith("/", dir_name))
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, BadConfiguration,
			"dir_name must be empty or end with '/': '" << dir_name << "'");
	}
	prefix += dir_name;
	ASSERT(EndsWith("/", prefix));

	mrClient.ListBucket(&files, &dirs, prefix, "/", // delimiter
		&is_truncated, max_keys);
	if(is_truncated)
	{
		THROW_EXCEPTION_MESSAGE(BackupStoreException, TooManyFilesInDirectory,
			"Failed to check directory: " << prefix << ": too many entries");
	}

	// Cache the list of files in this directory for later use.
	for(std::vector<S3Client::BucketEntry>::const_iterator i = files.begin();
		i != files.end(); i++)
	{
		// This will throw an exception if the filename does not start with
		// the prefix:
		std::string unprefixed_name = RemovePrefix(prefix, i->name());
		start_id_to_files[start_id].push_back(unprefixed_name);
	}

	for(std::vector<std::string>::const_iterator i(dirs.begin());
		i != dirs.end(); ++i)
	{
		std::string subdir_name = RemovePrefix(prefix, *i);
		subdir_name = RemoveSuffix("/", subdir_name);

		// Check to see if it's the right name
		int n = 0;
		if(subdir_name.size() == 2 && TwoDigitHexToInt(subdir_name.c_str(), n)
			&& n < (1<<STORE_ID_SEGMENT_LENGTH))
		{
			// Next level down
			BackupFileSystem::CheckObjectsResult sub_result;

			CheckObjectsScanDir(
				start_id | (n << (level * STORE_ID_SEGMENT_LENGTH)),
				level + 1, dir_name + subdir_name + "/", sub_result,
				fix_errors, start_id_to_files);
			result.numErrorsFound += sub_result.numErrorsFound;

			// Found a greater starting ID?
			if(result.maxObjectIDFound < sub_result.maxObjectIDFound)
			{
				result.maxObjectIDFound = sub_result.maxObjectIDFound;
			}
		}
		else
		{
			// We can't really "delete" directories on S3 because they don't exist
			// as separate objects, just prefixes for files, so we'd have to
			// recursively delete all files within that directory, and I don't feel
			// like writing that code right now.
			BOX_ERROR("Spurious or invalid directory '" << subdir_name <<
				"' found, please remove it");
			++result.numErrorsFound;
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    S3BackupFileSystem::CheckObjectsDir(int64_t start_id,
//			 BackupFileSystem::CheckObjectsResult& result,
//			 bool fix_errors,
//			 const dir_id_to_files_t& dir_id_to_files)
//		Purpose: Check all the files within this directory which has
//			 the given starting ID.
//		Created: 2016/03/21
//
// --------------------------------------------------------------------------
void S3BackupFileSystem::CheckObjectsDir(int64_t start_id,
	BackupFileSystem::CheckObjectsResult& result, bool fix_errors,
	const start_id_to_files_t& start_id_to_files)
{
	// Make directory name -- first generate the filename of an entry that
	// would be in this directory (it doesn't need to actually be there):
	std::string prefix = GetFileURI(start_id);

	// Check that GetFileURI returned what we expected:
	ASSERT(EndsWith(".file", prefix));
	std::string::size_type last_slash = prefix.find_last_of('/');
	ASSERT(last_slash != std::string::npos);

	// Remove the filename from it
	prefix.resize(last_slash);

	// Get directory contents. operator[] is not const, so we can't do this:
	// std::vector<std::string> files = start_id_to_files[start_id];
	std::vector<std::string> files = start_id_to_files.find(start_id)->second;

	// Parse each entry, checking whether it has a valid name for an object
	// file: must begin with '0x', followed by some hex digits and end with
	// .file or .dir. The object ID must belong in this position in the
	// directory hierarchy.  Any other file present is an error, and if
	// fix_errors is true, it will be deleted.
	for(std::vector<std::string>::const_iterator i(files.begin()); i != files.end(); ++i)
	{
		bool fileOK = false;
		if(StartsWith("0x", (*i)))
		{
			std::string object_id_str;
			if(EndsWith(".file", *i))
			{
				object_id_str = RemoveSuffix(".file", *i);
			}
			else if(EndsWith(".dir", *i))
			{
				object_id_str = RemoveSuffix(".dir", *i);
			}
			else
			{
				ASSERT(!fileOK);
			}

			if(!object_id_str.empty())
			{
				const char * p_end;
				int64_t object_id = box_strtoui64(object_id_str.c_str() + 2,
					&p_end, 16);
				if(*p_end != 0)
				{
					ASSERT(!fileOK);
				}
				else if(object_id < start_id ||
					object_id >= start_id + (1<<STORE_ID_SEGMENT_LENGTH))
				{
					ASSERT(!fileOK);
				}
				else
				{
					fileOK = true;
					// Filename is valid, mark as existing.
					if(result.maxObjectIDFound < object_id)
					{
						result.maxObjectIDFound = object_id;
					}
				}
			}
		}
		// No other files should be present in subdirectories
		else if(start_id != 0)
		{
			ASSERT(!fileOK);
		}
		// info and refcount databases are OK in the root directory
		else if(*i == S3_INFO_FILE_NAME ||
			*i == S3_REFCOUNT_FILE_NAME)
		{
			fileOK = true;
		}
		else
		{
			ASSERT(!fileOK);
		}

		if(!fileOK)
		{
			// Unexpected or bad file, delete it
			std::string uri = prefix + "/" + (*i);
			BOX_ERROR("Spurious file " << uri << " found" <<
				(fix_errors?", deleting":""));
			++result.numErrorsFound;
			if(fix_errors)
			{
				mrClient.DeleteObject(uri);
			}
		}
	}
}
