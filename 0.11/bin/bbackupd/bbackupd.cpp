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

	Logging::SetProgramName("bbackupd");
	Logging::ToConsole(true);
	Logging::ToSyslog (true);
	
#ifdef WIN32

	EnableBackupRights();

	gpDaemonService = new Win32BackupService();
	ExitCode = gpDaemonService->Daemon::Main(
		BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE,
		argc, argv);
	delete gpDaemonService;

#else // !WIN32

	BackupDaemon daemon;
	ExitCode = daemon.Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);

#endif // WIN32

	MAINHELPER_END

	return ExitCode;
}
