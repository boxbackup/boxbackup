// --------------------------------------------------------------------------
//
// File
//		Name:    BackupCommands.cpp
//		Purpose: Implement commands for the Backup store protocol
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <set>
#include <sstream>

#include "autogen_BackupProtocolServer.h"
#include "BackupConstants.h"
#include "BackupContext.h"
#include "CollectInBufferStream.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "StreamableMemBlock.h"
#include "BackupStoreConstants.h"
#include "RaidFileController.h"
#include "BackupStoreInfo.h"
#include "RaidFileController.h"
#include "FileStream.h"
#include "InvisibleTempFileStream.h"
#include "BufferedStream.h"

#include "MemLeakFindOn.h"

#define CHECK_PHASE(phase)																						\
	if(rContext.GetPhase() != BackupContext::phase)																\
	{																											\
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(										\
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_NotInRightProtocolPhase));		\
	}

#define CHECK_WRITEABLE_SESSION																					\
	if(rContext.SessionIsReadOnly())																			\
	{																											\
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(										\
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_SessionReadOnly));				\
	}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerVersion::DoCommand(Protocol &, BackupContext &)
//		Purpose: Return the current version, or an error if the requested version isn't allowed
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerVersion::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Version)

	// Correct version?
	if(mVersion != BACKUP_STORE_SERVER_VERSION)
	{
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_WrongVersion));
	}

	// Mark the next phase
	rContext.SetPhase(BackupContext::Phase_Login);

	// Return our version
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerVersion(BACKUP_STORE_SERVER_VERSION));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerLogin::DoCommand(Protocol &, BackupContext &)
//		Purpose: Return the current version, or an error if the requested version isn't allowed
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerLogin::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Login)

	// Check given client ID against the ID in the certificate certificate
	// and that the client actually has an account on this machine
	if(mClientID != rContext.GetClientID())
	{
		BOX_WARNING("Failed login from client ID " << 
			BOX_FORMAT_ACCOUNT(mClientID) <<
			": wrong certificate for this account");
		return std::auto_ptr<ProtocolObject>(
			new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType,
				BackupProtocolServerError::Err_BadLogin));
	}

	if(!rContext.GetClientHasAccount())
	{
		BOX_WARNING("Failed login from client ID " << 
			BOX_FORMAT_ACCOUNT(mClientID) <<
			": no such account on this server");
		return std::auto_ptr<ProtocolObject>(
			new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType,
				BackupProtocolServerError::Err_BadLogin));
	}

	// If we need to write, check that nothing else has got a write lock
	if((mFlags & Flags_ReadOnly) != Flags_ReadOnly)
	{
		// See if the context will get the lock
		if(!rContext.AttemptToGetWriteLock())
		{
			BOX_WARNING("Failed to get write lock for Client ID " <<
				BOX_FORMAT_ACCOUNT(mClientID));
			return std::auto_ptr<ProtocolObject>(
				new BackupProtocolServerError(
					BackupProtocolServerError::ErrorType,
					BackupProtocolServerError::Err_CannotLockStoreForWriting));			
		}
		
		// Debug: check we got the lock
		ASSERT(!rContext.SessionIsReadOnly());
	}
	
	// Load the store info
	rContext.LoadStoreInfo();

	// Get the last client store marker
	int64_t clientStoreMarker = rContext.GetClientStoreMarker();

	// Mark the next phase
	rContext.SetPhase(BackupContext::Phase_Commands);
	
	// Log login
	BOX_NOTICE("Login from Client ID " << 
		BOX_FORMAT_ACCOUNT(mClientID) <<
		" " <<
		(((mFlags & Flags_ReadOnly) != Flags_ReadOnly)
		?"Read/Write":"Read-only"));

	// Get the usage info for reporting to the client
	int64_t blocksUsed = 0, blocksSoftLimit = 0, blocksHardLimit = 0;
	rContext.GetStoreDiscUsageInfo(blocksUsed, blocksSoftLimit, blocksHardLimit);

	// Return success
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerLoginConfirmed(clientStoreMarker, blocksUsed, blocksSoftLimit, blocksHardLimit));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerFinished::DoCommand(Protocol &, BackupContext &)
//		Purpose: Marks end of conversation (Protocol framework handles this)
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerFinished::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	BOX_NOTICE("Session finished for Client ID " << 
		BOX_FORMAT_ACCOUNT(rContext.GetClientID()));

	// Let the context know about it
	rContext.ReceivedFinishCommand();

	// can be called in any phase
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerFinished);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerListDirectory::DoCommand(Protocol &, BackupContext &)
//		Purpose: Command to list a directory
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerListDirectory::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)

	// Ask the context for a directory
	const BackupStoreDirectory &rdir(rContext.GetDirectory(mObjectID));
	
	// Store the listing to a stream
	std::auto_ptr<CollectInBufferStream> stream(new CollectInBufferStream);
	rdir.WriteToStream(*stream, mFlagsMustBeSet, mFlagsNotToBeSet, mSendAttributes,
		false /* never send dependency info to the client */);
	stream->SetForReading();
	
	// Get the protocol to send the stream
	rProtocol.SendStreamAfterCommand(stream.release());

	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerStoreFile::DoCommand(Protocol &, BackupContext &)
//		Purpose: Command to store a file on the server
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerStoreFile::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION
	
	// Check that the diff from file actually exists, if it's specified
	if(mDiffFromFileID != 0)
	{
		if(!rContext.ObjectExists(mDiffFromFileID, BackupContext::ObjectExists_File))
		{
			return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_DiffFromFileDoesNotExist));	
		}
	}
	
	// A stream follows, which contains the file
	std::auto_ptr<IOStream> dirstream(rProtocol.ReceiveStream());
	
	// Ask the context to store it
	int64_t id = 0;
	try
	{
		id = rContext.AddFile(*dirstream, mDirectoryObjectID, mModificationTime, mAttributesHash, mDiffFromFileID,
			 mFilename, true /* mark files with same name as old versions */);
	}
	catch(BackupStoreException &e)
	{
		if(e.GetSubType() == BackupStoreException::AddedFileDoesNotVerify)
		{
			return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_FileDoesNotVerify));			
		}
		else if(e.GetSubType() == BackupStoreException::AddedFileExceedsStorageLimit)
		{
			return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_StorageLimitExceeded));			
		}
		else
		{
			throw;
		}
	}
	
	// Tell the caller what the file was
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(id));
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerGetObject::DoCommand(Protocol &, BackupContext &)
//		Purpose: Command to get an arbitary object from the server
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerGetObject::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)

	// Check the object exists
	if(!rContext.ObjectExists(mObjectID))
	{
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(NoObject));
	}

	// Open the object
	std::auto_ptr<IOStream> object(rContext.OpenObject(mObjectID));

	// Stream it to the peer
	rProtocol.SendStreamAfterCommand(object.release());

	// Tell the caller what the file was
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerGetFile::DoCommand(Protocol &, BackupContext &)
//		Purpose: Command to get an file object from the server -- may have to do a bit of 
//				 work to get the object.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerGetFile::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)

	// Check the objects exist
	if(!rContext.ObjectExists(mObjectID)
		|| !rContext.ObjectExists(mInDirectory))
	{
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_DoesNotExist));			
	}

	// Get the directory it's in
	const BackupStoreDirectory &rdir(rContext.GetDirectory(mInDirectory));

	// Find the object within the directory
	BackupStoreDirectory::Entry *pfileEntry = rdir.FindEntryByID(mObjectID);
	if(pfileEntry == 0)
	{
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_DoesNotExistInDirectory));			
	}

	// The result
	std::auto_ptr<IOStream> stream;

	// Does this depend on anything?
	if(pfileEntry->GetDependsNewer() != 0)
	{
		// File exists, but is a patch from a new version. Generate the older version.
		std::vector<int64_t> patchChain;
		int64_t id = mObjectID;
		BackupStoreDirectory::Entry *en = 0;
		do
		{
			patchChain.push_back(id);
			en = rdir.FindEntryByID(id);
			if(en == 0)
			{
				BOX_ERROR("Object " << 
					BOX_FORMAT_OBJECTID(mObjectID) <<
					" in dir " << 
					BOX_FORMAT_OBJECTID(mInDirectory) <<
					" for account " <<
					BOX_FORMAT_ACCOUNT(rContext.GetClientID()) <<
					" references object " << 
					BOX_FORMAT_OBJECTID(id) <<
					" which does not exist in dir");
				return std::auto_ptr<ProtocolObject>(
					new BackupProtocolServerError(
						BackupProtocolServerError::ErrorType,
						BackupProtocolServerError::Err_PatchConsistencyError));			
			}
			id = en->GetDependsNewer();
		}
		while(en != 0 && id != 0);
		
		// OK! The last entry in the chain is the full file, the others are patches back from it.
		// Open the last one, which is the current from file
		std::auto_ptr<IOStream> from(rContext.OpenObject(patchChain[patchChain.size() - 1]));
		
		// Then, for each patch in the chain, do a combine
		for(int p = ((int)patchChain.size()) - 2; p >= 0; --p)
		{
			// ID of patch
			int64_t patchID = patchChain[p];
			
			// Open it a couple of times
			std::auto_ptr<IOStream> diff(rContext.OpenObject(patchID));
			std::auto_ptr<IOStream> diff2(rContext.OpenObject(patchID));
			
			// Choose a temporary filename for the result of the combination
			std::ostringstream fs(rContext.GetStoreRoot());
			fs << ".recombinetemp.";
			fs << p;
			std::string tempFn(fs.str());
			tempFn = RaidFileController::DiscSetPathToFileSystemPath(rContext.GetStoreDiscSet(), tempFn, p + 16);
			
			// Open the temporary file
			std::auto_ptr<IOStream> combined;
			try
			{
				{
					// Write nastily to allow this to work with gcc 2.x
					std::auto_ptr<IOStream> t(
						new InvisibleTempFileStream(
							tempFn.c_str(), 
							O_RDWR | O_CREAT | 
							O_EXCL | O_BINARY | 
							O_TRUNC));
					combined = t;
				}
			}
			catch(...)
			{
				// Make sure it goes
				::unlink(tempFn.c_str());
				throw;
			}
			
			// Do the combining
			BackupStoreFile::CombineFile(*diff, *diff2, *from, *combined);
			
			// Move to the beginning of the combined file
			combined->Seek(0, IOStream::SeekType_Absolute);
			
			// Then shuffle round for the next go
			if (from.get()) from->Close();
			from = combined;
		}
		
		// Now, from contains a nice file to send to the client. Reorder it
		{
			// Write nastily to allow this to work with gcc 2.x
			std::auto_ptr<IOStream> t(BackupStoreFile::ReorderFileToStreamOrder(from.get(), true /* take ownership */));
			stream = t;
		}
		
		// Release from file to avoid double deletion
		from.release();
	}
	else
	{
		// Simple case: file already exists on disc ready to go
	
		// Open the object
		std::auto_ptr<IOStream> object(rContext.OpenObject(mObjectID));
		BufferedStream buf(*object);
		
		// Verify it
		if(!BackupStoreFile::VerifyEncodedFileFormat(buf))
		{
			return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_FileDoesNotVerify));			
		}
		
		// Reset stream -- seek to beginning
		object->Seek(0, IOStream::SeekType_Absolute);
		
		// Reorder the stream/file into stream order
		{
			// Write nastily to allow this to work with gcc 2.x
			std::auto_ptr<IOStream> t(BackupStoreFile::ReorderFileToStreamOrder(object.get(), true /* take ownership */));
			stream = t;
		}

		// Object will be deleted when the stream is deleted, 
		// so can release the object auto_ptr here to avoid 
		// premature deletion
		object.release();
	}

	// Stream the reordered stream to the peer
	rProtocol.SendStreamAfterCommand(stream.get());
	
	// Don't delete the stream here
	stream.release();

	// Tell the caller what the file was
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerCreateDirectory::DoCommand(Protocol &, BackupContext &)
//		Purpose: Create directory command
//		Created: 2003/09/04
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerCreateDirectory::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION
	
	// Get the stream containing the attributes
	std::auto_ptr<IOStream> attrstream(rProtocol.ReceiveStream());
	// Collect the attributes -- do this now so no matter what the outcome, 
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(*attrstream, rProtocol.GetTimeout());
	
	// Check to see if the hard limit has been exceeded
	if(rContext.HardLimitExceeded())
	{
		// Won't allow creation if the limit has been exceeded
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_StorageLimitExceeded));			
	}

	bool alreadyExists = false;
	int64_t id = rContext.AddDirectory(mContainingDirectoryID, mDirectoryName, attr, mAttributesModTime, alreadyExists);
	
	if(alreadyExists)
	{
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_DirectoryAlreadyExists));			
	}

	// Tell the caller what the file was
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(id));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerChangeDirAttributes::DoCommand(Protocol &, BackupContext &)
//		Purpose: Change attributes on directory
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerChangeDirAttributes::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Get the stream containing the attributes
	std::auto_ptr<IOStream> attrstream(rProtocol.ReceiveStream());
	// Collect the attributes -- do this now so no matter what the outcome, 
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(*attrstream, rProtocol.GetTimeout());

	// Get the context to do it's magic
	rContext.ChangeDirAttributes(mObjectID, attr, mAttributesModTime);

	// Tell the caller what the file was
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerSetReplacementFileAttributes::DoCommand(Protocol &, BackupContext &)
//		Purpose: Change attributes on directory
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerSetReplacementFileAttributes::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Get the stream containing the attributes
	std::auto_ptr<IOStream> attrstream(rProtocol.ReceiveStream());
	// Collect the attributes -- do this now so no matter what the outcome, 
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(*attrstream, rProtocol.GetTimeout());

	// Get the context to do it's magic
	int64_t objectID = 0;
	if(!rContext.ChangeFileAttributes(mFilename, mInDirectory, attr, mAttributesHash, objectID))
	{
		// Didn't exist
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_DoesNotExist));			
	}

	// Tell the caller what the file was
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(objectID));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerDeleteFile::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Delete a file
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerDeleteFile::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Context handles this
	int64_t objectID = 0;
	rContext.DeleteFile(mFilename, mInDirectory, objectID);

	// return the object ID or zero for not found
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerDeleteDirectory::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Delete a directory
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerDeleteDirectory::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Check it's not asking for the root directory to be deleted
	if(mObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_CannotDeleteRoot));			
	}

	// Context handles this
	rContext.DeleteDirectory(mObjectID);

	// return the object ID
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerUndeleteDirectory::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Undelete a directory
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerUndeleteDirectory::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Check it's not asking for the root directory to be deleted
	if(mObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
			BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_CannotDeleteRoot));			
	}

	// Context handles this
	rContext.DeleteDirectory(mObjectID, true /* undelete */);

	// return the object ID
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerSetClientStoreMarker::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Command to set the client's store marker
//		Created: 2003/10/29
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerSetClientStoreMarker::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Set the marker
	rContext.SetClientStoreMarker(mClientStoreMarker);

	// return store marker set
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mClientStoreMarker));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerMoveObject::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Command to move an object from one directory to another
//		Created: 2003/11/12
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerMoveObject::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION
	
	// Let context do this, but modify error reporting on exceptions...
	try
	{
		rContext.MoveObject(mObjectID, mMoveFromDirectory, mMoveToDirectory,
			mNewFilename, (mFlags & Flags_MoveAllWithSameName) == Flags_MoveAllWithSameName,
			(mFlags & Flags_AllowMoveOverDeletedObject) == Flags_AllowMoveOverDeletedObject);
	}
	catch(BackupStoreException &e)
	{
		if(e.GetSubType() == BackupStoreException::CouldNotFindEntryInDirectory)
		{
			return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_DoesNotExist));			
		}
		else if(e.GetSubType() == BackupStoreException::NameAlreadyExistsInDirectory)
		{
			return std::auto_ptr<ProtocolObject>(new BackupProtocolServerError(
				BackupProtocolServerError::ErrorType, BackupProtocolServerError::Err_TargetNameExists));			
		}
		else
		{
			throw;
		}
	}

	// Return the object ID
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerGetObjectName::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Command to find the name of an object
//		Created: 12/11/03
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerGetObjectName::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)
	
	// Create a stream for the list of filenames
	std::auto_ptr<CollectInBufferStream> stream(new CollectInBufferStream);

	// Object and directory IDs
	int64_t objectID = mObjectID;
	int64_t dirID = mContainingDirectoryID;
	
	// Data to return in the reply
	int32_t numNameElements = 0;
	int16_t objectFlags = 0;
	int64_t modTime = 0;
	uint64_t attrModHash = 0;
	bool haveModTimes = false;
	
	do
	{
		// Check the directory really exists
		if(!rContext.ObjectExists(dirID, BackupContext::ObjectExists_Directory))
		{
			return std::auto_ptr<ProtocolObject>(new BackupProtocolServerObjectName(BackupProtocolServerObjectName::NumNameElements_ObjectDoesntExist, 0, 0, 0));
		}

		// Load up the directory
		const BackupStoreDirectory &rdir(rContext.GetDirectory(dirID));

		// Find the element in this directory and store it's name
		if(objectID != ObjectID_DirectoryOnly)
		{
			const BackupStoreDirectory::Entry *en = rdir.FindEntryByID(objectID);

			// If this can't be found, then there is a problem... tell the caller it can't be found
			if(en == 0)
			{
				// Abort!
				return std::auto_ptr<ProtocolObject>(new BackupProtocolServerObjectName(BackupProtocolServerObjectName::NumNameElements_ObjectDoesntExist, 0, 0, 0));
			}
			
			// Store flags?
			if(objectFlags == 0)
			{
				objectFlags = en->GetFlags();
			}
			
			// Store modification times?
			if(!haveModTimes)
			{
				modTime = en->GetModificationTime();
				attrModHash = en->GetAttributesHash();
				haveModTimes = true;
			}
			
			// Store the name in the stream
			en->GetName().WriteToStream(*stream);
			
			// Count of name elements
			++numNameElements;
		}
		
		// Setup for next time round
		objectID = dirID;
		dirID = rdir.GetContainerID();

	} while(objectID != 0 && objectID != BACKUPSTORE_ROOT_DIRECTORY_ID);

	// Stream to send?
	if(numNameElements > 0)
	{
		// Get the stream ready to go
		stream->SetForReading();	
		// Tell the protocol to send the stream
		rProtocol.SendStreamAfterCommand(stream.release());
	}

	// Make reply
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerObjectName(numNameElements, modTime, attrModHash, objectFlags));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerGetBlockIndexByID::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Get the block index from a file, by ID
//		Created: 19/1/04
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerGetBlockIndexByID::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)

	// Open the file
	std::auto_ptr<IOStream> stream(rContext.OpenObject(mObjectID));
	
	// Move the file pointer to the block index
	BackupStoreFile::MoveStreamPositionToBlockIndex(*stream);
	
	// Return the stream to the client
	rProtocol.SendStreamAfterCommand(stream.release());

	// Return the object ID
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerGetBlockIndexByName::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Get the block index from a file, by name within a directory
//		Created: 19/1/04
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerGetBlockIndexByName::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)

	// Get the directory
	const BackupStoreDirectory &dir(rContext.GetDirectory(mInDirectory));
	
	// Find the latest object ID within it which has the same name
	int64_t objectID = 0;
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	while((en = i.Next(BackupStoreDirectory::Entry::Flags_File)) != 0)
	{
		if(en->GetName() == mFilename)
		{
			// Store the ID, if it's a newer ID than the last one
			if(en->GetObjectID() > objectID)
			{
				objectID = en->GetObjectID();
			}
		}
	}
	
	// Found anything?
	if(objectID == 0)
	{
		// No... return a zero object ID
		return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(0));
	}

	// Open the file
	std::auto_ptr<IOStream> stream(rContext.OpenObject(objectID));
	
	// Move the file pointer to the block index
	BackupStoreFile::MoveStreamPositionToBlockIndex(*stream);
	
	// Return the stream to the client
	rProtocol.SendStreamAfterCommand(stream.release());

	// Return the object ID
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerGetAccountUsage::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Return the amount of disc space used
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerGetAccountUsage::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)

	// Get store info from context
	const BackupStoreInfo &rinfo(rContext.GetBackupStoreInfo());
	
	// Find block size
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(rinfo.GetDiscSetNumber()));
	
	// Return info
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerAccountUsage(
		rinfo.GetBlocksUsed(),
		rinfo.GetBlocksInOldFiles(),
		rinfo.GetBlocksInDeletedFiles(),
		rinfo.GetBlocksInDirectories(),
		rinfo.GetBlocksSoftLimit(),
		rinfo.GetBlocksHardLimit(),
		rdiscSet.GetBlockSize()
	));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolServerGetIsAlive::DoCommand(BackupProtocolServer &, BackupContext &)
//		Purpose: Return the amount of disc space used
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> BackupProtocolServerGetIsAlive::DoCommand(BackupProtocolServer &rProtocol, BackupContext &rContext)
{
	CHECK_PHASE(Phase_Commands)

	//
	// NOOP
	//
	return std::auto_ptr<ProtocolObject>(new BackupProtocolServerIsAlive());
}
