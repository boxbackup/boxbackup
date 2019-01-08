// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientDirectoryRecord.cpp
//		Purpose: Implementation of record about directory for
//			 backup client
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_DIRENT_H
	#include <dirent.h>
#endif

#include <errno.h>
#include <string.h>

#include <algorithm>

#include "autogen_BackupProtocol.h"
#include "autogen_CipherException.h"
#include "autogen_ClientException.h"
#include "Archive.h"
#include "BackupClientContext.h"
#include "BackupClientDirectoryRecord.h"
#include "BackupClientInodeToIDMap.h"
#include "BackupDaemon.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileEncodeStream.h"
#include "BufferedStream.h"
#include "CommonException.h"
#include "CollectInBufferStream.h"
#include "FileModificationTime.h"
#include "IOStream.h"
#include "Logging.h"
#include "MemBlockStream.h"
#include "PathUtils.h"
#include "RateLimitingStream.h"
#include "ReadLoggingStream.h"

#include "MemLeakFindOn.h"

typedef std::map<std::string, BackupStoreDirectory::Entry *> DecryptedEntriesMap_t;

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::BackupClientDirectoryRecord()
//		Purpose: Constructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupClientDirectoryRecord::BackupClientDirectoryRecord(int64_t ObjectID, const std::string &rSubDirName)
	: mObjectID(ObjectID),
	  mSubDirName(rSubDirName),
	  mInitialSyncDone(false),
	  mSyncDone(false),
	  mpPendingEntries(0)
{
	::memset(mStateChecksum, 0, sizeof(mStateChecksum));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::~BackupClientDirectoryRecord()
//		Purpose: Destructor
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
BackupClientDirectoryRecord::~BackupClientDirectoryRecord()
{
	// Make deletion recursive
	DeleteSubDirectories();
	
	// Delete maps
	if(mpPendingEntries != 0)
	{
		delete mpPendingEntries;
		mpPendingEntries = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::DeleteSubDirectories();
//		Purpose: Delete all sub directory entries
//		Created: 2003/10/09
//
// --------------------------------------------------------------------------
void BackupClientDirectoryRecord::DeleteSubDirectories()
{
	// Delete all pointers
	for(std::map<std::string, BackupClientDirectoryRecord *>::iterator i = mSubDirectories.begin();
		i != mSubDirectories.end(); ++i)
	{
		delete i->second;
	}
	
	// Empty list
	mSubDirectories.clear();
}

std::string BackupClientDirectoryRecord::ConvertVssPathToRealPath(
	const std::string &rVssPath,
	const Location& rBackupLocation)
{
#ifdef ENABLE_VSS
	BOX_TRACE("VSS: ConvertVssPathToRealPath: mIsSnapshotCreated = " <<
		rBackupLocation.mIsSnapshotCreated);
	BOX_TRACE("VSS: ConvertVssPathToRealPath: File/Directory Path = " <<
		rVssPath.substr(0, rBackupLocation.mSnapshotPath.length()));
	BOX_TRACE("VSS: ConvertVssPathToRealPath: Snapshot Path = " <<
		rBackupLocation.mSnapshotPath);
	if(rBackupLocation.mIsSnapshotCreated &&
		rVssPath.substr(0, rBackupLocation.mSnapshotPath.length()) ==
		rBackupLocation.mSnapshotPath)
	{
		std::string convertedPath = rBackupLocation.mPath +
			rVssPath.substr(rBackupLocation.mSnapshotPath.length());
		BOX_TRACE("VSS: ConvertVssPathToRealPath: Converted Path = " <<
			convertedPath);
		return convertedPath;
	}
#endif

	return rVssPath;
}

bool BackupClientDirectoryRecord::ListLocalDirectory(const std::string& local_path,
	const std::string& local_path_non_vss, ProgressNotifier& notifier,
	std::vector<struct dirent>& entries_out)
{
	// read the contents...
	DIR *dir_handle = NULL;

	try
	{
		dir_handle = ::opendir(local_path.c_str());
		if(dir_handle == 0)
		{
			// Report the error (logs and eventual email to administrator)
			if(errno == EACCES)
			{
				notifier.NotifyDirListFailed(this, local_path_non_vss,
					"Access denied");
			}
			else
			{
				notifier.NotifyDirListFailed(this, local_path_non_vss,
					strerror(errno));
			}

			// Ignore this directory for now.
			return false;
		}

		struct dirent *en = 0;

		while((en = ::readdir(dir_handle)) != 0)
		{
			entries_out.push_back(*en);
		}

		if(::closedir(dir_handle) != 0)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}

		dir_handle = NULL;
	}
	catch(...)
	{
		if(dir_handle != NULL)
		{
			::closedir(dir_handle);
		}
		throw;
	}

	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::SyncDirectory(i
//			 BackupClientDirectoryRecord::SyncParams &,
//			 int64_t, const std::string &,
//			 const std::string &, bool)
//		Purpose: Recursively synchronise a local directory
//			 with the server.
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
void BackupClientDirectoryRecord::SyncDirectory(
	BackupClientDirectoryRecord::SyncParams &rParams,
	int64_t ContainingDirectoryID,
	const std::string &rLocalPath,
	const std::string &rRemotePath,
	const Location& rBackupLocation,
	bool ThisDirHasJustBeenCreated)
{
	BackupClientContext& rContext(rParams.mrContext);
	ProgressNotifier& rNotifier(rContext.GetProgressNotifier());

	// Signal received by daemon?
	if(rParams.mrRunStatusProvider.StopRun())
	{
		// Yes. Stop now.
		THROW_EXCEPTION(BackupStoreException, SignalReceived)
	}

	std::string local_path_non_vss = ConvertVssPathToRealPath(rLocalPath,
			rBackupLocation);

	// Start by making some flag changes, marking this sync as not done,
	// and on the immediate sub directories.
	mSyncDone = false;
	for(std::map<std::string, BackupClientDirectoryRecord *>::iterator
		i  = mSubDirectories.begin();
		i != mSubDirectories.end(); ++i)
	{
		i->second->mSyncDone = false;
	}

	// Work out the time in the future after which the file should
	// be uploaded regardless. This is a simple way to avoid having
	// too many problems with file servers when they have clients
	// with badly out of sync clocks.
	rParams.mUploadAfterThisTimeInTheFuture = GetCurrentBoxTime() +
		rParams.mMaxFileTimeInFuture;
	
	// Build the current state checksum to compare against while
	// getting info from dirs. Note checksum is used locally only,
	// so byte order isn't considered.
	MD5Digest currentStateChecksum;
	
	EMU_STRUCT_STAT dest_st;
	// Stat the directory, to get attribute info
	// If it's a symbolic link, we want the link target here
	// (as we're about to back up the contents of the directory)
	{
		if(EMU_STAT(rLocalPath.c_str(), &dest_st) != 0)
		{
			// The directory has probably been deleted, so
			// just ignore this error. In a future scan, this
			// deletion will be noticed, deleted from server,
			// and this object deleted.
			rNotifier.NotifyDirStatFailed(this, local_path_non_vss,
				strerror(errno));
			return;
		}

		BOX_TRACE("Stat dir '" << rLocalPath << "' "
			"found device/inode " <<
			dest_st.st_dev << "/" << dest_st.st_ino);

		// Store inode number in map so directories are tracked, in case they're renamed:
		{
			BackupClientInodeToIDMap &idMap(rParams.mrContext.GetNewIDMap());
			idMap.AddToMap(dest_st.st_ino, mObjectID, ContainingDirectoryID,
				local_path_non_vss);
		}
		// Add attributes to checksum
		currentStateChecksum.Add(&dest_st.st_mode,
			sizeof(dest_st.st_mode));
		currentStateChecksum.Add(&dest_st.st_uid,
			sizeof(dest_st.st_uid));
		currentStateChecksum.Add(&dest_st.st_gid,
			sizeof(dest_st.st_gid));
		// Inode to be paranoid about things moving around
		currentStateChecksum.Add(&dest_st.st_ino,
			sizeof(dest_st.st_ino));
#ifdef HAVE_STRUCT_STAT_ST_FLAGS
		currentStateChecksum.Add(&dest_st.st_flags,
			sizeof(dest_st.st_flags));
#endif

		StreamableMemBlock xattr;
		BackupClientFileAttributes::FillExtendedAttr(xattr,
			rLocalPath.c_str());
		currentStateChecksum.Add(xattr.GetBuffer(), xattr.GetSize());
	}
	
	// Read directory entries, building arrays of names
	// First, need to read the contents of the directory.
	std::vector<std::string> dirs;
	std::vector<std::string> files;
	bool downloadDirectoryRecordBecauseOfFutureFiles = false;

	// BLOCK
	{
		// read the contents...
		rNotifier.NotifyScanDirectory(this, local_path_non_vss);
		std::vector<struct dirent> entries;

		if(!ListLocalDirectory(rLocalPath, local_path_non_vss, rNotifier, entries))
		{
			SetErrorWhenReadingFilesystemObject(rParams, local_path_non_vss);

			// Ignore this directory for now.
			return;
		}

		int num_entries_found = 0;
		for(struct dirent& entry : entries)
		{
			num_entries_found++;
			rParams.mrContext.DoKeepAlive();
			if(rParams.mpBackgroundTask)
			{
				rParams.mpBackgroundTask->RunBackgroundTask(
					BackgroundTask::Scanning_Dirs,
					num_entries_found, 0);
			}

			if(!SyncDirectoryEntry(rParams, rNotifier,
				rBackupLocation, rLocalPath,
				currentStateChecksum, &entry, dest_st, dirs,
				files, downloadDirectoryRecordBecauseOfFutureFiles))
			{
				// This entry is not to be backed up.
				continue;
			}
		}
	}

	// Finish off the checksum, and compare with the one currently stored
	currentStateChecksum.Finish();
	bool checksumDifferent = ChecksumIsDifferent(currentStateChecksum);

	// Pointer to potentially downloaded store directory info
	std::auto_ptr<BackupStoreDirectory> apDirOnStore;
	
	try
	{
		// Want to get the directory listing?
		bool download_dir = false;

		if(ThisDirHasJustBeenCreated)
		{
			// Avoid sending another command to the server when we know it's empty
			apDirOnStore.reset(new BackupStoreDirectory(mObjectID,
				ContainingDirectoryID));
			BOX_TRACE("Not downloading directory listing of " << local_path_non_vss <<
				" (" << BOX_FORMAT_OBJECTID(mObjectID) << ") because it has just "
				"been created");
			ASSERT(!download_dir);
		}
		// Consider asking the store for it
		else if(!mInitialSyncDone)
		{
			BOX_TRACE("Downloading directory listing of " << local_path_non_vss <<
				" (" << BOX_FORMAT_OBJECTID(mObjectID) << ") because we haven't "
				"done an initial sync yet");
			download_dir = true;
		}
		else if(checksumDifferent)
		{
			BOX_TRACE("Downloading directory listing of " << local_path_non_vss <<
				" (" << BOX_FORMAT_OBJECTID(mObjectID) << ") because its contents "
				"have changed locally");
			download_dir = true;
		}
		else if(downloadDirectoryRecordBecauseOfFutureFiles)
		{
			BOX_TRACE("Downloading directory listing of " << local_path_non_vss <<
				" (" << BOX_FORMAT_OBJECTID(mObjectID) << ") because it contains "
				"files with dates in the future");
			download_dir = true;
		}
		else
		{
			// There is a small race condition where changes to files between two runs
			// in the same second might not be detected. This occurs sometimes in tests,
			// but is not a realistic scenario, so we don't need to defend against it.
			BOX_TRACE("Not downloading directory listing of " << local_path_non_vss <<
				" (" << BOX_FORMAT_OBJECTID(mObjectID) << ") because our cached "
				"copy appears to still be valid");
			ASSERT(!download_dir);
		}

		if(download_dir)
		{
			apDirOnStore = FetchDirectoryListing(rParams);
		}

		// Make sure the attributes are up to date -- if there's space
		// on the server and this directory has not just been created
		// (because it's attributes will be correct in this case) and
		// the checksum is different, implying they *MIGHT* be
		// different.
		if((!ThisDirHasJustBeenCreated) && checksumDifferent &&
			!rParams.mrContext.StorageLimitExceeded())
		{
			UpdateAttributes(rParams, apDirOnStore.get(), rLocalPath);
		}
		
		// Create the list of pointers to directory entries
		std::vector<BackupStoreDirectory::Entry *> entriesLeftOver;
		if(apDirOnStore.get())
		{
			entriesLeftOver.resize(apDirOnStore->GetNumberOfEntries(), 0);
			BackupStoreDirectory::Iterator i(*apDirOnStore);
			// Copy in pointers to all the entries
			for(unsigned int l = 0; l < apDirOnStore->GetNumberOfEntries(); ++l)
			{
				entriesLeftOver[l] = i.Next();
			}
		}
		
		// Do the directory reading
		bool updateCompleteSuccess = UpdateItems(rParams, rLocalPath,
			rRemotePath, rBackupLocation, apDirOnStore.get(),
			entriesLeftOver, files, dirs);
		
		// LAST THING! (think exception safety)
		// Store the new checksum -- don't fetch things unnecessarily
		// in the future But... only if 1) the storage limit isn't
		// exceeded -- make sure things are done again if the directory
		// is modified later and 2) All the objects within the
		// directory were stored successfully.
		if(!rParams.mrContext.StorageLimitExceeded() &&
			updateCompleteSuccess)
		{
			currentStateChecksum.CopyDigestTo(mStateChecksum);
		}
	}
	catch(...)
	{
		// Bad things have happened -- clean up
		// Set things so that we get a full go at stuff later
		::memset(mStateChecksum, 0, sizeof(mStateChecksum));
		
		throw;
	}
	
	// Flag things as having happened.
	mInitialSyncDone = true;
	mSyncDone = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::SyncDirectorEntry(
//			 BackupClientDirectoryRecord::SyncParams &,
//			 int64_t, const std::string &,
//			 const std::string &, bool)
//		Purpose: Recursively synchronise a local directory
//			 with the server.
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
bool BackupClientDirectoryRecord::SyncDirectoryEntry(
	BackupClientDirectoryRecord::SyncParams &rParams,
	ProgressNotifier& rNotifier,
	const Location& rBackupLocation,
	const std::string &rDirLocalPath,
	MD5Digest& currentStateChecksum,
	struct dirent *en,
	EMU_STRUCT_STAT dir_st,
	std::vector<std::string>& rDirs,
	std::vector<std::string>& rFiles,
	bool& rDownloadDirectoryRecordBecauseOfFutureFiles)
{
	std::string entry_name = en->d_name;
	if(entry_name == "." || entry_name == "..")
	{
		// ignore parent directory entries
		return false;
	}

	// Stat file to get info
	std::string filename = MakeFullPath(rDirLocalPath, entry_name);
	std::string realFileName = ConvertVssPathToRealPath(filename,
		rBackupLocation);
	EMU_STRUCT_STAT file_st;

#ifdef WIN32
	// Don't stat the file just yet, to ensure that users can exclude
	// unreadable files to suppress warnings that they are not accessible.

	int type;
	if(en->d_type == DT_DIR)
	{
		type = S_IFDIR;
	}
	else
	{
		type = S_IFREG;
	}
#else // !WIN32
	if(EMU_LSTAT(filename.c_str(), &file_st) != 0)
	{
		// We don't know whether it's a file or a directory, so check
		// both. This only affects whether a warning message is
		// displayed; the file is not backed up in either case.
		if(!(rParams.mrContext.ExcludeFile(filename)) &&
			!(rParams.mrContext.ExcludeDir(filename)))
		{
			// Report the error (logs and eventual email to
			// administrator)
			rNotifier.NotifyFileStatFailed(this, filename,
				strerror(errno));

			// FIXME move to NotifyFileStatFailed()
			SetErrorWhenReadingFilesystemObject(rParams, filename);
		}

		// Ignore this entry for now.
		return false;
	}

	BOX_TRACE("Stat entry '" << filename << "' found device/inode " <<
		file_st.st_dev << "/" << file_st.st_ino);

	// Workaround for apparent btrfs bug, where symlinks appear to be on
	// a different filesystem than their containing directory, thanks to
	// Toke Hoiland-Jorgensen.

	int type = file_st.st_mode & S_IFMT;
	if(type == S_IFDIR && file_st.st_dev != dir_st.st_dev)
	{
		if(!(rParams.mrContext.ExcludeDir(filename)))
		{
			rNotifier.NotifyMountPointSkipped(this, filename);
		}
		return false;
	}
#endif

	if(type == S_IFREG || type == S_IFLNK)
	{
		// File or symbolic link

		// Exclude it?
		if(rParams.mrContext.ExcludeFile(realFileName))
		{
			rNotifier.NotifyFileExcluded(this, realFileName);
			// Next item!
			return false;
		}
	}
	else if(type == S_IFDIR)
	{
		// Directory

		// Exclude it?
		if(rParams.mrContext.ExcludeDir(realFileName))
		{
			rNotifier.NotifyDirExcluded(this, realFileName);

			// Next item!
			return false;
		}

#ifdef WIN32
		// exclude reparse points, as Application Data points to the
		// parent directory under Vista and later, and causes an
		// infinite loop:
		// http://social.msdn.microsoft.com/forums/en-US/windowscompatibility/thread/05d14368-25dd-41c8-bdba-5590bf762a68/
		if(en->win_attrs & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			rNotifier.NotifyMountPointSkipped(this, realFileName);
			return false;
		}
#endif // WIN32
	}
	else // not a file or directory, what is it?
	{
		if(type == S_IFSOCK
#ifndef WIN32
			|| type == S_IFIFO
#endif // !WIN32
			)
		{
			// removed notification for these types
			// see Debian bug 479145, no objections
		}
		else if(rParams.mrContext.ExcludeFile(realFileName))
		{
			rNotifier.NotifyFileExcluded(this, realFileName);
		}
		else
		{
			rNotifier.NotifyUnsupportedFileType(this, realFileName);
			SetErrorWhenReadingFilesystemObject(rParams,
				realFileName);
		}

		return false;
	}

	// The object should be backed up (file, symlink or dir, not excluded).
	// So make the information for adding to the checksum.

	#ifdef WIN32
	// We didn't stat the file before, but now we need the information.
	if(emu_stat(filename.c_str(), &file_st) != 0)
	{
		rNotifier.NotifyFileStatFailed(this,
			ConvertVssPathToRealPath(filename, rBackupLocation),
			strerror(errno));

		// Report the error (logs and eventual email to administrator)
		SetErrorWhenReadingFilesystemObject(rParams, filename);

		// Ignore this entry for now.
		return false;
	}

	if(file_st.st_dev != dir_st.st_dev)
	{
		rNotifier.NotifyMountPointSkipped(this,
			ConvertVssPathToRealPath(filename, rBackupLocation));
		return false;
	}
	#endif

	// Basic structure for checksum info
	struct {
		box_time_t mModificationTime;
		box_time_t mAttributeModificationTime;
		int64_t mSize;
		// And then the name follows
	} checksum_info;

	// Be paranoid about structure packing
	::memset(&checksum_info, 0, sizeof(checksum_info));

	checksum_info.mModificationTime = FileModificationTime(file_st);
	checksum_info.mAttributeModificationTime = FileAttrModificationTime(file_st);
	checksum_info.mSize = file_st.st_size;
	currentStateChecksum.Add(&checksum_info, sizeof(checksum_info));
	currentStateChecksum.Add(en->d_name, strlen(en->d_name));
	
	// If the file has been modified madly into the future, download the
	// directory record anyway to ensure that it doesn't get uploaded
	// every single time the disc is scanned.
	if(checksum_info.mModificationTime > rParams.mUploadAfterThisTimeInTheFuture)
	{
		rDownloadDirectoryRecordBecauseOfFutureFiles = true;
		// Log that this has happened
		if(!rParams.mHaveLoggedWarningAboutFutureFileTimes)
		{
			rNotifier.NotifyFileModifiedInFuture(this,
				ConvertVssPathToRealPath(filename, rBackupLocation));
			rParams.mHaveLoggedWarningAboutFutureFileTimes = true;
		}
	}

	// We've decided to back it up, so add to file or directory list.
	if(type == S_IFREG || type == S_IFLNK)
	{
		rFiles.push_back(entry_name);
	}
	else if(type == S_IFDIR)
	{
		rDirs.push_back(entry_name);
	}

	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::FetchDirectoryListing(
//			 BackupClientDirectoryRecord::SyncParams &)
//		Purpose: Fetch the directory listing of this directory from
//			 the store.
//		Created: 2003/10/09
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreDirectory>
BackupClientDirectoryRecord::FetchDirectoryListing(
	BackupClientDirectoryRecord::SyncParams &rParams)
{
	std::auto_ptr<BackupStoreDirectory> apDir;
	
	// Get connection to store
	BackupProtocolCallable &connection(rParams.mrContext.GetConnection());

	// Query the directory
	std::auto_ptr<BackupProtocolSuccess> dirreply(connection.QueryListDirectory(
			mObjectID,
			// both files and directories
			BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
			// exclude old/deleted stuff
			BackupProtocolListDirectory::Flags_Deleted |
			BackupProtocolListDirectory::Flags_OldVersion,
			true /* want attributes */));

	// Retrieve the directory from the stream following
	apDir.reset(new BackupStoreDirectory(connection.ReceiveStream(),
		connection.GetTimeout()));
	return apDir;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::UpdateAttributes(
//			 BackupClientDirectoryRecord::SyncParams &,
//			 const std::string &)
//		Purpose: Sets the attributes of the directory on the store,
//			 if necessary.
//		Created: 2003/10/09
//
// --------------------------------------------------------------------------
void BackupClientDirectoryRecord::UpdateAttributes(
	BackupClientDirectoryRecord::SyncParams &rParams,
	BackupStoreDirectory *pDirOnStore,
	const std::string &rLocalPath)
{
	// Get attributes for the directory
	BackupClientFileAttributes attr;
	box_time_t attrModTime = 0;
	attr.ReadAttributes(rLocalPath.c_str(), true /* directories have zero mod times */,
		0 /* no modification time */, &attrModTime);

	// Assume attributes need updating, unless proved otherwise
	bool updateAttr = true;

	// Got a listing to compare with?
	ASSERT(pDirOnStore == 0 || (pDirOnStore != 0 && pDirOnStore->HasAttributes()));
	if(pDirOnStore != 0 && pDirOnStore->HasAttributes())
	{
		const StreamableMemBlock &storeAttrEnc(pDirOnStore->GetAttributes());
		// Explict decryption
		BackupClientFileAttributes storeAttr(storeAttrEnc);
		
		// Compare the attributes
		if(attr.Compare(storeAttr, true,
			true /* ignore both modification times */))
		{
			// No update necessary
			updateAttr = false;
		}
	}

	// Update them?
	if(updateAttr)
	{
		// Get connection to store
		BackupProtocolCallable &connection(rParams.mrContext.GetConnection());

		// Exception thrown if this doesn't work
		std::auto_ptr<IOStream> attrStream(new MemBlockStream(attr));
		connection.QueryChangeDirAttributes(mObjectID, attrModTime, attrStream);
	}
}

std::string BackupClientDirectoryRecord::DecryptFilename(
	BackupStoreDirectory::Entry *en,
	const std::string& rRemoteDirectoryPath)
{
	BackupStoreFilenameClear fn(en->GetName());
	return DecryptFilename(fn, en->GetObjectID(), rRemoteDirectoryPath);
}

std::string BackupClientDirectoryRecord::DecryptFilename(
	BackupStoreFilenameClear fn, int64_t filenameObjectID,
	const std::string& rRemoteDirectoryPath)
{
	std::string filenameClear;
	try
	{
		filenameClear = fn.GetClearFilename();
	}
	catch(BoxException &e)
	{
		BOX_ERROR("Failed to decrypt filename for object " <<
			BOX_FORMAT_OBJECTID(filenameObjectID) << " in "
			"directory " << BOX_FORMAT_OBJECTID(mObjectID) <<
			" (" << rRemoteDirectoryPath << ")");
		throw;
	}
	return filenameClear;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::UpdateItems(BackupClientDirectoryRecord::SyncParams &, const std::string &, BackupStoreDirectory *, std::vector<BackupStoreDirectory::Entry *> &)
//		Purpose: Update the items stored on the server. The rFiles vector will be erased after it's used to save space.
//				 Returns true if all items were updated successfully. (If not, the failures will have been logged).
//		Created: 2003/10/09
//
// --------------------------------------------------------------------------
bool BackupClientDirectoryRecord::UpdateItems(
	BackupClientDirectoryRecord::SyncParams &rParams,
	const std::string &rLocalPath,
	const std::string &rRemotePath,
	const Location& rBackupLocation,
	BackupStoreDirectory *pDirOnStore,
	std::vector<BackupStoreDirectory::Entry *> &rEntriesLeftOver,
	std::vector<std::string> &rFiles,
	const std::vector<std::string> &rDirs)
{
	BackupClientContext& rContext(rParams.mrContext);
	ProgressNotifier& rNotifier(rContext.GetProgressNotifier());

	bool allUpdatedSuccessfully = true;

	// Decrypt all the directory entries.
	// It would be nice to be able to just compare the encrypted versions, however this doesn't work
	// in practise because there can be multiple encodings of the same filename using different
	// methods (although each method will result in the same string for the same filename.) This
	// happens when the server fixes a broken store, and gives plain text generated filenames.
	// So if we didn't do things like this, then you wouldn't be able to recover from bad things
	// happening with the server.
	DecryptedEntriesMap_t decryptedEntries;
	if(pDirOnStore != NULL)
	{
		BackupStoreDirectory::Iterator i(*pDirOnStore);
		BackupStoreDirectory::Entry *en = NULL;
		while((en = i.Next()) != NULL)
		{
			std::string filenameClear;
			try
			{
				filenameClear = DecryptFilename(en, rRemotePath);
				decryptedEntries[filenameClear] = en;
			}
			catch (CipherException &e)
			{
				BOX_ERROR("Failed to decrypt a filename, "
					"pretending that the file doesn't "
					"exist");
			}
		}
	}

	// Do files
	for(std::vector<std::string>::const_iterator f = rFiles.begin();
		f != rFiles.end(); ++f)
	{
		// Send keep-alive message if needed
		rContext.DoKeepAlive();
		
		// Filename of this file
		std::string filename(MakeFullPath(rLocalPath, *f));
		std::string nonVssFilePath = ConvertVssPathToRealPath(filename,
			rBackupLocation);

		// Get relevant info about file
		box_time_t modTime = 0;
		uint64_t attributesHash = 0;
		int64_t fileSize = 0;
		InodeRefType inodeNum = 0;
		// BLOCK
		{
			// Stat the file
			EMU_STRUCT_STAT st;
			if(EMU_LSTAT(filename.c_str(), &st) != 0)
			{
				rNotifier.NotifyFileStatFailed(this, nonVssFilePath, strerror(errno));

				// Report the error (logs and
				// eventual email to administrator)
				SetErrorWhenReadingFilesystemObject(rParams, nonVssFilePath);

				// Ignore this entry for now.
				continue;
			}
			
			// Extract required data
			modTime = FileModificationTime(st);
			fileSize = st.st_size;
			inodeNum = st.st_ino;
			attributesHash = BackupClientFileAttributes::GenerateAttributeHash(st, filename, *f);
		}

		// See if it's in the listing (if we have one)
		BackupStoreFilenameClear storeFilename(*f);
		BackupStoreDirectory::Entry *en = NULL;
		int64_t latestObjectID = 0;
		if(pDirOnStore != NULL)
		{
			DecryptedEntriesMap_t::iterator i(decryptedEntries.find(*f));
			if(i != decryptedEntries.end())
			{
				en = i->second;
				latestObjectID = en->GetObjectID();
			}
		}

		// Check that the entry which might have been found is in fact a file
		if((en != NULL) && !(en->IsFile()))
		{
			// Directory exists in the place of this file -- sort it out
			RemoveDirectoryInPlaceOfFile(rParams, pDirOnStore, en, *f);
			en = NULL;
			latestObjectID = 0;
		}
		
		// Check for renaming?
		bool renamed = false;
		if(pDirOnStore != 0 && en == NULL)
		{
			// We now know...
			// 1) File has just been added
			// 2) It's not in the store
			ASSERT(latestObjectID == 0);
			en = CheckForRename(rContext, pDirOnStore, storeFilename, inodeNum, nonVssFilePath);
			if(en != NULL)
			{
				latestObjectID = en->GetObjectID();
				renamed = true;
			}
		}

		// Is it in the mPendingEntries list?
		box_time_t pendingFirstSeenTime = 0;		// ie not seen
		if(mpPendingEntries != NULL)
		{
			std::map<std::string, box_time_t>::const_iterator i(mpPendingEntries->find(*f));
			if(i != mpPendingEntries->end())
			{
				// found it -- set flag
				pendingFirstSeenTime = i->second;
			}
		}
		
		// If pDirOnStore == 0, then this must have been after an initial sync:
		ASSERT(pDirOnStore != NULL || mInitialSyncDone);

		// So, if pDirOnStore == 0, then we know that everything before syncPeriodStart
		// is either on the server, or in the toupload list. If the directory had changed,
		// we'd have got a directory listing.
		//
		// At this point, if (pDirOnStore == 0 && en == 0), we can assume it's on the server with a
		// mod time < syncPeriodStart, or didn't exist before that time.
		//
		// But if en != 0, then we need to compare modification times to avoid uploading it again.

		// Need to update?
		//
		// Condition for upload:
		//    modification time within sync period
		//    if it's been seen before but not uploaded, is the time from this first sight longer than the MaxUploadWait
		//    and if we know about it from a directory listing, that it hasn't got the same upload time as on the store

		bool doUpload = false;
		std::string decisionReason = "unknown";

		// Only upload a file if the mod time locally is
		// different to that on the server.

		if(en == NULL || en->GetModificationTime() != modTime)
		{
			// Check the file modified within the acceptable time period we're checking
			// If the file isn't on the server, the acceptable time starts at zero.
			// Check pDirOnStore and en, because if we didn't download a directory listing,
			// pDirOnStore will be zero, but we know it's on the server.
			if(modTime < rParams.mSyncPeriodEnd)
			{
				if(pDirOnStore != NULL && en == NULL)
				{
					doUpload = true;
					decisionReason = "not on server";
				}
				else if(modTime >= rParams.mSyncPeriodStart)
				{
					doUpload = true;
					decisionReason = "modified since last sync";
				}
			}

			// However, just in case things are continually
			// modified, we check the first seen time.

			if(!doUpload && pendingFirstSeenTime != 0)
			{
				BOX_TRACE("Current period ends at " <<
					FormatTime(rParams.mSyncPeriodEnd, false, true) <<
					" and file has been pending since " <<
					FormatTime(pendingFirstSeenTime, false, true));
			}

			if(!doUpload && pendingFirstSeenTime != 0 &&
				// The two compares of syncPeriodEnd and
				// pendingFirstSeenTime are because the values
				// are unsigned.
				rParams.mSyncPeriodEnd > pendingFirstSeenTime &&
				(rParams.mSyncPeriodEnd - pendingFirstSeenTime)
				> rParams.mMaxUploadWait)
			{
				doUpload = true;
				std::ostringstream s;
				s << "continually modified for " <<
					FormatTime(rParams.mSyncPeriodEnd - pendingFirstSeenTime,
						false, true, false
						// !include_date, include_millis, !adjust_for_dst
					) << " seconds";
				decisionReason = s.str();
			}

			// Then make sure that if files are added with a
			// time less than the sync period start
			// (which can easily happen on file server), it
			// gets uploaded. The directory contents checksum
			// will pick up the fact it has been added, so the
			// store listing will be available when this happens.

			if(!doUpload &&
				modTime <= rParams.mSyncPeriodStart &&
				en != 0 &&
				en->GetModificationTime() != modTime)
			{
				doUpload = true;
				decisionReason = "mod time changed";
			}

			// And just to catch really badly off clocks in
			// the future for file server clients,
			// just upload the file if it's madly in the future.

			if(!doUpload && modTime >
				rParams.mUploadAfterThisTimeInTheFuture)
			{
				doUpload = true;
				decisionReason = "mod time in the future";
			}
		}
	
		if(en != NULL && en->GetModificationTime() == modTime)
		{
			doUpload = false;
			decisionReason = "not modified since last upload";
		}
		else if(!doUpload)
		{
			if(modTime > rParams.mSyncPeriodEnd)
			{
				box_time_t now = GetCurrentBoxTime();
				int age = BoxTimeToSeconds(now - modTime);
				std::ostringstream s;
				s << "modified too recently: only " << age << " seconds ago";
				decisionReason = s.str();
			}
			else
			{
				std::ostringstream s;
				s << "mod time is " << modTime <<
					" which is outside sync window, "
					<< rParams.mSyncPeriodStart << " to "
					<< rParams.mSyncPeriodEnd;
				decisionReason = s.str();
			}
		}

		BOX_TRACE("Upload decision: " << nonVssFilePath << ": " <<
			(doUpload ? "will upload" : "will not upload") <<
			" (" << decisionReason << ")");

		bool fileSynced = true;

		if(doUpload)
		{
			// Upload needed, don't mark sync success until
			// we've actually done it
			fileSynced = false;

			// Make sure we're connected -- must connect here so we know whether
			// the storage limit has been exceeded, and hence whether or not
			// to actually upload the file.
			rContext.GetConnection();

			bool uploadSuccess = false;

			// Only do this step if there is room on the server.
			// This step will be repeated later when there is space available
			for(int retries = 10; retries > 0 && !rContext.StorageLimitExceeded(); retries--)
			{
				// Upload the file to the server, recording the
				// object ID it returns
				bool noPreviousVersionOnServer = ((pDirOnStore != 0) && (en == 0));
				
				// Surround this in a try/catch block, to
				// catch errors, but still continue
				try
				{
					latestObjectID = UploadFile(rParams,
						filename,
						nonVssFilePath,
						rRemotePath + "/" + *f,
						storeFilename,
						fileSize, modTime,
						attributesHash,
						noPreviousVersionOnServer);

					if(latestObjectID == 0)
					{
						// storage limit exceeded
						rParams.mrContext.SetStorageLimitExceeded();
						uploadSuccess = false;
						allUpdatedSuccessfully = false;
					}
					else
					{
						uploadSuccess = true;
					}
				}
				catch(ConnectionException &e)
				{
					// Connection errors should just be
					// passed on to the main handler,
					// retries would probably just cause
					// more problems.
					rNotifier.NotifyFileUploadException(this, nonVssFilePath, e);
					throw;
				}
				catch(BoxException &e)
				{
					if(e.GetType() == BackupStoreException::ExceptionType &&
						e.GetSubType() == BackupStoreException::SignalReceived)
					{
						if(rParams.mrRunStatusProvider.StopRun())
						{
							// abort requested, pass the
							// exception on up.
							throw;
						}
						else
						{
							// Unexpected signal. Try again.
							continue;
						}
					}
					
					// an error occured -- make return
					// code false, to show error in directory
					allUpdatedSuccessfully = false;
					// Log it.
					SetErrorWhenReadingFilesystemObject(rParams, nonVssFilePath);
					rNotifier.NotifyFileUploadException(this, nonVssFilePath, e);
				}

				// Update structures if the file was uploaded successfully:
				if(uploadSuccess)
				{
					fileSynced = true;

					// delete from pending entries
					if(pendingFirstSeenTime != 0 && mpPendingEntries != 0)
					{
						mpPendingEntries->erase(*f);
					}

					break;
				}
				else
				{
					BOX_NOTICE("Upload failed, attempts remaining: " << retries);
				}
			}

			if(rContext.StorageLimitExceeded())
			{
				rNotifier.NotifyFileSkippedServerFull(this, nonVssFilePath);
			}
			else if(!uploadSuccess)
			{
				// We only stop the loop on upload success, StorageLimitExceeded and
				// running out of retries on signals received, and we ruled out the
				// first two, and know that the signals were not terminate signals.
				BOX_WARNING("Upload failed: all retry attempts failed due to "
					"non-fatal signals. Is your KeepAliveTime too short?");
			}
		}
		else if(en != 0 && en->GetAttributesHash() != attributesHash)
		{
			// Attributes have probably changed, upload them again.
			// If the attributes have changed enough, the directory
			// hash will have changed too, and so the dir will have
			// been downloaded, and the entry will be available.

			// Get connection
			BackupProtocolCallable &connection(rContext.GetConnection());

			// Only do this step if there is room on the server.
			// This step will be repeated later when there is
			// space available
			if(!rContext.StorageLimitExceeded())
			{
				try
				{
					rNotifier.NotifyFileUploadingAttributes(this,
						nonVssFilePath);
					
					// Update store
					BackupClientFileAttributes attr;
					attr.ReadAttributes(filename,
						false /* put mod times in the attributes, please */);
					std::auto_ptr<IOStream> attrStream(new MemBlockStream(attr));
					connection.QuerySetReplacementFileAttributes(mObjectID,
						attributesHash, storeFilename, attrStream);
					fileSynced = true;
				}
				catch (BoxException &e)
				{
					BOX_ERROR("Failed to read or store file attributes "
						"for '" << nonVssFilePath << "', will try again "
						"later");
				}
			}
		}

		if(modTime >= rParams.mSyncPeriodEnd)
		{
			// Allocate?
			if(mpPendingEntries == 0)
			{
				mpPendingEntries = new std::map<std::string, box_time_t>;
			}

			// Adding to mPendingEntries list
			if(pendingFirstSeenTime == 0)
			{
				// Haven't seen this before -- add to list!
				// Use mSyncPeriodEnd here, instead of modTime, because we want to
				// back up the file after it's spent MaxUploadWait time on our
				// pending list, which uses (its own, later) mSyncPeriodEnd to make
				// that decision. Otherwise we will end up backing up the file too
				// late (we know that it was modified *after* the current
				// mSyncPeriodEnd), which normally doesn't matter much, but upsets
				// the timing-sensitive test_continuously_updated_file.
				(*mpPendingEntries)[*f] = rParams.mSyncPeriodEnd;

				BOX_INFO("Adding file to pending list with time " <<
					FormatTime(rParams.mSyncPeriodEnd, false));
			}
		}
		
		// Zero pointer in rEntriesLeftOver, if we have a pointer to zero
		if(en != 0)
		{
			auto i = std::find(rEntriesLeftOver.begin(), rEntriesLeftOver.end(), en);
			if(i != rEntriesLeftOver.end())
			{
				*i = NULL;
			}
		}
		
		// Does this file need an entry in the ID map?
		if(fileSize >= rParams.mFileTrackingSizeThreshold)
		{
			// Get the map
			BackupClientInodeToIDMap &new_id_map(rContext.GetNewIDMap());
			const BackupClientInodeToIDMap &old_id_map(rContext.GetCurrentIDMap());
			int64_t old_object_id = 0, old_dir_id = 0;
			int64_t new_object_id = 0, new_dir_id = 0;
			std::string old_local_path, new_local_path;
		
			// If there is already an entry in the new ID map, then we've seen the same
			// file multiple times, so they must be hardlinked together. We don't
			// support this fully, so warn about it. Also don't overwrite the existing
			// entry in the ID map.
			if(new_id_map.Lookup(inodeNum, new_object_id, new_dir_id, &new_local_path))
			{
				BOX_WARNING("Uploading the same file multiple times: ID " <<
					inodeNum << " (" << nonVssFilePath << " and " <<
					new_local_path << "). Hardlinks are not fully supported.");
			}
			// If we already have an object ID, then we can create an entry in the new
			// map using it:
			else if(latestObjectID != 0)
			{
				std::string file_state;
				if(doUpload)
				{
					file_state = "uploaded";
				}
				else if(renamed)
				{
					file_state = "renamed";
				}
				else
				{
					// Must have come from pDirOnStore
					ASSERT(pDirOnStore != NULL);
					file_state = "existing";
				}

				BOX_TRACE("Storing " << file_state << " file ID " << inodeNum << " "
					"(" << nonVssFilePath << ") in ID map as object " <<
					BOX_FORMAT_OBJECTID(latestObjectID) << " with parent " <<
					BOX_FORMAT_OBJECTID(mObjectID));
				new_id_map.AddToMap(inodeNum, latestObjectID,
					mObjectID /* containing directory */,
					nonVssFilePath);
			}
			else if(!old_id_map.Lookup(inodeNum, old_object_id, old_dir_id, &old_local_path))
			{
				// It didn't exist before, but we didn't upload it this time either,
				// so it should be a very new file. Hopefully we will as soon as
				// it's old enough.
				BOX_TRACE("New file was not uploaded, so not added to new ID map: "
					<< inodeNum << " (" << nonVssFilePath << ")");
			}
			// We have backed up this file before (it's in the old map), so copy the old
			// entry: (TODO: implement real hardlink support)
			else
			{
				BOX_TRACE("Copying previous file ID " << inodeNum << " (" <<
					nonVssFilePath << ") to new ID map: object ID " <<
					BOX_FORMAT_OBJECTID(old_object_id) << " with parent " <<
					BOX_FORMAT_OBJECTID(old_dir_id));
				new_id_map.AddToMap(inodeNum, old_object_id, old_dir_id, old_local_path);
			}
		}

		if(fileSynced)
		{
			rNotifier.NotifyFileSynchronised(this, nonVssFilePath, fileSize);
		}
	}

	// Erase contents of files to save space when recursing
	rFiles.clear();

	// Delete the pending entries, if the map is empty
	if(mpPendingEntries != 0 && mpPendingEntries->size() == 0)
	{
		BOX_TRACE("Deleting mpPendingEntries from dir ID " <<
			BOX_FORMAT_OBJECTID(mObjectID));
		delete mpPendingEntries;
		mpPendingEntries = 0;
	}
	
	// Do directories
	for(std::vector<std::string>::const_iterator d = rDirs.begin();
		d != rDirs.end(); ++d)
	{
		// Send keep-alive message if needed
		rContext.DoKeepAlive();
		
		// Get the local filename
		std::string dirname(MakeFullPath(rLocalPath, *d));
		std::string nonVssDirPath = ConvertVssPathToRealPath(dirname, rBackupLocation);
	
		// See if it's in the listing (if we have one)
		BackupStoreFilenameClear storeFilename(*d);
		BackupStoreDirectory::Entry *en = 0;
		if(pDirOnStore != 0)
		{
			DecryptedEntriesMap_t::iterator i(decryptedEntries.find(*d));
			if(i != decryptedEntries.end())
			{
				en = i->second;
			}
		}
		
		// Check that the entry which might have been found is in fact a directory
		if((en != 0) && !(en->IsDir()))
		{
			// Entry exists, but is not a directory. Bad.
			// Get rid of it.
			BackupProtocolCallable &connection(rContext.GetConnection());
			connection.QueryDeleteFile(mObjectID /* in directory */, storeFilename);

			std::string filenameClear = DecryptFilename(en, rRemotePath);
			rNotifier.NotifyFileDeleted(en->GetObjectID(), filenameClear);
			
			// Nothing found
			en = 0;
		}

		// Zero pointer in rEntriesLeftOver, if we have a pointer to zero
		if(en != 0)
		{
			for(unsigned int l = 0; l < rEntriesLeftOver.size(); ++l)
			{
				if(rEntriesLeftOver[l] == en)
				{
					rEntriesLeftOver[l] = 0;
					break;
				}
			}
		}

		// Flag for having created directory, so can optimise the
		// recursive call not to read it again, because we know
		// it's empty.
		bool haveJustCreatedDirOnServer = false;

		// Next, see if it's in the list of sub directories
		BackupClientDirectoryRecord *psubDirRecord = 0;
		std::map<std::string, BackupClientDirectoryRecord *>::iterator
			e(mSubDirectories.find(*d));

		if(e != mSubDirectories.end())
		{
			// In the list, just use this pointer
			psubDirRecord = e->second;
		}
		else
		{
			// Note: if we have exceeded our storage limit, then
			// we should not upload any more data, nor create any
			// DirectoryRecord representing data that would have
			// been uploaded. This step will be repeated when
			// there is some space available.
			bool doCreateDirectoryRecord = true;
			
			// Need to create the record. But do we need to create the directory on the server?
			int64_t subDirObjectID = 0;
			if(en != 0)
			{
				// No. Exists on the server, and we know about it from the listing.
				subDirObjectID = en->GetObjectID();
			}
			else if(rContext.StorageLimitExceeded())
			// know we've got a connection if we get this far,
			// as dir will have been modified.
			{
				doCreateDirectoryRecord = false;
			}
			else
			{
				// Yes, creation required!
				// It is known that it doesn't exist:
				//
				// if en == 0 and pDirOnStore == 0, then the
				//   directory has had an initial sync, and
				//   hasn't been modified (Really? then why
				//   are we here? TODO FIXME)
				//   so it has definitely been created already
				//   (so why create it again?)
				//
				// if en == 0 but pDirOnStore != 0, well... obviously it doesn't exist.
				//
				subDirObjectID = CreateRemoteDir(dirname,
					nonVssDirPath, rRemotePath + "/" + *d,
					storeFilename, &haveJustCreatedDirOnServer,
					rParams);
				doCreateDirectoryRecord = (subDirObjectID != 0);
			}

			if(doCreateDirectoryRecord)
			{
				// New an object for this
				psubDirRecord = CreateSubdirectoryRecord(subDirObjectID, *d);

				// Store in list
				try
				{
					mSubDirectories[*d] = psubDirRecord;
				}
				catch(...)
				{
					delete psubDirRecord;
					psubDirRecord = 0;
					throw;
				}
			}
		}

		// ASSERT(psubDirRecord != 0 || rContext.StorageLimitExceeded());
		// There's another possible reason now: the directory no longer
		// existed when we finally got around to checking its
		// attributes. See for example Brendon Baumgartner's reported
		// error with Wordpress cache directories.

		if(psubDirRecord)
		{
			// Sync this sub directory too
			psubDirRecord->SyncDirectory(rParams, mObjectID, dirname,
				rRemotePath + "/" + *d, rBackupLocation,
				haveJustCreatedDirOnServer);
		}
	}

	// Delete everything which is on the store, but not on disc
	for(unsigned int l = 0; l < rEntriesLeftOver.size(); ++l)
	{
		if(rEntriesLeftOver[l] != 0)
		{
			BackupStoreDirectory::Entry *en = rEntriesLeftOver[l];

			// These entries can't be deleted immediately, as it would prevent
			// renaming and moving of objects working properly. So we add them
			// to a list, which is actually deleted at the very end of the session.
			// If there's an error during the process, it doesn't matter if things
			// aren't actually deleted, as the whole state will be reset anyway.
			BackupClientDeleteList &rdel(rContext.GetDeleteList());
			std::string filenameClear;
			bool isCorruptFilename = false;

			try
			{
				filenameClear = DecryptFilename(en, rRemotePath);
			}
			catch (CipherException &e)
			{
				BOX_ERROR("Failed to decrypt a filename, "
					"scheduling that file for deletion");
				filenameClear = "<corrupt filename>";
				isCorruptFilename = true;
			}

			std::string localName = MakeFullPath(rLocalPath, filenameClear);
			std::string nonVssLocalName = ConvertVssPathToRealPath(localName, rBackupLocation);

			// Delete this entry -- file or directory?
			if((en->GetFlags() & BackupStoreDirectory::Entry::Flags_File) != 0)
			{
				// Set a pending deletion for the file
				rdel.AddFileDelete(mObjectID, en->GetName(), localName);
			}
			else if((en->GetFlags() & BackupStoreDirectory::Entry::Flags_Dir) != 0)
			{
				// Set as a pending deletion for the directory
				rdel.AddDirectoryDelete(en->GetObjectID(), localName);
				
				// If there's a directory record for it in
				// the sub directory map, delete it now
				BackupStoreFilenameClear dirname(en->GetName());
				std::map<std::string, BackupClientDirectoryRecord *>::iterator
					e(mSubDirectories.find(filenameClear));
				if(e != mSubDirectories.end() && !isCorruptFilename)
				{
					// Carefully delete the entry from the map
					BackupClientDirectoryRecord *rec = e->second;
					mSubDirectories.erase(e);
					delete rec;

					BOX_TRACE("Deleted directory record for " << nonVssLocalName);
				}
			}
		}
	}

	// Return success flag (will be false if some files failed)
	return allUpdatedSuccessfully;
}

// Returns NULL if not renamed, or the new BackupStoreDirectory::Entry in p_dir (containing the ID
// of the renamed (moved) object) otherwise.
BackupStoreDirectory::Entry* BackupClientDirectoryRecord::CheckForRename(
	BackupClientContext& context, BackupStoreDirectory* p_dir,
	const BackupStoreFilenameClear& remote_filename, InodeRefType inode_num,
	const std::string& local_path_non_vss)
{
	// We now know...
	// 1) File has just been added
	// 2) It's not in the store
	
	// Do we know about the inode number?
	const BackupClientInodeToIDMap &idMap(context.GetCurrentIDMap());
	int64_t prev_object_id = 0, prev_dir_id = 0;
	if(!idMap.Lookup(inode_num, prev_object_id, prev_dir_id))
	{
		return NULL;
	}

	std::ostringstream msg_prefix_buf;
	msg_prefix_buf << local_path_non_vss << ": have seen inode " << inode_num << " before, "
		"with ID " << BOX_FORMAT_OBJECTID(prev_object_id) << " in directory " <<
		BOX_FORMAT_OBJECTID(prev_dir_id);
	std::string msg_prefix = msg_prefix_buf.str();

	std::ostringstream msg_suffix_buf;
	msg_suffix_buf << ", so will not move to directory " << BOX_FORMAT_OBJECTID(mObjectID);
	std::string msg_suffix = msg_suffix_buf.str();

	// We've seen this inode number before. Look up on the server to get the filename, to
	// reconstruct the local filename that it had when it was backed up before (elsewhere):
	std::string possible_prev_local_name;
	bool was_a_dir = false;
	bool was_current_version = false;
	box_time_t remote_mod_time = 0, remote_attr_hash = 0;
	BackupStoreFilenameClear prev_remote_name;
	if(!context.FindFilename(prev_object_id, prev_dir_id, possible_prev_local_name, was_a_dir,
		was_current_version, &remote_mod_time, &remote_attr_hash, &prev_remote_name))
	{
		BOX_TRACE(msg_prefix << ", but that no longer exists on the server, so cannot find "
			"corresponding local file to check for rename");
		return NULL;
	}

	// Only interested if it's a file and the latest version
	if(was_a_dir || !was_current_version)
	{
		BOX_TRACE(msg_prefix << ", but that was " <<
			(was_a_dir ? "a directory" : "not the latest version") <<
			msg_suffix);
		return NULL;
	}

	// Check that the object we found in the ID map doesn't exist on disc
	EMU_STRUCT_STAT st;
	if(EMU_STAT(possible_prev_local_name.c_str(), &st) == 0)
	{
		BOX_TRACE(msg_prefix << ", but that was for " << possible_prev_local_name << " "
			"which still exists locally (most likely moved and replaced)" << msg_suffix);
		return NULL;
	}

	if(errno != ENOENT)
	{
		BOX_TRACE(BOX_SYS_ERROR_MESSAGE(msg_prefix << ", but that was for " <<
			possible_prev_local_name << " which we cannot access" << msg_suffix));
		return NULL;
	}

	// Doesn't exist locally, but does exist on the server.
	// Therefore we can safely rename it to this new file.

	// Get the connection to the server
	BackupProtocolCallable &connection(context.GetConnection());

	// Only do this step if there is room on the server.
	// This step will be repeated later when there is space available
	if(context.StorageLimitExceeded())
	{
		BOX_TRACE(possible_prev_local_name << " appears to have been renamed to " <<
			local_path_non_vss << ", but our account on the server is full, "
			"so not moving object " <<
			BOX_FORMAT_OBJECTID(prev_object_id) << " from directory " <<
			BOX_FORMAT_OBJECTID(prev_dir_id) << " to " <<
			BOX_FORMAT_OBJECTID(mObjectID));
		return NULL;
	}
		
	// Rename the existing files (ie include old versions) on the server
	connection.QueryMoveObject(prev_object_id, prev_dir_id,
		mObjectID /* move to this directory */,
		BackupProtocolMoveObject::Flags_MoveAllWithSameName |
		BackupProtocolMoveObject::Flags_AllowMoveOverDeletedObject,
		remote_filename);
		
	// Stop the attempt to delete the file in the original location
	BackupClientDeleteList &rdelList(context.GetDeleteList());
	rdelList.StopFileDeletion(prev_dir_id, prev_remote_name);
	
	BOX_TRACE(possible_prev_local_name << " appears to have been renamed to " <<
		local_path_non_vss << ", so moving object " <<
		BOX_FORMAT_OBJECTID(prev_object_id) << " from directory " <<
		BOX_FORMAT_OBJECTID(prev_dir_id) << " to " <<
		BOX_FORMAT_OBJECTID(mObjectID));

	// Create new entry in the directory for it: will be near enough what's actually on the
	// server for the rest to work.
	return p_dir->AddEntry(remote_filename, remote_mod_time, prev_object_id,
		0 /* size in blocks unknown, but not needed */,
		BackupStoreDirectory::Entry::Flags_File, remote_attr_hash);
}
		
int64_t BackupClientDirectoryRecord::CreateRemoteDir(const std::string& localDirPath,
	const std::string& nonVssDirPath, const std::string& remoteDirPath,
	BackupStoreFilenameClear& storeFilename, bool* pHaveJustCreatedDirOnServer,
	BackupClientDirectoryRecord::SyncParams &rParams)
{
	// Get attributes
	box_time_t attrModTime = 0;
	InodeRefType inodeNum = 0;
	BackupClientFileAttributes attr;
	*pHaveJustCreatedDirOnServer = false;
	ProgressNotifier& rNotifier(rParams.mrContext.GetProgressNotifier());

	try
	{
		attr.ReadAttributes(localDirPath,
			true /* directories have zero mod times */,
			0 /* not interested in mod time */,
			&attrModTime, 0 /* not file size */,
			&inodeNum);
	}
	catch (BoxException &e)
	{
		// We used to try to recover from this, but we'd need an
		// attributes block to upload to the server, so we have to
		// skip creating the directory instead.
		BOX_WARNING("Failed to read attributes of directory, "
			"ignoring it for now: " << nonVssDirPath);
		return 0; // no object ID
	}

	// Check to see if the directory been renamed
	// First, do we have a record in the ID map?
	int64_t renameObjectID = 0, renameInDirectory = 0;
	bool renameDir = false;
	const BackupClientInodeToIDMap &idMap(rParams.mrContext.GetCurrentIDMap());

	if(idMap.Lookup(inodeNum, renameObjectID, renameInDirectory))
	{
		// Look up on the server to get the name, to build the local filename
		std::string localPotentialOldName;
		bool isDir = false;
		bool isCurrentVersion = false;
		if(rParams.mrContext.FindFilename(renameObjectID, renameInDirectory,
			localPotentialOldName, isDir, isCurrentVersion))
		{	
			// Only interested if it's a directory
			if(isDir && isCurrentVersion)
			{
				// Check that the object doesn't exist already
				EMU_STRUCT_STAT st;
				if(EMU_STAT(localPotentialOldName.c_str(), &st) != 0 &&
					errno == ENOENT)
				{
					// Doesn't exist locally, but does exist
					// on the server. Therefore we can
					// safely rename it.
					renameDir = true;
				}
			}
		}
	}

	// Get connection
	BackupProtocolCallable &connection(rParams.mrContext.GetConnection());

	// Don't do a check for storage limit exceeded here, because if we get to this
	// stage, a connection will have been opened, and the status known, so the check
	// in the else if(...) above will be correct.

	// Build attribute stream for sending
	std::auto_ptr<IOStream> attrStream(new MemBlockStream(attr));

	if(renameDir)
	{
		// Rename the existing directory on the server
		connection.QueryMoveObject(renameObjectID,
			renameInDirectory,
			mObjectID /* move to this directory */,
			BackupProtocolMoveObject::Flags_MoveAllWithSameName |
			BackupProtocolMoveObject::Flags_AllowMoveOverDeletedObject,
			storeFilename);

		// Put the latest attributes on it
		connection.QueryChangeDirAttributes(renameObjectID, attrModTime, attrStream);

		// Stop it being deleted later
		BackupClientDeleteList &rdelList(
			rParams.mrContext.GetDeleteList());
		rdelList.StopDirectoryDeletion(renameObjectID);

		// This is the ID for the renamed directory
		return renameObjectID;
	}
	else
	{
		int64_t subDirObjectID = 0; // no object ID

		// Create a new directory
		try
		{
			subDirObjectID = connection.QueryCreateDirectory(
				mObjectID, attrModTime, storeFilename,
				attrStream)->GetObjectID();
			// Flag as having done this for optimisation later
			*pHaveJustCreatedDirOnServer = true;
		}
		catch(BoxException &e)
		{
			int type, subtype;
			connection.GetLastError(type, subtype);
			rNotifier.NotifyFileUploadServerError(this, nonVssDirPath,
				type, subtype);
			if(e.GetType() == ConnectionException::ExceptionType &&
				e.GetSubType() == ConnectionException::Protocol_UnexpectedReply &&
				type == BackupProtocolError::ErrorType &&
				subtype == BackupProtocolError::Err_StorageLimitExceeded)
			{
				// The hard limit was exceeded on the server, notify!
				rParams.mrContext.SetStorageLimitExceeded();
				rParams.mrSysadminNotifier.NotifySysadmin(
					SysadminNotifier::StoreFull);
			}
			else
			{
				throw;
			}
		}

		if(*pHaveJustCreatedDirOnServer)
		{
			rNotifier.NotifyDirectoryCreated(subDirObjectID,
				nonVssDirPath, remoteDirPath);
		}

		return subDirObjectID;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::RemoveDirectoryInPlaceOfFile(
//		         SyncParams &, BackupStoreDirectory *, int64_t, const std::string &)
//		Purpose: Called to resolve difficulties when a directory is found on the
//		         store where a file is to be uploaded.
//		Created: 9/7/04
//
// --------------------------------------------------------------------------
void BackupClientDirectoryRecord::RemoveDirectoryInPlaceOfFile(
	SyncParams &rParams,
	BackupStoreDirectory* pDirOnStore,
	BackupStoreDirectory::Entry* pEntry,
	const std::string &rFilename)
{
	// First, delete the directory
	BackupProtocolCallable &connection(rParams.mrContext.GetConnection());
	connection.QueryDeleteDirectory(pEntry->GetObjectID());

	BackupStoreFilenameClear clear(pEntry->GetName());
	rParams.mrContext.GetProgressNotifier().NotifyDirectoryDeleted(
		pEntry->GetObjectID(), clear.GetClearFilename());

	// Then, delete any directory record
	std::map<std::string, BackupClientDirectoryRecord *>::iterator
		e(mSubDirectories.find(rFilename));

	if(e != mSubDirectories.end())
	{
		// A record exists for this, remove it
		BackupClientDirectoryRecord *psubDirRecord = e->second;
		mSubDirectories.erase(e);

		// And delete the object
		delete psubDirRecord;
	}
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::UploadFile(
//			 BackupClientDirectoryRecord::SyncParams &,
//			 const std::string &,
//			 const BackupStoreFilename &,
//			 int64_t, box_time_t, box_time_t, bool)
//		Purpose: Private. Upload a file to the server. May send
//			 a patch instead of the whole thing
//		Created: 20/1/04
//
// --------------------------------------------------------------------------
int64_t BackupClientDirectoryRecord::UploadFile(
	BackupClientDirectoryRecord::SyncParams &rParams,
	const std::string &rLocalPath,
	const std::string &rNonVssFilePath,
	const std::string &rRemotePath,
	const BackupStoreFilenameClear &rStoreFilename,
	int64_t FileSize,
	box_time_t ModificationTime,
	box_time_t AttributesHash,
	bool NoPreviousVersionOnServer)
{
	BackupClientContext& rContext(rParams.mrContext);
	ProgressNotifier& rNotifier(rContext.GetProgressNotifier());

	// Get the connection
	BackupProtocolCallable &connection(rContext.GetConnection());

	// Info
	int64_t objID = 0;
	int64_t uploadedSize = -1;
	
	// Use a try block to catch store full errors
	try
	{
		std::auto_ptr<BackupStoreFileEncodeStream> apStreamToUpload;
		int64_t diffFromID = 0;

		// Might an old version be on the server, and is the file
		// size over the diffing threshold?
		if(!NoPreviousVersionOnServer &&
			FileSize >= rParams.mDiffingUploadSizeThreshold)
		{
			// YES -- try to do diff, if possible
			// First, query the server to see if there's an old version available
			std::auto_ptr<BackupProtocolSuccess> getBlockIndex(connection.QueryGetBlockIndexByName(mObjectID, rStoreFilename));
			diffFromID = getBlockIndex->GetObjectID();
			
			if(diffFromID != 0)
			{
				// Found an old version

				// Get the index
				std::auto_ptr<IOStream> blockIndexStream(connection.ReceiveStream());
			
				//
				// Diff the file
				//

				rContext.ManageDiffProcess();

				bool isCompletelyDifferent = false;

				apStreamToUpload = BackupStoreFile::EncodeFileDiff(
					rLocalPath,
					mObjectID, /* containing directory */
					rStoreFilename, diffFromID, *blockIndexStream,
					connection.GetTimeout(),
					&rContext, // DiffTimer implementation
					0 /* not interested in the modification time */,
					&isCompletelyDifferent,
					rParams.mpBackgroundTask);

				if(isCompletelyDifferent)
				{
					diffFromID = 0;
				}

				rContext.UnManageDiffProcess();
			}
		}

		if(apStreamToUpload.get())
		{
			rNotifier.NotifyFileUploadingPatch(this, rNonVssFilePath,
				apStreamToUpload->GetBytesToUpload());
		}
		else // No patch upload, so do a normal upload
		{
			// below threshold or nothing to diff from, so upload whole
			rNotifier.NotifyFileUploading(this, rNonVssFilePath);
			
			// Prepare to upload, getting a stream which will encode the file as we go along
			apStreamToUpload = BackupStoreFile::EncodeFile(
				rLocalPath, mObjectID, /* containing directory */
				rStoreFilename, NULL, &rParams,
				&(rParams.mrRunStatusProvider),
				rParams.mpBackgroundTask);
		}

		rContext.SetNiceMode(true);
		std::auto_ptr<IOStream> apWrappedStream;

		if(rParams.mMaxUploadRate > 0)
		{
			apWrappedStream.reset(new RateLimitingStream(
				*apStreamToUpload, rParams.mMaxUploadRate));
		}
		else
		{
			// Wrap the stream in *something*, so that
			// QueryStoreFile() doesn't delete the original
			// stream (upload object) and we can retrieve
			// the byte counter.
			apWrappedStream.reset(new BufferedStream(*apStreamToUpload));
		}

		// Send to store
		std::auto_ptr<BackupProtocolSuccess> stored(
			connection.QueryStoreFile(mObjectID, ModificationTime, AttributesHash,
				diffFromID, rStoreFilename, apWrappedStream));

		rContext.SetNiceMode(false);

		// Get object ID from the result
		objID = stored->GetObjectID();
		uploadedSize = apStreamToUpload->GetTotalBytesSent();
	}
	catch(BoxException &e)
	{
		rContext.UnManageDiffProcess();

		if(e.GetType() == ConnectionException::ExceptionType &&
			e.GetSubType() == ConnectionException::Protocol_UnexpectedReply)
		{
			// Check and see what error the protocol has,
			// this is more useful to users than the exception.
			int type, subtype;
			if(connection.GetLastError(type, subtype))
			{
				if(type == BackupProtocolError::ErrorType
				&& subtype == BackupProtocolError::Err_StorageLimitExceeded)
				{
					// The hard limit was exceeded on the server, notify!
					rParams.mrSysadminNotifier.NotifySysadmin(
						SysadminNotifier::StoreFull);
					// return an error code instead of
					// throwing an exception that we
					// can't debug.
					return 0;
				}
				rNotifier.NotifyFileUploadServerError(this,
					rNonVssFilePath, type, subtype);
			}
		}
		
		// Send the error on it's way
		throw;
	}

	rNotifier.NotifyFileUploaded(this, rNonVssFilePath, FileSize,
		uploadedSize, objID);

	// Return the new object ID of this file
	return objID;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::SetErrorWhenReadingFilesystemObject(
//			 SyncParams &, const char *)
//		Purpose: Sets the error state when there were problems
//			 reading an object from the filesystem.
//		Created: 29/3/04
//
// --------------------------------------------------------------------------
void BackupClientDirectoryRecord::SetErrorWhenReadingFilesystemObject(
	BackupClientDirectoryRecord::SyncParams &rParams,
	const std::string& rFilename)
{
	// Zero hash, so it gets synced properly next time round.
	::memset(mStateChecksum, 0, sizeof(mStateChecksum));

	// More detailed logging was already done by the caller, but if we
	// have a read error reported, we need to be able to search the logs
	// to find out which file it was, so we need to log a consistent and
	// clear error message.
	BOX_WARNING("Failed to backup file, see above for details: " <<
		rFilename);

	// Mark that an error occured in the parameters object
	rParams.mReadErrorsOnFilesystemObjects = true;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::SyncParams::SyncParams(BackupClientContext &)
//		Purpose: Constructor
//		Created: 8/3/04
//
// --------------------------------------------------------------------------
BackupClientDirectoryRecord::SyncParams::SyncParams(
	RunStatusProvider &rRunStatusProvider,
	SysadminNotifier &rSysadminNotifier,
	ProgressNotifier &rProgressNotifier,
	BackupClientContext &rContext,
	BackgroundTask *pBackgroundTask)
: mSyncPeriodStart(0),
  mSyncPeriodEnd(0),
  mMaxUploadWait(0),
  mMaxFileTimeInFuture(99999999999999999LL),
  mFileTrackingSizeThreshold(16*1024),
  mDiffingUploadSizeThreshold(16*1024),
  mpBackgroundTask(pBackgroundTask),
  mrRunStatusProvider(rRunStatusProvider),
  mrSysadminNotifier(rSysadminNotifier),
  mrProgressNotifier(rProgressNotifier),
  mrContext(rContext),
  mReadErrorsOnFilesystemObjects(false),
  mMaxUploadRate(0),
  mUploadAfterThisTimeInTheFuture(99999999999999999LL),
  mHaveLoggedWarningAboutFutureFileTimes(false)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::SyncParams::~SyncParams()
//		Purpose: Destructor
//		Created: 8/3/04
//
// --------------------------------------------------------------------------
BackupClientDirectoryRecord::SyncParams::~SyncParams()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::Deserialize(Archive & rArchive)
//		Purpose: Deserializes this object instance from a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void BackupClientDirectoryRecord::Deserialize(Archive & rArchive)
{
	// Make deletion recursive
	DeleteSubDirectories();

	// Delete maps
	if(mpPendingEntries != 0)
	{
		delete mpPendingEntries;
		mpPendingEntries = 0;
	}

	//
	//
	//
	rArchive.Read(mObjectID);
	rArchive.Read(mSubDirName);
	rArchive.Read(mInitialSyncDone);
	rArchive.Read(mSyncDone);

	//
	//
	//
	int64_t iCount = 0;
	rArchive.Read(iCount);

	if(iCount != sizeof(mStateChecksum)/sizeof(mStateChecksum[0]))
	{
		// we have some kind of internal system representation change: throw for now
		THROW_EXCEPTION(CommonException, Internal)
	}

	for(int v = 0; v < iCount; v++)
	{
		// Load each checksum entry
		rArchive.Read(mStateChecksum[v]);
	}

	//
	//
	//
	iCount = 0;
	rArchive.Read(iCount);

	if(iCount > 0)
	{
		// load each pending entry
		mpPendingEntries = new std::map<std::string, box_time_t>;
		if(!mpPendingEntries)
		{
			throw std::bad_alloc();
		}

		for(int v = 0; v < iCount; v++)
		{
			std::string strItem;
			box_time_t btItem;

			rArchive.Read(strItem);
			rArchive.Read(btItem);
			(*mpPendingEntries)[strItem] = btItem;
		}
	}

	//
	//
	//
	iCount = 0;
	rArchive.Read(iCount);

	if(iCount > 0)
	{
		for(int v = 0; v < iCount; v++)
		{
			std::string strItem;
			rArchive.Read(strItem);

			BackupClientDirectoryRecord* pSubDirRecord =
				new BackupClientDirectoryRecord(0, "");
			// will be deserialized anyway, give it id 0 for now

			if(!pSubDirRecord)
			{
				throw std::bad_alloc();
			}

			/***** RECURSE *****/
			pSubDirRecord->Deserialize(rArchive);
			mSubDirectories[strItem] = pSubDirRecord;
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientDirectoryRecord::Serialize(Archive & rArchive)
//		Purpose: Serializes this object instance into a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void BackupClientDirectoryRecord::Serialize(Archive & rArchive) const
{
	//
	//
	//
	rArchive.Write(mObjectID);
	rArchive.Write(mSubDirName);
	rArchive.Write(mInitialSyncDone);
	rArchive.Write(mSyncDone);

	//
	//
	//
	int64_t iCount = 0;

	// when reading back the archive, we will
	// need to know how many items there are.
	iCount = sizeof(mStateChecksum) / sizeof(mStateChecksum[0]);
	rArchive.Write(iCount);

	for(int v = 0; v < iCount; v++)
	{
		rArchive.Write(mStateChecksum[v]);
	}

	//
	//
	//
	if(!mpPendingEntries)
	{
		iCount = 0;
		rArchive.Write(iCount);
	}
	else
	{
		iCount = mpPendingEntries->size();
		rArchive.Write(iCount);

		for(std::map<std::string, box_time_t>::const_iterator
			i  = mpPendingEntries->begin();
			i != mpPendingEntries->end(); i++)
		{
			rArchive.Write(i->first);
			rArchive.Write(i->second);
		}
	}
	//
	//
	//
	iCount = mSubDirectories.size();
	rArchive.Write(iCount);

	for(std::map<std::string, BackupClientDirectoryRecord*>::const_iterator
		i  = mSubDirectories.begin();
		i != mSubDirectories.end(); i++)
	{
		const BackupClientDirectoryRecord* pSubItem = i->second;
		ASSERT(pSubItem);

		rArchive.Write(i->first);
		pSubItem->Serialize(rArchive);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Location::Location()
//		Purpose: Constructor
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
Location::Location()
: mIDMapIndex(0)
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    Location::~Location()
//		Purpose: Destructor
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
Location::~Location()
{ }

// --------------------------------------------------------------------------
//
// Function
//		Name:    Location::Serialize(Archive & rArchive)
//		Purpose: Serializes this object instance into a stream of bytes,
//               using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void Location::Serialize(Archive & rArchive) const
{
	//
	//
	//
	rArchive.Write(mName);
	rArchive.Write(mPath);
	rArchive.Write(mIDMapIndex);

	//
	//
	//
	if(!mapDirectoryRecord.get())
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_NOOP;
		rArchive.Write(aMagicMarker);
	}
	else
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_RECURSE; // be explicit about whether recursion follows
		rArchive.Write(aMagicMarker);

		mapDirectoryRecord->Serialize(rArchive);
	}

	//
	//
	//
	if(!mapExcludeFiles.get())
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_NOOP;
		rArchive.Write(aMagicMarker);
	}
	else
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_RECURSE; // be explicit about whether recursion follows
		rArchive.Write(aMagicMarker);

		mapExcludeFiles->Serialize(rArchive);
	}

	//
	//
	//
	if(!mapExcludeDirs.get())
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_NOOP;
		rArchive.Write(aMagicMarker);
	}
	else
	{
		int64_t aMagicMarker = ARCHIVE_MAGIC_VALUE_RECURSE; // be explicit about whether recursion follows
		rArchive.Write(aMagicMarker);

		mapExcludeDirs->Serialize(rArchive);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Location::Deserialize(Archive & rArchive)
//		Purpose: Deserializes this object instance from a stream of bytes, using an Archive abstraction.
//
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------
void Location::Deserialize(Archive &rArchive)
{
	//
	//
	//
	mapDirectoryRecord.reset();
	mapExcludeFiles.reset();
	mapExcludeDirs.reset();

	//
	//
	//
	rArchive.Read(mName);
	rArchive.Read(mPath);
	rArchive.Read(mIDMapIndex);

	//
	//
	//
	int64_t aMagicMarker = 0;
	rArchive.Read(aMagicMarker);

	if(aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if(aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		BackupClientDirectoryRecord *pSubRecord = new BackupClientDirectoryRecord(0, "");
		if(!pSubRecord)
		{
			throw std::bad_alloc();
		}

		mapDirectoryRecord.reset(pSubRecord);
		mapDirectoryRecord->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(ClientException, CorruptStoreObjectInfoFile);
	}

	//
	//
	//
	rArchive.Read(aMagicMarker);

	if(aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if(aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		mapExcludeFiles.reset(new ExcludeList);
		if(!mapExcludeFiles.get())
		{
			throw std::bad_alloc();
		}

		mapExcludeFiles->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(ClientException, CorruptStoreObjectInfoFile);
	}

	//
	//
	//
	rArchive.Read(aMagicMarker);

	if(aMagicMarker == ARCHIVE_MAGIC_VALUE_NOOP)
	{
		// NOOP
	}
	else if(aMagicMarker == ARCHIVE_MAGIC_VALUE_RECURSE)
	{
		mapExcludeDirs.reset(new ExcludeList);
		if(!mapExcludeDirs.get())
		{
			throw std::bad_alloc();
		}

		mapExcludeDirs->Deserialize(rArchive);
	}
	else
	{
		// there is something going on here
		THROW_EXCEPTION(ClientException, CorruptStoreObjectInfoFile);
	}
}
