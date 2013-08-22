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

#include "autogen_BackupProtocol.h"
#include "autogen_RaidFileException.h"
#include "BackupConstants.h"
#include "BackupStoreContext.h"
#include "BackupStoreConstants.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreFile.h"
#include "BackupStoreInfo.h"
#include "BufferedStream.h"
#include "CollectInBufferStream.h"
#include "FileStream.h"
#include "InvisibleTempFileStream.h"
#include "RaidFileController.h"
#include "StreamableMemBlock.h"

#include "MemLeakFindOn.h"

#define PROTOCOL_ERROR(code) \
	std::auto_ptr<BackupProtocolMessage>(new BackupProtocolError( \
		BackupProtocolError::ErrorType, \
		BackupProtocolError::code));

#define CHECK_PHASE(phase) \
	if(rContext.GetPhase() != BackupStoreContext::phase) \
	{ \
		BOX_ERROR("Received command " << ToString() << " " \
			"in wrong protocol phase " << rContext.GetPhaseName() << ", " \
			"expected in " #phase); \
		return PROTOCOL_ERROR(Err_NotInRightProtocolPhase); \
	}

#define CHECK_WRITEABLE_SESSION \
	if(rContext.SessionIsReadOnly()) \
	{ \
		BOX_ERROR("Received command " << ToString() << " " \
			"in a read-only session"); \
		return PROTOCOL_ERROR(Err_SessionReadOnly); \
	}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolVersion::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Return the current version, or an error if the requested version isn't allowed
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolVersion::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Version)

	// Correct version?
	if(mVersion != BACKUP_STORE_SERVER_VERSION)
	{
		return PROTOCOL_ERROR(Err_WrongVersion);
	}

	// Mark the next phase
	rContext.SetPhase(BackupStoreContext::Phase_Login);

	// Return our version
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolVersion(BACKUP_STORE_SERVER_VERSION));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolLogin::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Return the current version, or an error if the requested version isn't allowed
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolLogin::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Login)

	// Check given client ID against the ID in the certificate certificate
	// and that the client actually has an account on this machine
	if(mClientID != rContext.GetClientID())
	{
		BOX_WARNING("Failed login from client ID " << 
			BOX_FORMAT_ACCOUNT(mClientID) << ": "
			"wrong certificate for this account");
		return PROTOCOL_ERROR(Err_BadLogin);
	}

	if(!rContext.GetClientHasAccount())
	{
		BOX_WARNING("Failed login from client ID " << 
			BOX_FORMAT_ACCOUNT(mClientID) << ": "
			"no such account on this server");
		return PROTOCOL_ERROR(Err_BadLogin);
	}

	// If we need to write, check that nothing else has got a write lock
	if((mFlags & Flags_ReadOnly) != Flags_ReadOnly)
	{
		// See if the context will get the lock
		if(!rContext.AttemptToGetWriteLock())
		{
			BOX_WARNING("Failed to get write lock for Client ID " <<
				BOX_FORMAT_ACCOUNT(mClientID));
			return PROTOCOL_ERROR(Err_CannotLockStoreForWriting);
		}
		
		// Debug: check we got the lock
		ASSERT(!rContext.SessionIsReadOnly());
	}
	
	// Load the store info
	rContext.LoadStoreInfo();

	if(!rContext.GetBackupStoreInfo().IsAccountEnabled())
	{
		BOX_WARNING("Refused login from disabled client ID " << 
			BOX_FORMAT_ACCOUNT(mClientID));
		return PROTOCOL_ERROR(Err_DisabledAccount);
	}

	// Get the last client store marker
	int64_t clientStoreMarker = rContext.GetClientStoreMarker();

	// Mark the next phase
	rContext.SetPhase(BackupStoreContext::Phase_Commands);
	
	// Log login
	BOX_NOTICE("Login from Client ID " << 
		BOX_FORMAT_ACCOUNT(mClientID) << " "
		"(name=" << rContext.GetAccountName() << "): " <<
		(((mFlags & Flags_ReadOnly) != Flags_ReadOnly)
			?"Read/Write":"Read-only") << " from " <<
		rContext.GetConnectionDetails());

	// Get the usage info for reporting to the client
	int64_t blocksUsed = 0, blocksSoftLimit = 0, blocksHardLimit = 0;
	rContext.GetStoreDiscUsageInfo(blocksUsed, blocksSoftLimit, blocksHardLimit);

	// Return success
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolLoginConfirmed(clientStoreMarker, blocksUsed, blocksSoftLimit, blocksHardLimit));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolFinished::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Marks end of conversation (Protocol framework handles this)
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolFinished::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	BOX_NOTICE("Session finished for Client ID " << 
		BOX_FORMAT_ACCOUNT(rContext.GetClientID()) << " "
		"(name=" << rContext.GetAccountName() << ")");

	// Let the context know about it
	rContext.ReceivedFinishCommand();

	// can be called in any phase
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolFinished);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolListDirectory::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to list a directory
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolListDirectory::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Store the listing to a stream
	std::auto_ptr<CollectInBufferStream> stream(new CollectInBufferStream);

	try
	{
		// Ask the context for a directory
		const BackupStoreDirectory &rdir(
			rContext.GetDirectory(mObjectID));
		rdir.WriteToStream(*stream, mFlagsMustBeSet, 
			mFlagsNotToBeSet, mSendAttributes,
			false /* never send dependency info to the client */);
	}
	catch (RaidFileException &e)
	{
		if (e.GetSubType() == RaidFileException::RaidFileDoesntExist)
		{
			return PROTOCOL_ERROR(Err_DoesNotExist);
		}
		throw;
	}

	stream->SetForReading();
	
	// Get the protocol to send the stream
	rProtocol.SendStreamAfterCommand(static_cast< std::auto_ptr<IOStream> > (stream));

	return std::auto_ptr<BackupProtocolMessage>(
		new BackupProtocolSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolStoreFile::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to store a file on the server
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolStoreFile::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	std::auto_ptr<BackupProtocolMessage> hookResult =
		rContext.StartCommandHook(*this);
	if(hookResult.get())
	{
		return hookResult;
	}
	
	// Check that the diff from file actually exists, if it's specified
	if(mDiffFromFileID != 0)
	{
		if(!rContext.ObjectExists(mDiffFromFileID,
			BackupStoreContext::ObjectExists_File))
		{
			return PROTOCOL_ERROR(Err_DiffFromFileDoesNotExist);
		}
	}
	
	// A stream follows, which contains the file
	std::auto_ptr<IOStream> dirstream(rProtocol.ReceiveStream());
	
	// Ask the context to store it
	int64_t id = 0;
	try
	{
		id = rContext.AddFile(*dirstream, mDirectoryObjectID,
			mModificationTime, mAttributesHash, mDiffFromFileID,
			mFilename,
			true /* mark files with same name as old versions */);
	}
	catch(BackupStoreException &e)
	{
		if(e.GetSubType() == BackupStoreException::AddedFileDoesNotVerify)
		{
			return PROTOCOL_ERROR(Err_FileDoesNotVerify);
		}
		else if(e.GetSubType() == BackupStoreException::AddedFileExceedsStorageLimit)
		{
			return PROTOCOL_ERROR(Err_StorageLimitExceeded);
		}
		else
		{
			throw;
		}
	}
	
	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(id));
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetObject::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to get an arbitary object from the server
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetObject::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Check the object exists
	if(!rContext.ObjectExists(mObjectID))
	{
		return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(NoObject));
	}

	// Open the object
	std::auto_ptr<IOStream> object(rContext.OpenObject(mObjectID));

	// Stream it to the peer
	rProtocol.SendStreamAfterCommand(object);

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetFile::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Command to get an file object from the server -- may have to do a bit of 
//				 work to get the object.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetFile::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Check the objects exist
	if(!rContext.ObjectExists(mObjectID)
		|| !rContext.ObjectExists(mInDirectory))
	{
		return PROTOCOL_ERROR(Err_DoesNotExist);
	}

	// Get the directory it's in
	const BackupStoreDirectory &rdir(rContext.GetDirectory(mInDirectory));

	// Find the object within the directory
	BackupStoreDirectory::Entry *pfileEntry = rdir.FindEntryByID(mObjectID);
	if(pfileEntry == 0)
	{
		return PROTOCOL_ERROR(Err_DoesNotExistInDirectory);
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
				return PROTOCOL_ERROR(Err_PatchConsistencyError);
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
			std::ostringstream fs;
			fs << rContext.GetStoreRoot() << ".recombinetemp." << p;
			std::string tempFn = 
				RaidFileController::DiscSetPathToFileSystemPath(
					rContext.GetStoreDiscSet(), fs.str(),
					p + 16);
			
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
			return PROTOCOL_ERROR(Err_FileDoesNotVerify);
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
	rProtocol.SendStreamAfterCommand(stream);
	
	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolCreateDirectory::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Create directory command
//		Created: 2003/09/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolCreateDirectory::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
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
		return PROTOCOL_ERROR(Err_StorageLimitExceeded);
	}

	bool alreadyExists = false;
	int64_t id = rContext.AddDirectory(mContainingDirectoryID, mDirectoryName, attr, mAttributesModTime, alreadyExists);
	
	if(alreadyExists)
	{
		return PROTOCOL_ERROR(Err_DirectoryAlreadyExists);
	}

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(id));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolChangeDirAttributes::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Change attributes on directory
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolChangeDirAttributes::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
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
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolSetReplacementFileAttributes::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Change attributes on directory
//		Created: 2003/09/06
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolSetReplacementFileAttributes::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
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
		return PROTOCOL_ERROR(Err_DoesNotExist);
	}

	// Tell the caller what the file was
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(objectID));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolDeleteFile::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Delete a file
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolDeleteFile::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Context handles this
	int64_t objectID = 0;
	rContext.DeleteFile(mFilename, mInDirectory, objectID);

	// return the object ID or zero for not found
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolUndeleteFile::DoCommand(
//			 BackupProtocolBase &, BackupStoreContext &)
//		Purpose: Undelete a file
//		Created: 2008-09-12
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolUndeleteFile::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Context handles this
	bool result = rContext.UndeleteFile(mObjectID, mInDirectory);

	// return the object ID or zero for not found
	return std::auto_ptr<BackupProtocolMessage>(
		new BackupProtocolSuccess(result ? mObjectID : 0));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolDeleteDirectory::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Delete a directory
//		Created: 2003/10/21
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolDeleteDirectory::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Check it's not asking for the root directory to be deleted
	if(mObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return PROTOCOL_ERROR(Err_CannotDeleteRoot);
	}

	// Context handles this
	try
	{
		rContext.DeleteDirectory(mObjectID);
	}
	catch (BackupStoreException &e)
	{
		if(e.GetSubType() == BackupStoreException::MultiplyReferencedObject)
		{
			return PROTOCOL_ERROR(Err_MultiplyReferencedObject);
		}
		
		throw;
	}

	// return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolUndeleteDirectory::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Undelete a directory
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolUndeleteDirectory::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Check it's not asking for the root directory to be deleted
	if(mObjectID == BACKUPSTORE_ROOT_DIRECTORY_ID)
	{
		return PROTOCOL_ERROR(Err_CannotDeleteRoot);
	}

	// Context handles this
	rContext.DeleteDirectory(mObjectID, true /* undelete */);

	// return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolSetClientStoreMarker::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to set the client's store marker
//		Created: 2003/10/29
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolSetClientStoreMarker::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Set the marker
	rContext.SetClientStoreMarker(mClientStoreMarker);

	// return store marker set
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mClientStoreMarker));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolMoveObject::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to move an object from one directory to another
//		Created: 2003/11/12
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolMoveObject::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
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
			return PROTOCOL_ERROR(Err_DoesNotExist);
		}
		else if(e.GetSubType() == BackupStoreException::NameAlreadyExistsInDirectory)
		{
			return PROTOCOL_ERROR(Err_TargetNameExists);
		}
		else
		{
			throw;
		}
	}

	// Return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetObjectName::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Command to find the name of an object
//		Created: 12/11/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetObjectName::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
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
		if(!rContext.ObjectExists(dirID, BackupStoreContext::ObjectExists_Directory))
		{
			return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName(BackupProtocolObjectName::NumNameElements_ObjectDoesntExist, 0, 0, 0));
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
				return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName(BackupProtocolObjectName::NumNameElements_ObjectDoesntExist, 0, 0, 0));
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
		rProtocol.SendStreamAfterCommand(static_cast< std::auto_ptr<IOStream> >(stream));
	}

	// Make reply
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolObjectName(numNameElements, modTime, attrModHash, objectFlags));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetBlockIndexByID::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Get the block index from a file, by ID
//		Created: 19/1/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetBlockIndexByID::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Open the file
	std::auto_ptr<IOStream> stream(rContext.OpenObject(mObjectID));
	
	// Move the file pointer to the block index
	BackupStoreFile::MoveStreamPositionToBlockIndex(*stream);
	
	// Return the stream to the client
	rProtocol.SendStreamAfterCommand(stream);

	// Return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(mObjectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetBlockIndexByName::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Get the block index from a file, by name within a directory
//		Created: 19/1/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetBlockIndexByName::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
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
		return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(0));
	}

	// Open the file
	std::auto_ptr<IOStream> stream(rContext.OpenObject(objectID));
	
	// Move the file pointer to the block index
	BackupStoreFile::MoveStreamPositionToBlockIndex(*stream);
	
	// Return the stream to the client
	rProtocol.SendStreamAfterCommand(stream);

	// Return the object ID
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolSuccess(objectID));
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetAccountUsage::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Return the amount of disc space used
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetAccountUsage::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Get store info from context
	const BackupStoreInfo &rinfo(rContext.GetBackupStoreInfo());
	
	// Find block size
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet &rdiscSet(rcontroller.GetDiscSet(rinfo.GetDiscSetNumber()));
	
	// Return info
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolAccountUsage(
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
//		Name:    BackupProtocolGetIsAlive::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Return the amount of disc space used
//		Created: 19/4/04
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetIsAlive::DoCommand(BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	//
	// NOOP
	//
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolIsAlive());
}
