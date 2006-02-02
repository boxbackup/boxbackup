// --------------------------------------------------------------------------
//
// File
//		Name:    BackupDaemon.h
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef BACKUPDAEMON__H
#define BACKUPDAEMON__H

#include <vector>
#include <string>
#include <memory>

#include "BoxTime.h"
#include "Daemon.h"
#include "Socket.h"
#include "SocketListen.h"
#include "SocketStream.h"
#ifdef WIN32
	#include "WinNamedPipeStream.h"
#endif

class BackupClientDirectoryRecord;
class BackupClientContext;
class Configuration;
class BackupClientInodeToIDMap;
class ExcludeList;
class IOStreamGetLine;
class Archive;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupDaemon
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class BackupDaemon : public Daemon
{
public:
	BackupDaemon();
	~BackupDaemon();

	// methods below do partial (specialized) serialization of client state only
	void SerializeStoreObjectInfo(int64_t aClientStoreMarker, box_time_t theLastSyncTime, box_time_t theNextSyncTime) const;
	void DeserializeStoreObjectInfo(int64_t & aClientStoreMarker, box_time_t & theLastSyncTime, box_time_t & theNextSyncTime);
private:
	BackupDaemon(const BackupDaemon &);
public:

	void Run();
	virtual const char *DaemonName() const;
	virtual const char *DaemonBanner() const;
	const ConfigurationVerify *GetConfigVerify() const;

	bool FindLocationPathName(const std::string &rLocationName, std::string &rPathOut) const;

	enum
	{
		// Add stuff to this, make sure the textual equivalents in SetState() are changed too.
		State_Initialising = -1,
		State_Idle = 0,
		State_Connected = 1,
		State_Error = 2,
		State_StorageLimitExceeded = 3
	};

	int GetState() {return mState;}

	// Allow other classes to call this too
	enum
	{
		NotifyEvent_StoreFull = 0,
		NotifyEvent_ReadError = 1,
		NotifyEvent__MAX = 1
		// When adding notifications, remember to add strings to NotifySysadmin()
	};
	void NotifySysadmin(int Event);

private:
	void Run2();

	void DeleteAllLocations();
	void SetupLocations(BackupClientContext &rClientContext, const Configuration &rLocationsConf);

	void DeleteIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector);
	void DeleteAllIDMaps()
	{
		DeleteIDMapVector(mCurrentIDMaps);
		DeleteIDMapVector(mNewIDMaps);
	}
	void FillIDMapVector(std::vector<BackupClientInodeToIDMap *> &rVector, bool NewMaps);
	
	void SetupIDMapsForSync();
	void CommitIDMapsAfterSync();
	void DeleteCorruptBerkelyDbFiles();
	
	void MakeMapBaseName(unsigned int MountNumber, std::string &rNameOut) const;

	void SetState(int State);
	
	void WaitOnCommandSocket(box_time_t RequiredDelay, bool &DoSyncFlagOut, bool &SyncIsForcedOut);
	void CloseCommandConnection();
	void SendSyncStartOrFinish(bool SendStart);
	
	void TouchFileInWorkingDir(const char *Filename);

	void DeleteUnusedRootDirEntries(BackupClientContext &rContext);

#ifdef PLATFORM_CANNOT_FIND_PEER_UID_OF_UNIX_SOCKET
	// For warning user about potential security hole
	virtual void SetupInInitialProcess();
#endif

	int UseScriptToSeeIfSyncAllowed();

private:
	class Location
	{
	public:
		Location();
		~Location();

		void Deserialize(Archive & rArchive);
		void Serialize(Archive & rArchive) const;
	private:
		Location(const Location &);	// copy not allowed
		Location &operator=(const Location &);
	public:
		std::string mName;
		std::string mPath;
		std::auto_ptr<BackupClientDirectoryRecord> mpDirectoryRecord;
		int mIDMapIndex;
		ExcludeList *mpExcludeFiles;
		ExcludeList *mpExcludeDirs;
	};

	int mState;		// what the daemon is currently doing

	std::vector<Location *> mLocations;
	
	std::vector<std::string> mIDMapMounts;
	std::vector<BackupClientInodeToIDMap *> mCurrentIDMaps;
	std::vector<BackupClientInodeToIDMap *> mNewIDMaps;
	
	// For the command socket
	class CommandSocketInfo
	{
	public:
		CommandSocketInfo();
		~CommandSocketInfo();
	private:
		CommandSocketInfo(const CommandSocketInfo &);	// no copying
		CommandSocketInfo &operator=(const CommandSocketInfo &);
	public:
#ifdef WIN32
		WinNamedPipeStream mListeningSocket;
#else
		SocketListen<SocketStream, 1 /* listen backlog */> mListeningSocket;
		std::auto_ptr<SocketStream> mpConnectedSocket;
#endif
		IOStreamGetLine *mpGetLine;
	};
	
	// Using a socket?
	CommandSocketInfo *mpCommandSocketInfo;
	
	// Stop notifications being repeated.
	bool mNotificationsSent[NotifyEvent__MAX + 1];

	// Unused entries in the root directory wait a while before being deleted
	box_time_t mDeleteUnusedRootDirEntriesAfter;	// time to delete them
	std::vector<std::pair<int64_t,std::string> > mUnusedRootDirEntries;

#ifdef WIN32
	public:
	void RunHelperThread(void);

	private:
	bool mDoSyncFlagOut, mSyncIsForcedOut, mReceivedCommandConn;
#endif
};

#endif // BACKUPDAEMON__H
