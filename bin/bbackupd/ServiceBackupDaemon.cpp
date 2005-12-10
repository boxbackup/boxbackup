// Win32 service functions for Box Backup, by Nick Knight

#ifdef WIN32

#include "Box.h"
#include "BackupDaemon.h"
#include "MainHelper.h"
#include "BoxPortsAndFiles.h"
#include "BackupStoreException.h"

#include "MemLeakFindOn.h"

#include "ServiceBackupDaemon.h"

ServiceBackupDaemon gDaemonService;
extern HANDLE gStopServiceEvent;

unsigned int WINAPI RunService(LPVOID lpParameter)
{
	DWORD retVal = gDaemonService.WinService();
	SetEvent( gStopServiceEvent );
	return retVal;
}

void TerminateService(void)
{
	gDaemonService.SetTerminateWanted();
}

DWORD ServiceBackupDaemon::WinService(void)
{
	int argc = 2;
	//first off get the path name for the default 
	char buf[MAX_PATH];
	
	GetModuleFileName(NULL, buf, sizeof(buf));
	std::string buffer(buf);
	std::string conf( "-c");
	std::string cfile(buffer.substr(0,(buffer.find("bbackupd.exe"))) 
			+ "bbackupd.conf");

	const char *argv[] = {conf.c_str(), cfile.c_str()};

	MAINHELPER_START

	return this->Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, argc, argv);

	MAINHELPER_END
}

#endif // WIN32
