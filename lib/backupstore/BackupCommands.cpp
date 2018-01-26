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
#include "CollectInBufferStream.h"
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
//		Name:    BackupProtocolMessage::HandleException(BoxException& e)
//		Purpose: Return an error message appropriate to the passed-in
//		exception, or rethrow it.
//		Created: 2014/09/14
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolReplyable::HandleException(BoxException& e) const
{
	if(e.GetType() == RaidFileException::ExceptionType &&
		e.GetSubType() == RaidFileException::RaidFileDoesntExist)
	{
		return PROTOCOL_ERROR(Err_DoesNotExist);
	}
	else if (e.GetType() == BackupStoreException::ExceptionType)
	{
		// Slightly broken or really broken, both thrown by VerifyStream:
		if(e.GetSubType() == BackupStoreException::AddedFileDoesNotVerify ||
			e.GetSubType() == BackupStoreException::BadBackupStoreFile)
		{
			return PROTOCOL_ERROR(Err_FileDoesNotVerify);
		}
		else if(e.GetSubType() == BackupStoreException::AddedFileExceedsStorageLimit)
		{
			return PROTOCOL_ERROR(Err_StorageLimitExceeded);
		}
		else if(e.GetSubType() == BackupStoreException::MultiplyReferencedObject)
		{
			return PROTOCOL_ERROR(Err_MultiplyReferencedObject);
		}
		else if(e.GetSubType() == BackupStoreException::CouldNotFindEntryInDirectory)
		{
			return PROTOCOL_ERROR(Err_DoesNotExistInDirectory);
		}
		else if(e.GetSubType() == BackupStoreException::NameAlreadyExistsInDirectory)
		{
			return PROTOCOL_ERROR(Err_TargetNameExists);
		}
		else if(e.GetSubType() == BackupStoreException::ObjectDoesNotExist)
		{
			return PROTOCOL_ERROR(Err_DoesNotExist);
		}
		else if(e.GetSubType() == BackupStoreException::PatchChainInfoBadInDirectory)
		{
			return PROTOCOL_ERROR(Err_PatchConsistencyError);
		}
	}

	throw;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolVersion::DoCommand(Protocol &,
//			 BackupStoreContext &)
//		Purpose: Return the current version, or an error if the
//			 requested version isn't allowed
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
	// can be called in any phase

	BOX_NOTICE("Session finished for Client ID " <<
		BOX_FORMAT_ACCOUNT(rContext.GetClientID()) << " "
		"(name=" << rContext.GetAccountName() << ")");

	// Let the context know about it
	rContext.CleanUp();

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

	// Ask the context for a directory
	const BackupStoreDirectory &rdir(
		rContext.GetDirectory(mObjectID));
	rdir.WriteToStream(*stream, mFlagsMustBeSet,
		mFlagsNotToBeSet, mSendAttributes,
		false /* never send dependency info to the client */);

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
std::auto_ptr<BackupProtocolMessage> BackupProtocolStoreFile::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
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

	// Ask the context to store it
	int64_t id = rContext.AddFile(rDataStream, mDirectoryObjectID,
		mModificationTime, mAttributesHash, mDiffFromFileID,
		mFilename,
		true /* mark files with same name as old versions */);

	// Tell the caller what the file ID was
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
		return PROTOCOL_ERROR(Err_DoesNotExist);
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
	std::auto_ptr<IOStream> stream;

	try
	{
		stream = rContext.GetFile(mObjectID, mInDirectory);
	}
	catch(BackupStoreException &e)
	{
		if(EXCEPTION_IS_TYPE(e, BackupStoreException, ObjectDoesNotExist))
		{
			return PROTOCOL_ERROR(Err_DoesNotExist);
		}
		else
		{
			throw;
		}
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
std::auto_ptr<BackupProtocolMessage> BackupProtocolCreateDirectory::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	return BackupProtocolCreateDirectory2(mContainingDirectoryID,
		mAttributesModTime, 0 /* ModificationTime */,
		mDirectoryName).DoCommand(rProtocol, rContext, rDataStream);
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolCreateDirectory2::DoCommand(Protocol &, BackupStoreContext &)
//		Purpose: Create directory command, with a specific
//			 modification time.
//		Created: 2014/02/11
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolCreateDirectory2::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Collect the attributes -- do this now so no matter what the outcome,
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(rDataStream, rProtocol.GetTimeout());

	// Check to see if the hard limit has been exceeded
	if(rContext.HardLimitExceeded())
	{
		// Won't allow creation if the limit has been exceeded
		return PROTOCOL_ERROR(Err_StorageLimitExceeded);
	}

	bool alreadyExists = false;
	int64_t id = rContext.AddDirectory(mContainingDirectoryID,
		mDirectoryName, attr, mAttributesModTime, mModificationTime,
		alreadyExists);

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
std::auto_ptr<BackupProtocolMessage> BackupProtocolChangeDirAttributes::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Collect the attributes -- do this now so no matter what the outcome,
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(rDataStream, rProtocol.GetTimeout());

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
std::auto_ptr<BackupProtocolMessage>
BackupProtocolSetReplacementFileAttributes::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext,
	IOStream& rDataStream) const
{
	CHECK_PHASE(Phase_Commands)
	CHECK_WRITEABLE_SESSION

	// Collect the attributes -- do this now so no matter what the outcome,
	// the data has been absorbed.
	StreamableMemBlock attr;
	attr.Set(rDataStream, rProtocol.GetTimeout());

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
	rContext.DeleteDirectory(mObjectID);

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
	rContext.MoveObject(mObjectID, mMoveFromDirectory, mMoveToDirectory,
		mNewFilename, (mFlags & Flags_MoveAllWithSameName) == Flags_MoveAllWithSameName,
		(mFlags & Flags_AllowMoveOverDeletedObject) == Flags_AllowMoveOverDeletedObject);

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
		const BackupStoreDirectory *pDir;

		try
		{
			pDir = &rContext.GetDirectory(dirID);
		}
		catch(BackupStoreException &e)
		{
			if(e.GetSubType() == BackupStoreException::ObjectDoesNotExist)
			{
				// If this can't be found, then there is a problem...
				// tell the caller it can't be found.
				return std::auto_ptr<BackupProtocolMessage>(
					new BackupProtocolObjectName(
						BackupProtocolObjectName::NumNameElements_ObjectDoesntExist,
						0, 0, 0));
			}

			throw;
		}

		const BackupStoreDirectory& rdir(*pDir);

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

	// Return info
	return std::auto_ptr<BackupProtocolMessage>(new BackupProtocolAccountUsage(
		rinfo.GetBlocksUsed(),
		rinfo.GetBlocksInOldFiles(),
		rinfo.GetBlocksInDeletedFiles(),
		rinfo.GetBlocksInDirectories(),
		rinfo.GetBlocksSoftLimit(),
		rinfo.GetBlocksHardLimit(),
		rContext.GetBlockSize()
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

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupProtocolGetAccountUsage2::DoCommand(BackupProtocolReplyable &, BackupStoreContext &)
//		Purpose: Return the amount of disc space used
//		Created: 26/12/13
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupProtocolMessage> BackupProtocolGetAccountUsage2::DoCommand(
	BackupProtocolReplyable &rProtocol, BackupStoreContext &rContext) const
{
	CHECK_PHASE(Phase_Commands)

	// Get store info from context
	const BackupStoreInfo &info(rContext.GetBackupStoreInfo());

	// Return info
	BackupProtocolAccountUsage2* usage = new BackupProtocolAccountUsage2();
	std::auto_ptr<BackupProtocolMessage> reply(usage);
	#define COPY(name) usage->Set ## name (info.Get ## name ())
	COPY(AccountName);
	usage->SetAccountEnabled(info.IsAccountEnabled());
	COPY(ClientStoreMarker);
	usage->SetBlockSize(rContext.GetBlockSize());
	COPY(LastObjectIDUsed);
	COPY(BlocksUsed);
	COPY(BlocksInCurrentFiles);
	COPY(BlocksInOldFiles);
	COPY(BlocksInDeletedFiles);
	COPY(BlocksInDirectories);
	COPY(BlocksSoftLimit);
	COPY(BlocksHardLimit);
	COPY(NumCurrentFiles);
	COPY(NumOldFiles);
	COPY(NumDeletedFiles);
	COPY(NumDirectories);
	#undef COPY

	return reply;
}
