// --------------------------------------------------------------------------
//
// File
//		Name:    CommandSocketManager.h
//		Purpose: Interface for managing command socket and processing
//			 client commands
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#ifndef COMMANDSOCKETMANAGER__H
#define COMMANDSOCKETMANAGER__H

#include "BoxTime.h"
#include "Configuration.h"
#include "Socket.h"
#include "SocketListen.h"
#include "SocketStream.h"

#ifdef WIN32
	#include "WinNamedPipeStream.h"
#endif

typedef enum
{
	// Add stuff to this, make sure the textual equivalents 
	// in BackupDaemon::SetState() are changed too.
	State_Initialising = -1,
	State_Idle = 0,
	State_Connected = 1,
	State_Error = 2,
	State_StorageLimitExceeded = 3
} state_t;

class IOStreamGetLine;

class CommandListener
{
	public:
	virtual ~CommandListener() { }
	virtual void SetReloadConfigWanted() = 0;
	virtual void SetTerminateWanted() = 0;
	virtual void SetSyncRequested() = 0;
	virtual void SetSyncForced() = 0;
};

class CommandSocketManager
{
public:
	CommandSocketManager(
		const Configuration& rConf, 
		CommandListener* pListener,
		const char * pSocketName);
	~CommandSocketManager();
	void Wait(box_time_t RequiredDelay);
	void CloseConnection();
	void SendSyncStartOrFinish(bool SendStart);
	void SendStateUpdate(state_t newState);

private:
	CommandSocketManager(const CommandSocketManager &);	// no copying
	CommandSocketManager &operator=(const CommandSocketManager &);

#ifdef WIN32
	WinNamedPipeStream mListeningSocket;
#else
	SocketListen<SocketStream, 1 /* listen backlog */> mListeningSocket;
	std::auto_ptr<SocketStream> mpConnectedSocket;
#endif

	IOStreamGetLine *mpGetLine;
	CommandListener* mpListener;
	Configuration mConf;
	state_t mState;
};

#endif // COMMANDSOCKETMANAGER__H
