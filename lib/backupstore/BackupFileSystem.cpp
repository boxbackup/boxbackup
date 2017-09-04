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

void BackupFileSystem::GetLock()
{
	for(int triesLeft = 8; triesLeft >= 0; triesLeft--)
	{
		try
		{
			TryGetLock();
		}
		catch(BackupStoreException &e)
		{
			if(EXCEPTION_IS_TYPE(e, BackupStoreException,
				CouldNotLockStoreAccount) && triesLeft)
			{
				// Sleep a bit, and try again, as long as we have retries left.
				ShortSleep(MilliSecondsToBoxTime(1000), true);
			}
			else
			{
				throw;
			}
		}

		// Success!
		break;
	}
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
	int mDiscSet;
	bool mCommitted;

public:
	RaidPutFileCompleteTransaction(int StoreDiscSet, const std::string& filename,
		BackupStoreRefCountDatabase::refcount_t refcount)
	: mStoreFile(StoreDiscSet, filename, refcount),
	  mFileName(filename),
	  mDiscSet(StoreDiscSet),
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

#ifndef NDEBUG
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
#endif // !NDEBUG

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

	RaidPutFileCompleteTransaction* pTrans = new RaidPutFileCompleteTransaction(
		mStoreDiscSet, filename, refcount);
	std::auto_ptr<BackupFileSystem::Transaction> apTrans(pTrans);

	RaidFileWrite& rStoreFile(pTrans->GetRaidFile());
	rStoreFile.Open(false); // no overwriting

	BackupStoreFile::VerifyStream validator(&rStoreFile);

	// A full file, just store to disc
	if(!rFileData.CopyStreamTo(validator, BACKUP_STORE_TIMEOUT))
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
		const std::string& reversedPatchFilename)
	: mNewCompleteFile(StoreDiscSet, newCompleteFilename),
	  mReversedPatchFile(StoreDiscSet, reversedPatchFilename),
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
	IOStream& rPatchData)
{
	// Create the containing directory if it doesn't exist.
	std::string newVersionFilename = GetObjectFileName(ObjectID, true);

	// Filename of the old version
	std::string oldVersionFilename = GetObjectFileName(DiffFromFileID,
		false); // no need to make sure the directory it's in exists

	RaidPutFilePatchTransaction* pTrans = new RaidPutFilePatchTransaction(
		mStoreDiscSet, newVersionFilename, oldVersionFilename);
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
}


int S3BackupFileSystem::GetBlockSize()
{
	return S3_NOTIONAL_BLOCK_SIZE;
}


std::string S3BackupFileSystem::GetAccountIdentifier()
{
	std::ostringstream oss;
	oss << "'" << GetBackupStoreInfo(true).GetAccountName() << "'";
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
	HTTPResponse response = GetObject(info_uri);
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
	CollectInBufferStream out;
	rInfo.Save(out);
	out.SetForReading();

	HTTPResponse response = PutObject(S3_INFO_FILE_NAME, out);

	std::string info_url = GetObjectURL(S3_INFO_FILE_NAME);
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
	return *mapPermanentRefCountDatabase;
}


BackupStoreRefCountDatabase& S3BackupFileSystem::GetPotentialRefCountDatabase()
{
	if(mapPotentialRefCountDatabase.get())
	{
		return *mapPotentialRefCountDatabase;
	}

	// Creating the "official" temporary refcount DB is actually a change
	// to the cache, even if you don't commit it, because it's in the same
	// directory and would conflict with another process trying to do the
	// same thing, so it requires that you hold the write lock.
	GetCacheLock();

	// It's dangerous to have two read-write databases open at the same time (it would
	// be too easy to update the refcounts in the wrong one by mistake), and temporary
	// databases are always read-write, so if a permanent database is already open
	// then it must be a read-only one.
	ASSERT(!mapPermanentRefCountDatabase.get() ||
		mapPermanentRefCountDatabase->IsReadOnly());

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
	HTTPResponse response = GetObject(uri);
	mrClient.CheckResponse(response,
		std::string("Failed to download directory: ") + uri);
	rDirOut.ReadFromStream(response, mrClient.GetNetworkTimeout());

	rDirOut.SetRevisionID(GetRevisionID(uri, response));
	ASSERT(false); // set the size in blocks
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
	HTTPResponse response = PutObject(uri, out);
	mrClient.CheckResponse(response,
		std::string("Failed to upload directory: ") + uri);

	rDir.SetRevisionID(GetRevisionID(uri, response));
	rDir.SetUserInfo1_SizeInBlocks(GetSizeInBlocks(out.GetSize()));
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
