// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientRestore.cpp
//		Purpose: 
//		Created: 23/11/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <set>
#include <limits.h>
#include <stdio.h>
#include <errno.h>

#include "BackupClientRestore.h"
#include "autogen_BackupProtocolClient.h"
#include "CommonException.h"
#include "BackupClientFileAttributes.h"
#include "IOStream.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "CollectInBufferStream.h"
#include "FileStream.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#define MAX_BYTES_WRITTEN_BETWEEN_RESTORE_INFO_SAVES (128*1024)

class RestoreResumeInfo
{
public:
	// constructor
	RestoreResumeInfo()
		: mNextLevelID(0),
		  mpNextLevel(0)
	{
	}
	
	// destructor
	~RestoreResumeInfo()
	{
		delete mpNextLevel;
		mpNextLevel = 0;
	}
	
	// Get a next level object
	RestoreResumeInfo &AddLevel(int64_t ID, const std::string &rLocalName)
	{
		ASSERT(mpNextLevel == 0 && mNextLevelID == 0);
		mpNextLevel = new RestoreResumeInfo;
		mNextLevelID = ID;
		mNextLevelLocalName = rLocalName;
		return *mpNextLevel;
	}
	
	// Remove the next level info
	void RemoveLevel()
	{
		ASSERT(mpNextLevel != 0 && mNextLevelID != 0);
		delete mpNextLevel;
		mpNextLevel = 0;
		mNextLevelID = 0;
		mNextLevelLocalName.erase();
	}
	
	void Save(const std::string &rFilename) const
	{
		// TODO: use proper buffered streams when they're done
		// Build info in memory buffer
		CollectInBufferStream write;
		
		// Save this level
		SaveLevel(write);
	
		// Store in file
		write.SetForReading();
		FileStream file(rFilename.c_str(), O_WRONLY | O_CREAT);
		write.CopyStreamTo(file, IOStream::TimeOutInfinite, 8*1024 /* large buffer */);
	}

	void SaveLevel(IOStream &rWrite) const
	{
		// Write the restored objects
		int64_t numObjects = mRestoredObjects.size();
		rWrite.Write(&numObjects, sizeof(numObjects));
		for(std::set<int64_t>::const_iterator i(mRestoredObjects.begin()); i != mRestoredObjects.end(); ++i)
		{
			int64_t id = *i;
			rWrite.Write(&id, sizeof(id));
		}
		
		// Next level?
		if(mpNextLevel != 0)
		{
			// ID
			rWrite.Write(&mNextLevelID, sizeof(mNextLevelID));
			// Name string
			int32_t nsize = mNextLevelLocalName.size();
			rWrite.Write(&nsize, sizeof(nsize));
			rWrite.Write(mNextLevelLocalName.c_str(), nsize);
			// And then the level itself
			mpNextLevel->SaveLevel(rWrite);
		}
		else
		{
			// Just write a zero
			int64_t zero = 0;
			rWrite.Write(&zero, sizeof(zero));
		}
	}

	// Not written to be efficient -- shouldn't be called very often.
	bool Load(const std::string &rFilename)
	{
		// Delete and reset if necessary
		if(mpNextLevel != 0)
		{
			RemoveLevel();
		}
		
		// Open file
		FileStream file(rFilename.c_str());
		
		// Load this level
		return LoadLevel(file);
	}
	
	#define CHECKED_READ(x, s) if(!rRead.ReadFullBuffer(x, s, 0)) {return false;}
	bool LoadLevel(IOStream &rRead)
	{
		// Load the restored objects list
		mRestoredObjects.clear();
		int64_t numObjects = 0;
		CHECKED_READ(&numObjects, sizeof(numObjects));
		for(int64_t o = 0; o < numObjects; ++o)
		{
			int64_t id;
			CHECKED_READ(&id, sizeof(id));
			mRestoredObjects.insert(id);
		}

		// ID of next level?
		int64_t nextID = 0;
		CHECKED_READ(&nextID, sizeof(nextID));
		if(nextID != 0)
		{
			// Load the next level!
			std::string name;
			int32_t nsize = 0;
			CHECKED_READ(&nsize, sizeof(nsize));
			char n[PATH_MAX];
			if(nsize > PATH_MAX) return false;
			CHECKED_READ(n, nsize);
			name.assign(n, nsize);
			
			// Create a new level
			mpNextLevel = new RestoreResumeInfo;
			mNextLevelID = nextID;
			mNextLevelLocalName = name;
			
			// And ask it to read itself in
			if(!mpNextLevel->LoadLevel(rRead))
			{
				return false;
			}
		}

		return true;
	}

	// List of objects at this level which have been done already
	std::set<int64_t> mRestoredObjects;
	// Next level ID
	int64_t mNextLevelID;
	// Pointer to next level
	RestoreResumeInfo *mpNextLevel;
	// Local filename of next level
	std::string mNextLevelLocalName;
};

// parameters structure
typedef struct
{
	bool PrintDots;
	bool RestoreDeleted;
	std::string mRestoreResumeInfoFilename;
	RestoreResumeInfo mResumeInfo;
} RestoreParams;



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientRestoreDir(BackupProtocolClient &, int64_t, const char *, bool)
//		Purpose: Restore a directory
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
static int BackupClientRestoreDir(BackupProtocolClient &rConnection, int64_t DirectoryID, std::string &rLocalDirectoryName,
	RestoreParams &Params, RestoreResumeInfo &rLevel)
{
	// If we're resuming... check that we haven't got a next level to look at
	if(rLevel.mpNextLevel != 0)
	{
		// Recurse immediately
		std::string localDirname(rLocalDirectoryName + DIRECTORY_SEPARATOR_ASCHAR + rLevel.mNextLevelLocalName);
		BackupClientRestoreDir(rConnection, rLevel.mNextLevelID, localDirname, Params, *rLevel.mpNextLevel);
		
		// Add it to the list of done itmes
		rLevel.mRestoredObjects.insert(rLevel.mNextLevelID);

		// Remove the level for the recursed directory
		rLevel.RemoveLevel();		
	}
	
	// Create the local directory, if not already done.
	// Path and owner set later, just use restrictive owner mode.

	int exists;

	try
	{
		exists = ObjectExists(rLocalDirectoryName.c_str());
	}
	catch (BoxException &e)
	{
		BOX_ERROR("Failed to check existence for " <<
			rLocalDirectoryName << ": " << e.what());
		return Restore_UnknownError;
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to check existence for " <<
			rLocalDirectoryName << ": " << e.what());
		return Restore_UnknownError;
	}
	catch(...)
	{
		BOX_ERROR("Failed to check existence for " <<
			rLocalDirectoryName << ": unknown error");
		return Restore_UnknownError;
	}

	switch(exists)
	{
		case ObjectExists_Dir:
			// Do nothing
			break;
		case ObjectExists_File:
			{
				// File exists with this name, which is fun. Get rid of it.
				BOX_WARNING("File present with name '" <<
					rLocalDirectoryName << "', removing " <<
					"out of the way of restored directory. "
					"Use specific restore with ID to "
					"restore this object.");
				if(::unlink(rLocalDirectoryName.c_str()) != 0)
				{
					BOX_LOG_SYS_ERROR("Failed to delete "
						"file '" << 
						rLocalDirectoryName << "'");
					return Restore_UnknownError;
				}
				BOX_TRACE("In restore, directory name " 
					"collision with file '" <<
					rLocalDirectoryName << "'");
			}
			break;
		case ObjectExists_NoObject:
			// we'll create it in a second, after checking
			// whether the parent directory exists
			break;
		default:
			ASSERT(false);
			break;
	}

	std::string parentDirectoryName(rLocalDirectoryName);
	if(parentDirectoryName[parentDirectoryName.size() - 1] == 
		DIRECTORY_SEPARATOR_ASCHAR)
	{
		parentDirectoryName.resize(parentDirectoryName.size() - 1);
	}

	size_t lastSlash = parentDirectoryName.rfind(DIRECTORY_SEPARATOR_ASCHAR);

	if(lastSlash == std::string::npos)
	{
		// might be a forward slash separator, 
		// especially in the unit tests!
		lastSlash = parentDirectoryName.rfind('/');
	}

	if(lastSlash != std::string::npos)
	{
		// the target directory is a deep path, remove the last
		// directory name and check that the resulting parent
		// exists, otherwise the restore should fail.
		parentDirectoryName.resize(lastSlash);

		#ifdef WIN32
			// if the path is a drive letter, then we need to
			// add a a backslash to query the root directory.
			if (lastSlash == 2 && parentDirectoryName[1] == ':')
			{
				parentDirectoryName += '\\';
			}
			else if (lastSlash == 0)
			{
				parentDirectoryName += '\\';
			}
		#endif

		int parentExists;

		try
		{
			parentExists = ObjectExists(parentDirectoryName.c_str());
		}
		catch (BoxException &e)
		{
			BOX_ERROR("Failed to check existence for " <<
				parentDirectoryName << ": " << e.what());
			return Restore_UnknownError;
		}
		catch(std::exception &e)
		{
			BOX_ERROR("Failed to check existence for " <<
				parentDirectoryName << ": " << e.what());
			return Restore_UnknownError;
		}
		catch(...)
		{
			BOX_ERROR("Failed to check existence for " <<
				parentDirectoryName << ": unknown error");
			return Restore_UnknownError;
		}

		switch(parentExists)
		{
			case ObjectExists_Dir:
				// this is fine, do nothing
				break;

			case ObjectExists_File:
				BOX_ERROR("Failed to restore: '" <<
					parentDirectoryName << "' "
					"is a file, but should be a "
					"directory.");
				return Restore_TargetPathNotFound;

			case ObjectExists_NoObject:
				BOX_ERROR("Failed to restore: parent '" <<
					parentDirectoryName << "' of target "
					"directory does not exist.");
				return Restore_TargetPathNotFound;

			default:
				BOX_ERROR("Failed to restore: unknown "
					"result from ObjectExists('" <<
					parentDirectoryName << "')");
				return Restore_UnknownError;
		}
	}

	if((exists == ObjectExists_NoObject ||
		exists == ObjectExists_File) && 
		::mkdir(rLocalDirectoryName.c_str(), S_IRWXU) != 0)
	{
		BOX_LOG_SYS_ERROR("Failed to create directory '" <<
			rLocalDirectoryName << "'");
		return Restore_UnknownError;
	}

	// Save the restore info, in case it's needed later
	try
	{
		Params.mResumeInfo.Save(Params.mRestoreResumeInfoFilename);
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to save resume info file '" <<
			Params.mRestoreResumeInfoFilename << "': " <<
			e.what());
		return Restore_UnknownError;
	}
	catch(...)
	{
		BOX_ERROR("Failed to save resume info file '" <<
			Params.mRestoreResumeInfoFilename <<
			"': unknown error");
		return Restore_UnknownError;
	}

	// Fetch the directory listing from the server -- getting a
	// list of files which is appropriate to the restore type
	rConnection.QueryListDirectory(
			DirectoryID,
			Params.RestoreDeleted?(BackupProtocolClientListDirectory::Flags_Deleted):(BackupProtocolClientListDirectory::Flags_INCLUDE_EVERYTHING),
			BackupProtocolClientListDirectory::Flags_OldVersion | (Params.RestoreDeleted?(0):(BackupProtocolClientListDirectory::Flags_Deleted)),
			true /* want attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(rConnection.ReceiveStream());
	dir.ReadFromStream(*dirstream, rConnection.GetTimeout());

	// Apply attributes to the directory
	const StreamableMemBlock &dirAttrBlock(dir.GetAttributes());
	BackupClientFileAttributes dirAttr(dirAttrBlock);

	try
	{
		dirAttr.WriteAttributes(rLocalDirectoryName.c_str(), true);
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to restore attributes for '" <<
			rLocalDirectoryName << "': " << e.what());
		return Restore_UnknownError;
	}
	catch(...)
	{
		BOX_ERROR("Failed to restore attributes for '" <<
			rLocalDirectoryName << "': unknown error");
		return Restore_UnknownError;
	}

	int64_t bytesWrittenSinceLastRestoreInfoSave = 0;
	
	// Process files
	{
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next(BackupStoreDirectory::Entry::Flags_File)) != 0)
		{
			// Check ID hasn't already been done
			if(rLevel.mRestoredObjects.find(en->GetObjectID()) == rLevel.mRestoredObjects.end())
			{
				// Local name
				BackupStoreFilenameClear nm(en->GetName());
				std::string localFilename(rLocalDirectoryName +
					DIRECTORY_SEPARATOR_ASCHAR +
					nm.GetClearFilename());
				
				// Unlink anything which already exists:
				// For resuming restores, we can't overwrite
				// files already there.
				if(ObjectExists(localFilename) != ObjectExists_NoObject &&
					::unlink(localFilename.c_str()) != 0)
				{
					BOX_LOG_SYS_ERROR("Failed to delete "
						"file '" << localFilename << 
						"'");
					return Restore_UnknownError;
				}
				
				// Request it from the store
				rConnection.QueryGetFile(DirectoryID,
					en->GetObjectID());
		
				// Stream containing encoded file
				std::auto_ptr<IOStream> objectStream(
					rConnection.ReceiveStream());
		
				// Decode the file -- need to do different
				// things depending on whether the directory
				// entry has additional attributes
				try
				{
					if(en->HasAttributes())
					{
						// Use these attributes
						const StreamableMemBlock &storeAttr(en->GetAttributes());
						BackupClientFileAttributes attr(storeAttr);
						BackupStoreFile::DecodeFile(*objectStream, localFilename.c_str(), rConnection.GetTimeout(), &attr);
					}
					else
					{
						// Use attributes stored in file
						BackupStoreFile::DecodeFile(*objectStream, localFilename.c_str(), rConnection.GetTimeout());
					}
				}
				catch(std::exception &e)
				{
					BOX_ERROR("Failed to restore file '" <<
						localFilename << "': " <<
						e.what());
					return Restore_UnknownError;
				}
				catch(...)
				{
					BOX_ERROR("Failed to restore file '" <<
						localFilename <<
						"': unknown error");
					return Restore_UnknownError;
				}
				
				// Progress display?
				if(Params.PrintDots)
				{
					printf(".");
					fflush(stdout);
				}

				// Add it to the list of done itmes
				rLevel.mRestoredObjects.insert(en->GetObjectID());
				
				// Save restore info?
				int64_t fileSize;
				int exists;

				try
				{
					exists = FileExists(
						localFilename.c_str(),
						&fileSize, 
						true /* treat links as not 
							existing */);
				}
				catch(std::exception &e)
				{
					BOX_ERROR("Failed to determine "
						"whether file exists: '" <<
						localFilename << "': " <<
						e.what());
					return Restore_UnknownError;
				}
				catch(...)
				{
					BOX_ERROR("Failed to determine "
						"whether file exists: '" <<
						localFilename << "': "
						"unknown error");
					return Restore_UnknownError;
				}

				if(exists)
				{
					// File exists...
					bytesWrittenSinceLastRestoreInfoSave += fileSize;
					
					if(bytesWrittenSinceLastRestoreInfoSave > MAX_BYTES_WRITTEN_BETWEEN_RESTORE_INFO_SAVES)
					{
						// Save the restore info, in case it's needed later
						try
						{
							Params.mResumeInfo.Save(Params.mRestoreResumeInfoFilename);
						}
						catch(std::exception &e)
						{
							BOX_ERROR("Failed to save resume info file '" <<
								Params.mRestoreResumeInfoFilename <<
								"': " << e.what());
							return Restore_UnknownError;
						}
						catch(...)
						{
							BOX_ERROR("Failed to save resume info file '" <<
								Params.mRestoreResumeInfoFilename <<
								"': unknown error");
							return Restore_UnknownError;
						}

						bytesWrittenSinceLastRestoreInfoSave = 0;
					}
				}
			}
		}
	}

	// Make sure the restore info has been saved	
	if(bytesWrittenSinceLastRestoreInfoSave != 0)
	{
		// Save the restore info, in case it's needed later
		try
		{
			Params.mResumeInfo.Save(
				Params.mRestoreResumeInfoFilename);
		}
		catch(std::exception &e)
		{
			BOX_ERROR("Failed to save resume info file '" <<
				Params.mRestoreResumeInfoFilename << "': " <<
				e.what());
			return Restore_UnknownError;
		}
		catch(...)
		{
			BOX_ERROR("Failed to save resume info file '" <<
				Params.mRestoreResumeInfoFilename <<
				"': unknown error");
			return Restore_UnknownError;
		}
		
		bytesWrittenSinceLastRestoreInfoSave = 0;
	}

	
	// Recurse to directories
	{
		BackupStoreDirectory::Iterator i(dir);
		BackupStoreDirectory::Entry *en = 0;
		while((en = i.Next(BackupStoreDirectory::Entry::Flags_Dir)) != 0)
		{
			// Check ID hasn't already been done
			if(rLevel.mRestoredObjects.find(en->GetObjectID()) == rLevel.mRestoredObjects.end())
			{
				// Local name
				BackupStoreFilenameClear nm(en->GetName());
				std::string localDirname(rLocalDirectoryName + DIRECTORY_SEPARATOR_ASCHAR + nm.GetClearFilename());
				
				// Add the level for the next entry
				RestoreResumeInfo &rnextLevel(rLevel.AddLevel(en->GetObjectID(), nm.GetClearFilename()));
				
				// Recurse
				int result = BackupClientRestoreDir(
					rConnection, en->GetObjectID(), 
					localDirname, Params, rnextLevel);

				if (result != Restore_Complete)
				{
					return result;
				}
				
				// Remove the level for the above call
				rLevel.RemoveLevel();
				
				// Add it to the list of done itmes
				rLevel.mRestoredObjects.insert(en->GetObjectID());
			}
		}
	}

	// now remove the user writable flag, if we added it earlier
	try
	{
		dirAttr.WriteAttributes(rLocalDirectoryName.c_str(), false);
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Failed to restore attributes for '" <<
			rLocalDirectoryName << "': " << e.what());
		return Restore_UnknownError;
	}
	catch(...)
	{
		BOX_ERROR("Failed to restore attributes for '" <<
			rLocalDirectoryName << "': unknown error");
		return Restore_UnknownError;
	}

	return Restore_Complete;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientRestore(BackupProtocolClient &, int64_t, const char *, bool, bool)
//		Purpose: Restore a directory on the server to a local directory on the disc.
//
//				 The local directory must not already exist.
//
//				 If a restore is aborted for any reason, then it may be resumed if
//				 Resume == true. If Resume == false and resumption is possible, then
//				 Restore_ResumePossible is returned.
//
//				 Set RestoreDeleted to restore a deleted directory. This may not give the
//				 directory structure when it was deleted, because files may have been deleted
//				 within it before it was deleted.
//
//				 Returns Restore_TargetExists if the target directory exists, but
//				 there is no restore possible. (Won't attempt to overwrite things.)
//
//				 Returns Restore_Complete on success. (Exceptions on error.)
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
int BackupClientRestore(BackupProtocolClient &rConnection, int64_t DirectoryID, const char *LocalDirectoryName,
	bool PrintDots, bool RestoreDeleted, bool UndeleteAfterRestoreDeleted, bool Resume)
{
	// Parameter block
	RestoreParams params;
	params.PrintDots = PrintDots;
	params.RestoreDeleted = RestoreDeleted;
	params.mRestoreResumeInfoFilename = LocalDirectoryName;
	params.mRestoreResumeInfoFilename += ".boxbackupresume";

	// Target exists?
	int targetExistance = ObjectExists(LocalDirectoryName);

	// Does any resumption information exist?
	bool doingResume = false;
	if(FileExists(params.mRestoreResumeInfoFilename.c_str()) && targetExistance == ObjectExists_Dir)
	{
		if(!Resume)
		{
			// Caller didn't specify that resume should be done, so refuse to do it
			// but say why.
			return Restore_ResumePossible;
		}
		
		// Attempt to load the resume info file
		if(!params.mResumeInfo.Load(params.mRestoreResumeInfoFilename))
		{
			// failed -- bad file, so things have gone a bit wrong
			return Restore_TargetExists;
		}
		
		// Flag as doing resume so next check isn't actually performed
		doingResume = true;
	}

	// Does the directory already exist?
	if(targetExistance != ObjectExists_NoObject && !doingResume)
	{
		// Don't do anything in this case!
		return Restore_TargetExists;
	}
	
	// Restore the directory
	std::string localName(LocalDirectoryName);
	int result = BackupClientRestoreDir(rConnection, DirectoryID, 
		localName, params, params.mResumeInfo);
	if (result != Restore_Complete)
	{
		return result;
	}

	// Undelete the directory on the server?
	if(RestoreDeleted && UndeleteAfterRestoreDeleted)
	{
		// Send the command
		rConnection.QueryUndeleteDirectory(DirectoryID);
	}

	// Finish progress display?
	if(PrintDots)
	{
		printf("\n");
		fflush(stdout);
	}
	
	// Delete the resume information file
	::unlink(params.mRestoreResumeInfoFilename.c_str());

	return Restore_Complete;
}




