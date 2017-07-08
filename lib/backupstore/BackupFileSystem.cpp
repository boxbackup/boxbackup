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


bool RaidBackupFileSystem::TryGetLock()
{
	// Make the filename of the write lock file
	std::string writeLockFile;
	StoreStructure::MakeWriteLockFilename(mAccountRootDir, mStoreDiscSet,
		writeLockFile);

	// Request the lock
	return mWriteLock.TryAndGetLock(writeLockFile.c_str(),
		0600 /* restrictive file permissions */);
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


std::auto_ptr<BackupStoreInfo> RaidBackupFileSystem::GetBackupStoreInfo(int32_t AccountID,
	bool ReadOnly)
{
	// Generate the filename
	std::string fn(mAccountRootDir + INFO_FILENAME);

	// Open the file for reading (passing on optional request for revision ID)
	std::auto_ptr<RaidFileRead> rf(RaidFileRead::Open(mStoreDiscSet, fn));
	std::auto_ptr<BackupStoreInfo> info = BackupStoreInfo::Load(*rf, fn, ReadOnly);

	// Check it
	if(info->GetAccountID() != AccountID)
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
	rf.Commit(true);
}

std::auto_ptr<BackupStoreRefCountDatabase> RaidBackupFileSystem::GetRefCountDatabase(
	int32_t AccountID, bool ReadOnly)
{
	BackupStoreAccountDatabase::Entry account(AccountID, mStoreDiscSet);
	return BackupStoreRefCountDatabase::Load(account, ReadOnly);
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
	bool mCommitted;

public:
	RaidPutFileCompleteTransaction(int StoreDiscSet, const std::string& filename)
	: mStoreFile(StoreDiscSet, filename),
	  mCommitted(false)
	{ }
	~RaidPutFileCompleteTransaction();
	virtual void Commit();
	virtual int64_t GetNumBlocks() { return mNumBlocks; }
	RaidFileWrite& GetRaidFile() { return mStoreFile; }
	virtual bool IsNewFileIndependent() { return false; }
	int64_t mNumBlocks;
};

void RaidPutFileCompleteTransaction::Commit()
{
	mCommitted = true;
}

RaidPutFileCompleteTransaction::~RaidPutFileCompleteTransaction()
{
	if(mCommitted)
	{
		GetRaidFile().TransformToRaidStorage();
	}
	else
	{
		GetRaidFile().Delete();
	}
}

std::auto_ptr<BackupFileSystem::Transaction>
RaidBackupFileSystem::PutFileComplete(int64_t ObjectID, IOStream& rFileData)
{
	// Create the containing directory if it doesn't exist.
	std::string filename = GetObjectFileName(ObjectID, true);

	RaidPutFileCompleteTransaction* pTrans = new RaidPutFileCompleteTransaction(
		mStoreDiscSet, filename);
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

	// Verify the file -- only necessary for non-diffed versions.
	//
	// Checking the file requires that we commit the RaidFile first, which
	// is unfortunate because we're not quite ready to, and because we thus
	// treat full and differential uploads differently. But it's not a huge
	// issue because we can always delete the RaidFile without any harm done
	// in the full upload case.
	rStoreFile.Commit(false); // Don't ConvertToRaidNow

	std::auto_ptr<RaidFileRead> checkFile(RaidFileRead::Open(mStoreDiscSet,
			filename));
	if(!BackupStoreFile::VerifyEncodedFileFormat(*checkFile))
	{
		THROW_EXCEPTION(BackupStoreException, AddedFileDoesNotVerify)
	}

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
		if(::unlink(tempFn.c_str()) != 0)
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
		::unlink(tempFn.c_str());
		throw;
	}
}

std::auto_ptr<IOStream> RaidBackupFileSystem::GetFile(int64_t ObjectID)
{
	std::string filename = GetObjectFileName(ObjectID,
		false); // no need to make sure the directory it's in exists.
	std::auto_ptr<RaidFileRead> objectFile(RaidFileRead::Open(mStoreDiscSet,
		filename));
	return static_cast<std::auto_ptr<IOStream> >(objectFile);
}

std::auto_ptr<IOStream> RaidBackupFileSystem::GetFilePatch(int64_t ObjectID,
	std::vector<int64_t>& rPatchChain)
{
	// File exists, but is a patch from a new version. Generate the older version.

	// The result

	// OK! The last entry in the chain is the full file, the others are patches back from it.
	// Open the last one, which is the current from file
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
		if (from.get()) from->Close();
		from = combined;
	}

	std::auto_ptr<IOStream> stream(
		BackupStoreFile::ReorderFileToStreamOrder(from.get(),
			true)); // take ownership

	// Release from file to avoid double deletion
	from.release();

	return stream;
}

void RaidBackupFileSystem::DeleteFile(int64_t ObjectID)
{
	std::string filename = GetObjectFileName(ObjectID, false);
	RaidFileWrite deleteFile(mStoreDiscSet, filename);
	deleteFile.Delete();
}

int S3BackupFileSystem::GetBlockSize()
{
	return S3_NOTIONAL_BLOCK_SIZE;
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

std::auto_ptr<BackupStoreInfo> S3BackupFileSystem::GetBackupStoreInfo(int32_t AccountID,
	bool ReadOnly)
{
	std::string info_url = GetObjectURL(S3_INFO_FILE_NAME);
	HTTPResponse response = GetObject(S3_INFO_FILE_NAME);
	mrClient.CheckResponse(response, std::string("No BackupStoreInfo file exists "
		"at this URL: ") + info_url);

	std::auto_ptr<BackupStoreInfo> info =
		BackupStoreInfo::Load(response, info_url, ReadOnly);

	// Check it
	if(info->GetAccountID() != AccountID)
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

