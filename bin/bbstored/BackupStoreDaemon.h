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

#include "ServerTLS.h"
#include "BoxPortsAndFiles.h"
#include "BackupConstants.h"
#include "BackupStoreContext.h"
#include "IOStreamGetLine.h"

class BackupStoreAccounts;
class BackupStoreAccountDatabase;
class HousekeepStoreAccount;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreDaemon
//		Purpose: Backup store daemon implementation
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
class BackupStoreDaemon : public ServerTLS<BOX_PORT_BBSTORED>
{
	friend class HousekeepStoreAccount;

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

	virtual void Connection(SocketStreamTLS &rStream);
	void Connection2(SocketStreamTLS &rStream);
	
	virtual const char *DaemonName() const;
	virtual std::string DaemonBanner() const;

	const ConfigurationVerify *GetConfigVerify() const;
	
	// Housekeeping functions
	void HousekeepingProcess();
	bool CheckForInterProcessMsg(int AccountNum = 0, int MaximumWaitTime = 0);

	void LogConnectionStats(const char *commonName, const SocketStreamTLS &s);

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
	void RunHousekeepingIfNeeded();
	int64_t mLastHousekeepingRun;

public:
	void SetTestHook(BackupStoreContext::TestHook& rTestHook)
	{
		mpTestHook = &rTestHook;
	}

private:
	BackupStoreContext::TestHook* mpTestHook;
};


#endif // BACKUPSTOREDAEMON__H

