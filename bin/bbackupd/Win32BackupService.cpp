// Win32 service functions for Box Backup, by Nick Knight

#ifdef WIN32

#include "Box.h"
#include "BackupDaemon.h"
#include "MainHelper.h"
#include "BoxPortsAndFiles.h"
#include "BackupStoreException.h"

#include "MemLeakFindOn.h"

#include "Win32BackupService.h"

Win32BackupService* gpDaemonService = NULL;
extern HANDLE gStopServiceEvent;

unsigned int WINAPI RunService(LPVOID lpParameter)
{
	DWORD retVal = gpDaemonService->WinService((const char*) lpParameter);
	SetEvent(gStopServiceEvent);
	return retVal;
}

void TerminateService(void)
{
	gpDaemonService->SetTerminateWanted();
}

DWORD Win32BackupService::WinService(const char* pConfigFileName)
{
	DWORD ret;

	// keep MAINHELPER_START happy
	int argc = 0;
	char* argv[] = {NULL};

	MAINHELPER_START

	if (pConfigFileName != NULL)
	{
		ret = this->Main(pConfigFileName);
	}
	else
	{
		ret = this->Main(BOX_GET_DEFAULT_BBACKUPD_CONFIG_FILE);
	}

	MAINHELPER_END

	return ret;
}

#endif // WIN32
