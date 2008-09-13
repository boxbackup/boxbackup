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

#include "NamedLock.h"
#include "ProtocolObject.h"
#include "Utils.h"

class BackupStoreDirectory;
class BackupStoreFilename;
class BackupStoreDaemon;
class BackupStoreInfo;
class IOStream;
class BackupProtocolObject;
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
	BackupStoreContext(int32_t ClientID, HousekeepingInterface &rDaemon);
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
	void SetPhase(int NewPhase) {mProtocolPhase = NewPhase;}
	
	// Read only locking
	bool SessionIsReadOnly() {return mReadOnly;}
	bool AttemptToGetWriteLock();

	void SetClientHasAccount(const std::string &rStoreRoot, int StoreDiscSet) {mClientHasAccount = true; mStoreRoot = rStoreRoot; mStoreDiscSet = StoreDiscSet;}
	bool GetClientHasAccount() const {return mClientHasAccount;}
	const std::string &GetStoreRoot() const {return mStoreRoot;}
	int GetStoreDiscSet() const {return mStoreDiscSet;}

	// Store info
	void LoadStoreInfo();
	void SaveStoreInfo(bool AllowDelay = true);
	const BackupStoreInfo &GetBackupStoreInfo() const;

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
		// merely turns the the returned directory const.
		return GetDirectoryInternal(ObjectID);
	}
	
	// Manipulating files/directories
	int64_t AddFile(IOStream &rFile, int64_t InDirectory, int64_t ModificationTime, int64_t AttributesHash, int64_t DiffFromFileID, const BackupStoreFilename &rFilename, bool MarkFileWithSameNameAsOldVersions);
	int64_t AddDirectory(int64_t InDirectory, const BackupStoreFilename &rFilename, const StreamableMemBlock &Attributes, int64_t AttributesModTime, bool &rAlreadyExists);
	void ChangeDirAttributes(int64_t Directory, const StreamableMemBlock &Attributes, int64_t AttributesModTime);
	bool ChangeFileAttributes(const BackupStoreFilename &rFilename, int64_t InDirectory, const StreamableMemBlock &Attributes, int64_t AttributesHash, int64_t &rObjectIDOut);
	bool DeleteFile(const BackupStoreFilename &rFilename, int64_t InDirectory, int64_t &rObjectIDOut);
	bool UndeleteFile(int64_t ObjectID, int64_t InDirectory);
	void DeleteDirectory(int64_t ObjectID, bool Undelete = false);
	void MoveObject(int64_t ObjectID, int64_t MoveFromDirectory, int64_t MoveToDirectory, const BackupStoreFilename &rNewFilename, bool MoveAllWithSameName, bool AllowMoveOverDeletedObject);

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

private:
	void MakeObjectFilename(int64_t ObjectID, std::string &rOutput, bool EnsureDirectoryExists = false);
	BackupStoreDirectory &GetDirectoryInternal(int64_t ObjectID);
	void SaveDirectory(BackupStoreDirectory &rDir, int64_t ObjectID);
	void RemoveDirectoryFromCache(int64_t ObjectID);
	void DeleteDirectoryRecurse(int64_t ObjectID, int64_t &rBlocksDeletedOut, bool Undelete);
	int64_t AllocateObjectID();

private:
	int32_t mClientID;
	HousekeepingInterface &mrDaemon;
	int mProtocolPhase;
	bool mClientHasAccount;
	std::string mStoreRoot;	// has final directory separator
	int mStoreDiscSet;
	bool mReadOnly;
	NamedLock mWriteLock;
	int mSaveStoreInfoDelay; // how many times to delay saving the store info
	
	// Store info
	std::auto_ptr<BackupStoreInfo> mpStoreInfo;
	
	// Directory cache
	std::map<int64_t, BackupStoreDirectory*> mDirectoryCache;

public:
	class TestHook
	{
		public:
		virtual std::auto_ptr<ProtocolObject> StartCommand(BackupProtocolObject&
			rCommand) = 0;
		virtual ~TestHook() { }
	};
	void SetTestHook(TestHook& rTestHook)
	{
		mpTestHook = &rTestHook;
	}
	std::auto_ptr<ProtocolObject> StartCommandHook(BackupProtocolObject& rCommand)
	{
		if(mpTestHook)
		{
			return mpTestHook->StartCommand(rCommand);
		}
		return std::auto_ptr<ProtocolObject>();
	}

private:
	TestHook* mpTestHook;
};

#endif // BACKUPCONTEXT__H

