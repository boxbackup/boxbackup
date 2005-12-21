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
	#include "Win32ServiceFunctions.h"
	#include "Win32BackupService.h"

	extern Win32BackupService gDaemonService;
#endif

int main(int argc, const char *argv[])
{
	MAINHELPER_START

#ifdef WIN32

	::openlog("Box Backup (bbackupd)", 0, 0);

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
		RemoveService();
		return 0;
	}
	if(argc == 2 && ::strcmp(argv[1], "-i") == 0)
	{
		InstallService();
		return 0;
	}
		
	// Under win32 we must initialise the Winsock library
	// before using sockets
		
	WSADATA info;

	if (WSAStartup(0x0101, &info) == SOCKET_ERROR) 
	{
		// box backup will not run without sockets
		::syslog(LOG_ERR, "Failed to initialise Windows Sockets");
		THROW_EXCEPTION(BackupStoreException, Internal)
	}

	EnableBackupRights();

	int ExitCode = 0;

	if (argc == 2 && ::strcmp(argv[1], "--service") == 0)
	{
		syslog(LOG_INFO,"Starting Box Backup Service");
		OurService();
	}
	else
	{
		ExitCode = gDaemonService.Main(
			BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);
	}

	// Clean up our sockets
	WSACleanup();

	::closelog();

	return ExitCode;

#else // !WIN32

	BackupDaemon daemon;
	return daemon.Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);

#endif // WIN32

	MAINHELPER_END
}
