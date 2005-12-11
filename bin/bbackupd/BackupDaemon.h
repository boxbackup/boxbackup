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

// #include <vector>
// #include <string>
// #include <memory>

// #include "BoxTime.h"
// #include "Socket.h"
// #include "SocketListen.h"
// #include "SocketStream.h"
#include "WinNamedPipeStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupDaemon
//		Purpose: Backup daemon
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
class BackupDaemon
{
public:
	BackupDaemon();
	~BackupDaemon();
private:
	BackupDaemon(const BackupDaemon &);
public:

	void Run();

	// Allow other classes to call this too
	enum
	{
		NotifyEvent_StoreFull = 0,
		NotifyEvent_ReadError = 1,
		NotifyEvent__MAX = 1
		// When adding notifications, remember to add strings to NotifySysadmin()
	};

private:
	void Run2();

	void CloseCommandConnection();
	
private:
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
		WinNamedPipeStream mListeningSocket;
	};
	
	// Using a socket?
	CommandSocketInfo *mpCommandSocketInfo;
	
	public:
	void RunHelperThread(void);

	private:
	bool mDoSyncFlagOut, mSyncIsForcedOut, mReceivedCommandConn;
};

#endif // BACKUPDAEMON__H
