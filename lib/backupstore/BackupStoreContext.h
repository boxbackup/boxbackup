// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreContext.h
//		Purpose: Context for backup store server
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BACKUPCONTEXT__H
#define BACKUPCONTEXT__H

#include <string>
#include <map>
#include <memory>

#include "autogen_BackupProtocol.h"
#include "BackupFileSystem.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "Message.h"
#include "Utils.h"

class BackupStoreDirectory;
class BackupStoreFilename;
class IOStream;
class BackupProtocolMessage;
class StreamableMemBlock;

class HousekeepingInterface
{
	public:
	virtual ~HousekeepingInterface() { }
	virtual void SendMessageToHousekeepingProcess(const void *Msg, int MsgLen) = 0;
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreContext
//		Purpose: Context for backup store server
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
class BackupStoreContext
{
public:
	BackupStoreContext(int32_t ClientID,
		HousekeepingInterface* mpHousekeeping,
		const std::string& rConnectionDetails);
	BackupStoreContext(BackupFileSystem& rFileSystem, int32_t ClientID,
		HousekeepingInterface* mpHousekeeping,
		const std::string& rConnectionDetails);
	virtual ~BackupStoreContext();

private:
	BackupStoreContext(const BackupStoreContext &rToCopy); // no copying

public:
	void CleanUp();
	int32_t GetClientID() {return mClientID;}

	enum
	{
		Phase_START    = 0,
		Phase_Version  = 0,
		Phase_Login    = 1,
		Phase_Commands = 2
	};

	int GetPhase() const {return mProtocolPhase;}
	std::string GetPhaseName() const
	{
		switch(mProtocolPhase)
		{
			case Phase_Version:  return "Phase_Version";
			case Phase_Login:    return "Phase_Login";
			case Phase_Commands: return "Phase_Commands";
			default:
				std::ostringstream oss;
				oss << "Unknown phase " << mProtocolPhase;
				return oss.str();
		}
	}
	void SetPhase(int NewPhase) {mProtocolPhase = NewPhase;}

	// Read only locking
	bool SessionIsReadOnly() {return mReadOnly;}
	bool AttemptToGetWriteLock();

	// Not really an API, but useful for BackupProtocolLocal2.
	void ReleaseWriteLock()
	{
		// Even a read-only filesystem may hold some locks, for example
		// S3BackupFileSystem's cache lock, so we always notify the filesystem
		// to release any locks that it holds, even if we are read-only.
		mpFileSystem->ReleaseLock();
	}

	// TODO: stop using this version, which has the side-effect of creating a
	// BackupStoreFileSystem:
	void SetClientHasAccount(const std::string &rStoreRoot, int StoreDiscSet);
	void SetClientHasAccount()
	{
		mClientHasAccount = true;
	}
	bool GetClientHasAccount() const {return mClientHasAccount;}

	// Store info
	void LoadStoreInfo();
	void SaveStoreInfo(bool AllowDelay = true);

	// Const version for external use:
	const BackupStoreInfo& GetBackupStoreInfo() const
	{
		return GetBackupStoreInfoInternal();
	}

	const std::string GetAccountName()
	{
		if(!mpFileSystem)
		{
			return "Unknown";
		}
		return mpFileSystem->GetBackupStoreInfo(true).GetAccountName(); // ReadOnly
	}

	// Client marker
	int64_t GetClientStoreMarker();
	void SetClientStoreMarker(int64_t ClientStoreMarker);

	// Usage information
	void GetStoreDiscUsageInfo(int64_t &rBlocksUsed, int64_t &rBlocksSoftLimit, int64_t &rBlocksHardLimit);
	bool HardLimitExceeded();

	// Reading directories
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    BackupStoreContext::GetDirectory(int64_t)
	//		Purpose: Return a reference to a directory. Valid only until the 
	//				 next time a function which affects directories is called.
	//				 Mainly this funciton, and creation of files.
	//		Created: 2003/09/02
	//
	// --------------------------------------------------------------------------
	const BackupStoreDirectory &GetDirectory(int64_t ObjectID)
	{
		// External callers aren't allowed to change it -- this function
		// merely turns the returned directory const.
		return GetDirectoryInternal(ObjectID);
	}

	// Manipulating files/directories
	int64_t AddFile(IOStream &rFile,
		int64_t InDirectory,
		int64_t ModificationTime,
		int64_t AttributesHash,
		int64_t DiffFromFileID,
		const BackupStoreFilename &rFilename,
		bool MarkFileWithSameNameAsOldVersions);
	int64_t AddDirectory(int64_t InDirectory,
		const BackupStoreFilename &rFilename,
		const StreamableMemBlock &Attributes,
		int64_t AttributesModTime,
		int64_t ModificationTime,
		bool &rAlreadyExists);
	void ChangeDirAttributes(int64_t Directory, const StreamableMemBlock &Attributes,
		int64_t AttributesModTime);
	bool ChangeFileAttributes(const BackupStoreFilename &rFilename,
		int64_t InDirectory, const StreamableMemBlock &Attributes,
		int64_t AttributesHash, int64_t &rObjectIDOut);
	bool DeleteFile(const BackupStoreFilename &rFilename, int64_t InDirectory,
		int64_t &rObjectIDOut);
	bool UndeleteFile(int64_t ObjectID, int64_t InDirectory);
	void DeleteDirectory(int64_t ObjectID, bool Undelete = false);
	void MoveObject(int64_t ObjectID, int64_t MoveFromDirectory,
		int64_t MoveToDirectory, const BackupStoreFilename &rNewFilename,
		bool MoveAllWithSameName, bool AllowMoveOverDeletedObject);

	// Manipulating objects
	enum
	{
		ObjectExists_Anything = 0,
		ObjectExists_File = 1,
		ObjectExists_Directory = 2
	};
	bool ObjectExists(int64_t ObjectID, int MustBe = ObjectExists_Anything);
	std::auto_ptr<IOStream> OpenObject(int64_t ObjectID);
	std::auto_ptr<IOStream> GetFile(int64_t ObjectID, int64_t InDirectory);

	// Info
	int32_t GetClientID() const {return mClientID;}
	const std::string& GetConnectionDetails() { return mConnectionDetails; }
	virtual int GetBlockSize() { return mpFileSystem->GetBlockSize(); }

private:
	BackupStoreDirectory &GetDirectoryInternal(int64_t ObjectID,
		bool AllowFlushCache = true);
	void SaveDirectory(BackupStoreDirectory &rDir);
	void RemoveDirectoryFromCache(int64_t ObjectID);
	void ClearDirectoryCache();
	void DeleteDirectoryRecurse(int64_t ObjectID, bool Undelete);
	int64_t AllocateObjectID();

	std::string mConnectionDetails;
	int32_t mClientID;
	HousekeepingInterface *mpHousekeeping;
	int mProtocolPhase;
	bool mClientHasAccount;
	bool mReadOnly;
	int mSaveStoreInfoDelay; // how many times to delay saving the store info

	// mapOwnFileSystem is initialised when we created our own BackupFileSystem,
	// using the old constructor. It ensures that the BackupFileSystem is deleted
	// when this BackupStoreContext is destroyed. TODO: stop using that old
	// constructor, and remove this member.
	std::auto_ptr<BackupFileSystem> mapOwnFileSystem;

	// mpFileSystem is always initialised when SetClientHasAccount() has been called,
	// whether or not we created it ourselves, and all internal functions use this
	// member instead of mapOwnFileSystem.
	BackupFileSystem* mpFileSystem;

	// Non-const version for internal use:
	BackupStoreInfo& GetBackupStoreInfoInternal() const
	{
		if(!mpFileSystem)
		{
			THROW_EXCEPTION(BackupStoreException, FileSystemNotInitialised);
		}
		return mpFileSystem->GetBackupStoreInfo(mReadOnly);
	}

	// Refcount database
	BackupStoreRefCountDatabase* mpRefCount;

	// Directory cache
	std::map<int64_t, BackupStoreDirectory*> mDirectoryCache;

public:
	class TestHook
	{
		public:
		virtual std::auto_ptr<BackupProtocolMessage>
			StartCommand(const BackupProtocolMessage& rCommand) = 0;
		virtual ~TestHook() { }
	};
	void SetTestHook(TestHook& rTestHook)
	{
		mpTestHook = &rTestHook;
	}
	std::auto_ptr<BackupProtocolMessage>
		StartCommandHook(const BackupProtocolMessage& rCommand)
	{
		if(mpTestHook)
		{
			return mpTestHook->StartCommand(rCommand);
		}
		return std::auto_ptr<BackupProtocolMessage>();
	}

private:
	TestHook* mpTestHook;
};

#endif // BACKUPCONTEXT__H

