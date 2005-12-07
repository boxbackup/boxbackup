// --------------------------------------------------------------------------
//
// File
//		Name:    bbackupd.cpp
//		Purpose: main file for backup daemon
//		Created: 2003/10/11
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BackupDaemon.h"
#include "MainHelper.h"
#include "BoxPortsAndFiles.h"
#include "BackupStoreException.h"

#include "MemLeakFindOn.h"

#ifdef WIN32
	#include "bbwinservice.h"
	#include "ServiceBackupDaemon.h"

	extern ServiceBackupDaemon daemonService;
#endif

int main(int argc, const char *argv[])
{
	MAINHELPER_START

#ifdef WIN32

	if(argc == 3 && ::strcmp(argv[1], "-c") == 0)
	{
		// Under win32 we must initialise the Winsock library
		// before using sockets
		
		WSADATA info;

		if (WSAStartup(MAKELONG(1, 1), &info) == SOCKET_ERROR) 
		{
			// box backup will not run without sockets
			THROW_EXCEPTION(BackupStoreException, Internal)
		}

		EnableBackupRights();

		int ExitCode = daemonService.Main(
			BOX_FILE_BBACKUPD_DEFAULT_CONFIG, 
			argc, argv);

		// Clean up our sockets
		WSACleanup();

		return ExitCode;
	}
	
	if(argc == 2 &&
		(::strcmp(argv[1], "--help") == 0 ||
		 ::strcmp(argv[1], "-h") == 0))
	{
		printf("-h help, -i install service, -r remove service,\n"
			"-c <config file> start daemon now");
		return 2;
	}
	if(argc == 2 && ::strcmp(argv[1], "-r") == 0)
	{
		removeService();
		return 0;
	}
	if(argc == 2 && ::strcmp(argv[1], "-i") == 0)
	{
		installService();
		return 0;
	}

	EnableBackupRights();

	//if no match we assume it is the service starting
	//syslog(LOG_INFO,"Starting Box Backup Service");
	ourService();
		
	return 0;

#else // ! WIN32

	BackupDaemon daemon;
	return daemon.Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);
	
#endif // WIN32

	MAINHELPER_END
}
