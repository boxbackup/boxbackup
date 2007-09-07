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
#include "Logging.h"

#include "MemLeakFindOn.h"

#ifdef WIN32
	#include "Win32ServiceFunctions.h"
	#include "Win32BackupService.h"

	extern Win32BackupService* gpDaemonService;
#endif

int main(int argc, const char *argv[])
{
	int ExitCode = 0;

	MAINHELPER_START

	Logging::SetProgramName("Box Backup (bbackupd)");
	Logging::ToConsole(true);
	Logging::ToSyslog (true);
	
#ifdef WIN32

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
		return RemoveService();
	}
	if((argc == 2 || argc == 3) && ::strcmp(argv[1], "-i") == 0)
	{
		const char* config = NULL;
		if (argc == 3)
		{
			config = argv[2];
		}
		return InstallService(config);
	}

	bool runAsWin32Service = false;
	if (argc >= 2 && ::strcmp(argv[1], "--service") == 0)
	{
		runAsWin32Service = true;
	}

	gpDaemonService = new Win32BackupService();

	EnableBackupRights();

	if (runAsWin32Service)
	{
		BOX_INFO("Box Backup service starting");

		char* config = NULL;
		if (argc >= 3)
		{
			config = strdup(argv[2]);
		}

		ExitCode = OurService(config);

		if (config)
		{
			free(config);
		}

		BOX_INFO("Box Backup service shut down");
	}
	else
	{
		ExitCode = gpDaemonService->Main(
			BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE, argc, argv);
	}

	delete gpDaemonService;

#else // !WIN32

	BackupDaemon daemon;
	ExitCode = daemon.Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);

#endif // WIN32

	MAINHELPER_END

	return ExitCode;
}
