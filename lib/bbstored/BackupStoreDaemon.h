// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDaemon.h
//		Purpose: Backup store daemon
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREDAEMON__H
#define BACKUPSTOREDAEMON__H

#include <string>

#include "ServerTLS.h"
#include "BoxPortsAndFiles.h"
#include "BackupConstants.h"
#include "BackupStoreContext.h"
#include "HousekeepStoreAccount.h"
#include "IOStreamGetLine.h"

class BackupStoreAccounts;
class BackupStoreAccountDatabase;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreDaemon
//		Purpose: Backup store daemon implementation
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
class BackupStoreDaemon : public ServerTLS<BOX_PORT_BBSTORED>,
	HousekeepingInterface, HousekeepingCallback
{
public:
	BackupStoreDaemon();
	~BackupStoreDaemon();
private:
	BackupStoreDaemon(const BackupStoreDaemon &rToCopy);
public:

	// For BackupStoreContext to communicate with housekeeping process
	void SendMessageToHousekeepingProcess(const void *Msg, int MsgLen)
	{
#ifndef WIN32
		mInterProcessCommsSocket.Write(Msg, MsgLen);
#endif
	}

protected:
	
	virtual void SetupInInitialProcess();

	virtual void Run();

	virtual void Connection(std::auto_ptr<SocketStreamTLS> apStream);
	void Connection2(std::auto_ptr<SocketStreamTLS> apStream);
	
	virtual const char *DaemonName() const;
	virtual std::string DaemonBanner() const;

	const ConfigurationVerify *GetConfigVerify() const;
	
	// Housekeeping functions
	void HousekeepingProcess();

	void LogConnectionStats(uint32_t accountId,
		const std::string& accountName, const BackupProtocolServer &server);

public:
	// HousekeepingInterface implementation
	virtual bool CheckForInterProcessMsg(int AccountNum = 0, int MaximumWaitTime = 0);
	void RunHousekeepingIfNeeded();

private:
	BackupStoreAccountDatabase *mpAccountDatabase;
	BackupStoreAccounts *mpAccounts;
	bool mExtendedLogging;
	bool mHaveForkedHousekeeping;
	bool mIsHousekeepingProcess;
	bool mHousekeepingInited;
	
	SocketStream mInterProcessCommsSocket;
	IOStreamGetLine mInterProcessComms;

	virtual void OnIdle();
	void HousekeepingInit();
	int64_t mLastHousekeepingRun;

	virtual void NotifyListenerIsReady()
	{
		// bbstored forks a housekeeping process (except on Windows) which may inherit the
		// exclusive lock on the PID file, so we might have to wait for it to be released,
		// by passing wait_for_shared_lock = true, which unfortunately extends the race
		// condition.
		WritePidFile(true); // wait_for_shared_lock
	}
public:
	void SetTestHook(BackupStoreContext::TestHook& rTestHook)
	{
		mpTestHook = &rTestHook;
	}

private:
	BackupStoreContext::TestHook* mpTestHook;
};


#endif // BACKUPSTOREDAEMON__H

