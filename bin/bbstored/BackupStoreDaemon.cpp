// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDaemon.cpp
//		Purpose: Backup store daemon
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#ifdef HAVE_SYSLOG_H
	#include <syslog.h>
#endif

#include "BackupContext.h"
#include "BackupStoreDaemon.h"
#include "BackupStoreConfigVerify.h"
#include "autogen_BackupProtocolServer.h"
#include "RaidFileController.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "BannerText.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::BackupStoreDaemon()
//		Purpose: Constructor
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
BackupStoreDaemon::BackupStoreDaemon()
	: mpAccountDatabase(0),
	  mpAccounts(0),
	  mExtendedLogging(false),
	  mHaveForkedHousekeeping(false),
	  mIsHousekeepingProcess(false),
	  mHousekeepingInited(false),
	  mInterProcessComms(mInterProcessCommsSocket)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::~BackupStoreDaemon()
//		Purpose: Destructor
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
BackupStoreDaemon::~BackupStoreDaemon()
{
	// Must delete this one before the database ...
	if(mpAccounts != 0)
	{
		delete mpAccounts;
		mpAccounts = 0;
	}
	// ... as the mpAccounts object has a reference to it
	if(mpAccountDatabase != 0)
	{
		delete mpAccountDatabase;
		mpAccountDatabase = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::DaemonName()
//		Purpose: Name of daemon
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
const char *BackupStoreDaemon::DaemonName() const
{
	return "bbstored";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::DaemonBanner()
//		Purpose: Daemon banner
//		Created: 1/1/04
//
// --------------------------------------------------------------------------
const char *BackupStoreDaemon::DaemonBanner() const
{
#ifndef NDEBUG
	// Don't display banner in debug builds
	return 0;
#else
	return BANNER_TEXT("Backup Store Server");
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::GetConfigVerify()
//		Purpose: Configuration definition
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
const ConfigurationVerify *BackupStoreDaemon::GetConfigVerify() const
{
	return &BackupConfigFileVerify;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::SetupInInitialProcess()
//		Purpose: Setup before we fork -- get raid file controller going
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
void BackupStoreDaemon::SetupInInitialProcess()
{
	const Configuration &config(GetConfiguration());
	
	// Initialise the raid files controller
	RaidFileController &rcontroller = RaidFileController::GetController();

	std::string raidFileConfig;

	#ifdef WIN32
		if (!config.KeyExists("RaidFileConf"))
		{
			raidFileConfig = BOX_GET_DEFAULT_RAIDFILE_CONFIG_FILE;
		}
		else
		{
			raidFileConfig = config.GetKeyValue("RaidFileConf");
		}
	#else
		raidFileConfig = config.GetKeyValue("RaidFileConf");
	#endif

	rcontroller.Initialise(raidFileConfig);
	
	// Load the account database
	std::auto_ptr<BackupStoreAccountDatabase> pdb(BackupStoreAccountDatabase::Read(config.GetKeyValue("AccountDatabase").c_str()));
	mpAccountDatabase = pdb.release();
	
	// Create a accounts object
	mpAccounts = new BackupStoreAccounts(*mpAccountDatabase);
	
	// Ready to go!
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::Run()
//		Purpose: Run shim for the store daemon -- read some config details
//		Created: 2003/10/24
//
// --------------------------------------------------------------------------
void BackupStoreDaemon::Run()
{
	// Get extended logging flag
	mExtendedLogging = false;
	const Configuration &config(GetConfiguration());
	mExtendedLogging = config.GetKeyValueBool("ExtendedLogging");
	
#ifdef WIN32	
	// Housekeeping runs synchronously on Win32
#else
	// Fork off housekeeping daemon -- must only do this the first time Run() is called
	if(!mHaveForkedHousekeeping)
	{
		// Open a socket pair for communication
		int sv[2] = {-1,-1};
		if(::socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sv) != 0)
		{
			THROW_EXCEPTION(ServerException, SocketPairFailed)
		}
		int whichSocket = 0;
		
		// Fork
		switch(::fork())
		{
		case -1:
			{
				// Error
				THROW_EXCEPTION(ServerException, ServerForkError)
			}
			break;
		case 0:
			{
				// In child process
				mIsHousekeepingProcess = true;
				SetProcessTitle("housekeeping, idle");
				whichSocket = 1;
				// Change the log name
				::openlog("bbstored/hk", LOG_PID, LOG_LOCAL6);
				// Log that housekeeping started
				BOX_INFO("Housekeeping process started");
				// Ignore term and hup
				// Parent will handle these and alert the child via the socket, don't want to randomly die
				::signal(SIGHUP, SIG_IGN);
				::signal(SIGTERM, SIG_IGN);
			}
			break;
		default:
			{
				// Parent process
				whichSocket = 0;
			}
			break;
		}
		
		// Mark that this has been, so -HUP doesn't try and do this again
		mHaveForkedHousekeeping = true;
		
		// Attach the comms thing to the right socket, and close the other one
		mInterProcessCommsSocket.Attach(sv[whichSocket]);
		
		if(::close(sv[(whichSocket == 0)?1:0]) != 0)
		{
			THROW_EXCEPTION(ServerException, SocketCloseError)
		}
	}
#endif // WIN32

	if(mIsHousekeepingProcess)
	{
		// Housekeeping process -- do other stuff
		HousekeepingProcess();
	}
	else
	{
		// In server process -- use the base class to do the magic
		ServerTLS<BOX_PORT_BBSTORED>::Run();

		if (!mInterProcessCommsSocket.IsOpened())
		{
			return;
		}

		// Why did it stop? Tell the housekeeping process to do the same
		if(IsReloadConfigWanted())
		{
			mInterProcessCommsSocket.Write("h\n", 2);
		}

		if(IsTerminateWanted())
		{
			mInterProcessCommsSocket.Write("t\n", 2);
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::Connection(SocketStreamTLS &)
//		Purpose: Handles a connection, by catching exceptions and
//			 delegating to Connection2
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
void BackupStoreDaemon::Connection(SocketStreamTLS &rStream)
{
	try
	{
		Connection2(rStream);
	}
	catch(BoxException &e)
	{
		BOX_ERROR("Error in child process, terminating connection: " <<
			e.what() << " (" << e.GetType() << "/" << 
			e.GetSubType() << ")");
	}
	catch(std::exception &e)
	{
		BOX_ERROR("Error in child process, terminating connection: " <<
			e.what());
	}
	catch(...)
	{
		BOX_ERROR("Error in child process, terminating connection: " <<
			"unknown exception");
	}
}
	
// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDaemon::Connection2(SocketStreamTLS &)
//		Purpose: Handles a connection from bbackupd
//		Created: 2006/11/12
//
// --------------------------------------------------------------------------
void BackupStoreDaemon::Connection2(SocketStreamTLS &rStream)
{
	// Get the common name from the certificate
	std::string clientCommonName(rStream.GetPeerCommonName());
	
	// Log the name
	BOX_INFO("Client certificate CN: " << clientCommonName);
	
	// Check it
	int32_t id;
	if(::sscanf(clientCommonName.c_str(), "BACKUP-%x", &id) != 1)
	{
		// Bad! Disconnect immediately
		return;
	}

	// Make ps listings clearer
	SetProcessTitle("client %08x", id);

	// Create a context, using this ID
	BackupContext context(id, *this);
	
	// See if the client has an account?
	if(mpAccounts && mpAccounts->AccountExists(id))
	{
		std::string root;
		int discSet;
		mpAccounts->GetAccountRoot(id, root, discSet);
		context.SetClientHasAccount(root, discSet);
	}

	// Handle a connection with the backup protocol
	BackupProtocolServer server(rStream);
	server.SetLogToSysLog(mExtendedLogging);
	server.SetTimeout(BACKUP_STORE_TIMEOUT);
	try
	{
		server.DoServer(context);
	}
	catch(...)
	{
		LogConnectionStats(clientCommonName.c_str(), rStream);
		throw;
	}
	LogConnectionStats(clientCommonName.c_str(), rStream);
	context.CleanUp();
}

void BackupStoreDaemon::LogConnectionStats(const char *commonName,
		const SocketStreamTLS &s)
{
	// Log the amount of data transferred
	BOX_INFO("Connection statistics for " << commonName << ":"
		" IN="  << s.GetBytesRead() <<
		" OUT=" << s.GetBytesWritten() <<
		" TOTAL=" << (s.GetBytesRead() + s.GetBytesWritten()));
}
