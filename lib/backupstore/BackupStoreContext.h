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

#include "BackupStoreDirectory.h"
#include "BackupStoreInfo.h"
#include "BackupStoreRefCountDatabase.h"
#include "NamedLock.h"
#include "Utils.h"

class BackupStoreFilename;
class BackupStoreRefCountDatabase;
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
	~BackupStoreContext();
private:
	BackupStoreContext(const BackupStoreContext &rToCopy);
public:

	void ReceivedFinishCommand();
	void CleanUp();

	int32_t GetClientID() {return mClientID;}

	enum
	{
		Phase_START		= 0,
		Phase_Version	= 0,
		Phase_Login		= 1,
		Phase_Commands	= 2
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
		if(mWriteLock.GotLock())
		{
			mWriteLock.ReleaseLock();
		}
	}

	void SetClientHasAccount(const std::string &rStoreRoot, int StoreDiscSet) {mClientHasAccount = true; mAccountRootDir = rStoreRoot; mStoreDiscSet = StoreDiscSet;}
	bool GetClientHasAccount() const {return mClientHasAccount;}
	const std::string &GetAccountRoot() const {return mAccountRootDir;}
	int GetStoreDiscSet() const {return mStoreDiscSet;}

	// Store info
	void LoadStoreInfo();
	void SaveStoreInfo(bool AllowDelay = true);
	const BackupStoreInfo &GetBackupStoreInfo() const;
	const std::string GetAccountName()
	{
		if(!mapStoreInfo.get())
		{
			return "Unknown";
		}
		return mapStoreInfo->GetAccountName();
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
	void AddReference(int64_t ObjectID,
		int64_t OldDirectoryID,
		int64_t NewDirectoryID,
		const BackupStoreFilename &rNewObjectFileName);
	int64_t MakeUnique(int64_t ObjectToMakeUniqueID,
		int64_t ContainingDirID);
	void ChangeDirAttributes(int64_t Directory, const StreamableMemBlock &Attributes, int64_t AttributesModTime);
	bool ChangeFileAttributes(const BackupStoreFilename &rFilename, int64_t InDirectory, const StreamableMemBlock &Attributes, int64_t AttributesHash, int64_t &rObjectIDOut);
	bool DeleteFile(const BackupStoreFilename &rFilename, int64_t InDirectory, int64_t &rObjectIDOut);
	bool UndeleteFile(int64_t ObjectID, int64_t InDirectory);
	void DeleteDirectory(int64_t ObjectID, bool Undelete = false);
	bool DeleteNow(int64_t ObjectToDeleteID, int64_t ContainingDirID);
	void MoveObject(int64_t ObjectID, int64_t MoveFromDirectory, int64_t MoveToDirectory, const BackupStoreFilename &rNewFilename, bool MoveAllWithSameName, bool AllowMoveOverDeletedObject);

private:
	void DeleteEntryNow(BackupStoreDirectory::Entry& rEntry);
	void DeleteDirEntriesNow(int64_t DirectoryID);

public:
	// Manipulating objects
	enum
	{
		ObjectExists_Anything = 0,
		ObjectExists_File = 1,
		ObjectExists_Directory = 2
	};
	bool ObjectExists(int64_t ObjectID, int MustBe = ObjectExists_Anything);
	std::auto_ptr<IOStream> OpenObject(int64_t ObjectID);
	
	// Info
	int32_t GetClientID() const {return mClientID;}
	const std::string& GetConnectionDetails() { return mConnectionDetails; }

private:
	void MakeObjectFilename(int64_t ObjectID, std::string &rOutput, bool EnsureDirectoryExists = false);
	BackupStoreDirectory &GetDirectoryInternal(int64_t ObjectID);
	void SaveDirectory(BackupStoreDirectory &rDir);
	void RemoveDirectoryFromCache(int64_t ObjectID);
	void ClearDirectoryCache();
	void DeleteDirectoryRecurse(int64_t ObjectID, bool Undelete);
	int64_t AllocateObjectID();
	void AssertMutable(int64_t ObjectID);

	std::string mConnectionDetails;
	int32_t mClientID;
	HousekeepingInterface *mpHousekeeping;
	int mProtocolPhase;
	bool mClientHasAccount;
	std::string mAccountRootDir;	// has final directory separator
	int mStoreDiscSet;
	bool mReadOnly;
	NamedLock mWriteLock;
	int mSaveStoreInfoDelay; // how many times to delay saving the store info
	
	// Store info
	std::auto_ptr<BackupStoreInfo> mapStoreInfo;

	// Refcount database
	std::auto_ptr<BackupStoreRefCountDatabase> mapRefCount;
	
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

