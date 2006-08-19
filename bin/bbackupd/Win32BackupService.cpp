// Win32 service functions for Box Backup, by Nick Knight

#ifdef WIN32

#include "Box.h"
#include "BackupDaemon.h"
#include "MainHelper.h"
#include "BoxPortsAndFiles.h"
#include "BackupStoreException.h"

#include "MemLeakFindOn.h"

#include "Win32BackupService.h"

Win32BackupService gDaemonService;
extern HANDLE gStopServiceEvent;

unsigned int WINAPI RunService(LPVOID lpParameter)
{
	DWORD retVal = gDaemonService.WinService((const char*) lpParameter);
	SetEvent(gStopServiceEvent);
	return retVal;
}

void TerminateService(void)
{
	gDaemonService.SetTerminateWanted();
}

DWORD Win32BackupService::WinService(const char* pConfigFileName)
{
	char exepath[MAX_PATH];
	GetModuleFileName(NULL, exepath, sizeof(exepath));

	std::string configfile;
	
	if (pConfigFileName != NULL)
	{
		configfile = pConfigFileName;
	}
	else
	{
		// make the default config file name,
		// based on the program path
		configfile = exepath;
		configfile = configfile.substr(0,
			configfile.rfind(DIRECTORY_SEPARATOR_ASCHAR));
		configfile += DIRECTORY_SEPARATOR "bbackupd.conf";
	}

	const char *argv[] = {exepath, "-c", configfile.c_str()};
	int argc = sizeof(argv) / sizeof(*argv);
	DWORD ret;

	MAINHELPER_START
	ret = this->Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);
	MAINHELPER_END

	return ret;
}

#endif // WIN32
